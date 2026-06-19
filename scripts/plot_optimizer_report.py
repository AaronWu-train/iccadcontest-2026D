#!/usr/bin/env python3
"""Generate auditable report plots for optimizer experiments."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import statistics
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


OPTIMIZERS: list[tuple[str, str, str]] = [
    ("A1", "greedy-violation-path", "Best-improvement greedy from violated path endpoints"),
    ("A2", "sa", "Single simulated annealing flow"),
    ("A3", "isa", "Iterated simulated annealing"),
    ("A4", "greedy-critical-endpoint", "Greedy candidates from top critical endpoints"),
    ("A5", "greedy-upstream-window", "Greedy candidates from upstream endpoint windows"),
    ("A6", "greedy-repair-recover", "Timing repair followed by area recovery"),
    ("A7", "greedy-randomized-rcl", "Randomized greedy top-k move selection with restarts"),
    ("A8", "tabu", "Tabu search with aspiration"),
]
OPT_ID = {alias: oid for oid, alias, _role in OPTIMIZERS}
OPT_ROLE = {alias: role for _oid, alias, role in OPTIMIZERS}
OPT_ORDER = [alias for _oid, alias, _role in OPTIMIZERS]
TIE_EPSILON = 0.005
METRIC_COLUMNS = ["tns_ss", "wns_ss", "tns_ff", "wns_ff"]
GROUP_AVERAGE_MAX_ELAPSED_SEC = 550.0
GROUP_AVERAGE_SPECS = [
    (
        "fig16_19_greedy",
        [
            "greedy-violation-path",
            "greedy-critical-endpoint",
            "greedy-upstream-window",
            "greedy-random",
            "greedy-union-pool",
        ],
        {
            "best": "fig16_greedy_average_best_score_progress",
            "setup": "fig17_greedy_normalized_setup_slack_optimization_average",
            "hold": "fig18_greedy_normalized_hold_slack_optimization_average",
            "area": "fig19_greedy_normalized_area_optimization_average",
        },
    ),
    (
        "fig20_23_random",
        [
            "tabu-random",
            "sa-random",
            "isa-random",
            "two-step-random",
        ],
        {
            "best": "fig20_random_average_best_score_progress",
            "setup": "fig21_random_normalized_setup_slack_optimization_average",
            "hold": "fig22_random_normalized_hold_slack_optimization_average",
            "area": "fig23_random_normalized_area_optimization_average",
        },
    ),
    (
        "fig24_27_union_pool",
        [
            "isa-sampled-union-pool",
            "sa-sampled-union-pool",
            "tabu-union-pool",
            "two-step-union-pool",
        ],
        {
            "best": "fig24_union_pool_average_best_score_progress",
            "setup": "fig25_union_pool_normalized_setup_slack_optimization_average",
            "hold": "fig26_union_pool_normalized_hold_slack_optimization_average",
            "area": "fig27_union_pool_normalized_area_optimization_average",
        },
    ),
    (
        "fig28_31_best_four",
        [
            "tabu-random",
            "tabu-union-pool",
            "isa-random",
            "isa-sampled-union-pool",
        ],
        {
            "best": "fig28_best_four_average_best_score_progress",
            "setup": "fig29_best_four_normalized_setup_slack_optimization_average",
            "hold": "fig30_best_four_normalized_hold_slack_optimization_average",
            "area": "fig31_best_four_normalized_area_optimization_average",
        },
    ),
]


class ReportError(RuntimeError):
    pass


@dataclass
class ManifestRow:
    input_file: str
    file_type: str
    optimizer: str = ""
    testcase: str = ""
    seed: str = ""
    rows: int = 0
    sha256: str = ""
    parse_status: str = "ok"
    warning: str = ""


@dataclass
class FinalRun:
    optimizer: str
    testcase: str
    seed: str
    final_score: float
    tns_ss: float = math.nan
    wns_ss: float = math.nan
    tns_ff: float = math.nan
    wns_ff: float = math.nan
    area: float = math.nan
    runtime_sec: float = math.nan
    source_file: str = ""


@dataclass
class ProgressRow:
    optimizer: str
    testcase: str
    seed: str
    step: int
    elapsed_sec: float
    best_score: float
    current_score: float = math.nan
    tns_ss: float = math.nan
    wns_ss: float = math.nan
    tns_ff: float = math.nan
    wns_ff: float = math.nan
    area: float = math.nan
    phase: str = ""
    event: str = ""


@dataclass
class ReportData:
    final_runs: list[FinalRun] = field(default_factory=list)
    progress_rows: list[ProgressRow] = field(default_factory=list)
    manifest: list[ManifestRow] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    detected_columns: dict[str, list[str]] = field(default_factory=dict)
    column_mapping: dict[str, dict[str, str]] = field(default_factory=dict)
    selected_testcases: list[str] = field(default_factory=list)


def parse_float(value: Any) -> float:
    if value is None:
        return math.nan
    text = str(value).strip()
    if text in {"", "-", "NA", "N/A", "nan"}:
        return math.nan
    try:
        return float(text)
    except ValueError:
        return math.nan


def parse_int(value: Any) -> int:
    if value is None:
        return -1
    try:
        return int(str(value).strip())
    except ValueError:
        return -1


def fmt(value: Any) -> str:
    if isinstance(value, float):
        if math.isnan(value):
            return "NA"
        return f"{value:.10g}"
    if value is None:
        return "NA"
    return str(value)


def median(values: Iterable[float]) -> float:
    clean = [v for v in values if not math.isnan(v)]
    return statistics.median(clean) if clean else math.nan


def quantile(values: Iterable[float], q: float) -> float:
    clean = sorted(v for v in values if not math.isnan(v))
    if not clean:
        return math.nan
    if len(clean) == 1:
        return clean[0]
    pos = (len(clean) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return clean[lo]
    return clean[lo] * (hi - pos) + clean[hi] * (pos - lo)


def mean(values: Iterable[float]) -> float:
    clean = [v for v in values if not math.isnan(v)]
    return sum(clean) / len(clean) if clean else math.nan


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def rel(path: Path, root: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def read_tsv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames is None:
            raise ReportError(f"{path}: missing header row")
        rows = [dict(row) for row in reader]
    return list(reader.fieldnames), rows


def canonicalize_columns(
    columns: list[str], aliases: dict[str, list[str]], required: list[str], file_path: Path, figure: str
) -> dict[str, str]:
    normalized = {column.strip().lower(): column for column in columns}
    mapping: dict[str, str] = {}
    missing: list[str] = []
    for canonical, names in aliases.items():
        for name in [canonical, *names]:
            key = name.strip().lower()
            if key in normalized:
                mapping[canonical] = normalized[key]
                break
        if canonical in required and canonical not in mapping:
            missing.append(canonical)
    if missing:
        raise ReportError(
            f"{file_path}: missing required column(s) {', '.join(missing)} for {figure}. "
            "Regenerate the aggregate results or include progress.tsv files for progress figures."
        )
    return mapping


def discover_progress_files(run_dir: Path) -> list[Path]:
    direct = run_dir / "progress.tsv"
    files: list[Path] = []
    if direct.is_file():
        files.append(direct)
    files.extend(sorted((run_dir / "progress").glob("*/*/*/progress.tsv")))
    files.extend(sorted((run_dir / "progress").glob("*/*/progress.tsv")))
    if not files:
        files.extend(sorted(run_dir.rglob("progress.tsv")))
    return sorted(set(files))


def infer_progress_context(path: Path, row: dict[str, str]) -> tuple[str, str, str]:
    parent = path.parent
    seed = row.get("seed", "")
    if not seed and parent.parent.name.startswith("seed_"):
        seed = parent.parent.name.removeprefix("seed_")
    if not seed:
        seed = "default"
    if parent.parent.name.startswith("seed_"):
        optimizer = row.get("optimizer") or parent.parent.parent.name
        testcase = row.get("testcase") or parent.name
    else:
        optimizer = row.get("optimizer") or parent.parent.name
        testcase = row.get("testcase") or parent.name
    return optimizer, testcase, seed


def load_inputs(run_dir: Path) -> ReportData:
    data = ReportData()
    if not run_dir.exists():
        raise ReportError(f"Missing run directory: {run_dir}")

    aggregate_path = run_dir / "results.tsv"
    if not aggregate_path.is_file():
        raise ReportError(
            f"Missing required file: {aggregate_path}. Figure 1-5 and final summary tables cannot be generated. "
            "Run ./scripts/slurm_run_all_optimizers.sh and aggregate the run first."
        )
    load_results(aggregate_path, run_dir, data)

    for optional_name in ["by_optimizer.tsv", "best_by_testcase.tsv", "summary.txt"]:
        optional = run_dir / optional_name
        if optional.is_file():
            data.manifest.append(
                ManifestRow(rel(optional, run_dir), optional_name, rows=count_rows(optional), sha256=sha256(optional))
            )
            if optional.suffix == ".tsv":
                columns, _rows = read_tsv(optional)
                data.detected_columns[optional_name] = columns
                data.column_mapping[optional_name] = validate_optional_tsv(optional_name, optional, columns)

    progress_files = discover_progress_files(run_dir)
    if not progress_files:
        data.warnings.append(
            "No progress.tsv files found. Anytime figures/tables (time-to-feasible, time-to-gap, progress IQR) "
            "were skipped. Use a Slurm optimizer run or pass --progress-dir to cadd0040."
        )
    for path in progress_files:
        load_progress(path, run_dir, data)
    enrich_final_runs_from_progress(data)

    return data


def validate_optional_tsv(name: str, path: Path, columns: list[str]) -> dict[str, str]:
    if name == "by_optimizer.tsv":
        aliases = {
            "optimizer": ["OPTIMIZER"],
            "ok": ["OK"],
            "fail": ["FAIL"],
            "avg_final": ["AVG_FINAL", "average_final"],
            "total_time": ["TOTAL_TIME", "runtime_sec"],
        }
        required = ["optimizer", "ok", "fail", "avg_final", "total_time"]
    elif name == "best_by_testcase.tsv":
        aliases = {
            "testcase": ["TESTCASE"],
            "best_final": ["BEST_FINAL", "best_score", "score"],
            "optimizer": ["OPTIMIZER", "config", "CONFIG"],
            "seed": ["SEED"],
        }
        required = ["testcase", "best_final"]
    else:
        return {}
    return canonicalize_columns(columns, aliases, required, path, f"optional audit input {name}")


def count_rows(path: Path) -> int:
    if path.suffix != ".tsv":
        return sum(1 for _line in path.open(errors="replace"))
    try:
        _columns, rows = read_tsv(path)
        return len(rows)
    except Exception:
        return 0


def load_results(path: Path, run_dir: Path, data: ReportData) -> None:
    columns, rows = read_tsv(path)
    data.detected_columns["results.tsv"] = columns
    aliases = {
        "optimizer": ["OPTIMIZER", "config", "CONFIG"],
        "testcase": ["TESTCASE"],
        "seed": ["SEED"],
        "final_score": ["FINAL", "score", "final", "best_score", "BEST_FINAL"],
        "runtime_sec": ["TIME(s)", "elapsed_sec", "runtime_sec", "time_sec"],
        "status": ["STATUS"],
        "tns_ss": ["TNS_SS"],
        "wns_ss": ["WNS_SS"],
        "tns_ff": ["TNS_FF"],
        "wns_ff": ["WNS_FF"],
        "area": ["AREA"],
    }
    mapping = canonicalize_columns(
        columns, aliases, ["optimizer", "testcase", "final_score"], path, "aggregate report figures"
    )
    data.column_mapping["results.tsv"] = mapping
    warning = ""
    if "seed" not in mapping:
        warning = "seed column missing; using seed=default"
        data.warnings.append(f"{path}: seed column missing; using seed=default for final-run tables.")
    valid_rows = 0
    for row in rows:
        status = row.get(mapping.get("status", ""), "OK")
        final_score = parse_float(row.get(mapping["final_score"]))
        if status not in {"OK", "ok", ""} or math.isnan(final_score):
            continue
        valid_rows += 1
        data.final_runs.append(
            FinalRun(
                optimizer=row.get(mapping["optimizer"], "").strip(),
                testcase=row.get(mapping["testcase"], "").strip(),
                seed=row.get(mapping["seed"], "default").strip() if "seed" in mapping else "default",
                final_score=final_score,
                runtime_sec=parse_float(row.get(mapping.get("runtime_sec", ""))),
                tns_ss=parse_float(row.get(mapping.get("tns_ss", ""))),
                wns_ss=parse_float(row.get(mapping.get("wns_ss", ""))),
                tns_ff=parse_float(row.get(mapping.get("tns_ff", ""))),
                wns_ff=parse_float(row.get(mapping.get("wns_ff", ""))),
                area=parse_float(row.get(mapping.get("area", ""))),
                source_file=rel(path, run_dir),
            )
        )
    if not data.final_runs:
        raise ReportError(f"{path}: no usable OK rows with numeric final score; aggregate figures cannot be generated.")
    data.manifest.append(
        ManifestRow(rel(path, run_dir), "results.tsv", rows=len(rows), sha256=sha256(path), warning=warning)
    )


def load_progress(path: Path, run_dir: Path, data: ReportData) -> None:
    columns, rows = read_tsv(path)
    aliases = {
        "optimizer": ["OPTIMIZER"],
        "testcase": ["TESTCASE"],
        "seed": ["SEED"],
        "step": ["STEP"],
        "elapsed_sec": ["elapsed_seconds", "time_sec"],
        "best_score": ["BEST_SCORE", "score"],
        "current_score": ["CURRENT_SCORE"],
        "phase": ["PHASE"],
        "event": ["EVENT"],
        "tns_ss": ["TNS_SS"],
        "wns_ss": ["WNS_SS"],
        "tns_ff": ["TNS_FF"],
        "wns_ff": ["WNS_FF"],
        "area": ["AREA"],
    }
    mapping = canonicalize_columns(
        columns, aliases, ["step", "elapsed_sec", "best_score"], path, "anytime progress figures"
    )
    data.detected_columns[rel(path, run_dir)] = columns
    data.column_mapping[rel(path, run_dir)] = mapping
    parsed = 0
    opt = tc = seed = ""
    for row in rows:
        inferred_opt, inferred_tc, inferred_seed = infer_progress_context(path, row)
        opt = row.get(mapping.get("optimizer", ""), inferred_opt) or inferred_opt
        tc = row.get(mapping.get("testcase", ""), inferred_tc) or inferred_tc
        seed = row.get(mapping.get("seed", ""), inferred_seed) or inferred_seed
        item = ProgressRow(
            optimizer=opt.strip(),
            testcase=tc.strip(),
            seed=seed.strip(),
            step=parse_int(row.get(mapping["step"])),
            elapsed_sec=parse_float(row.get(mapping["elapsed_sec"])),
            best_score=parse_float(row.get(mapping["best_score"])),
            current_score=parse_float(row.get(mapping.get("current_score", ""))),
            tns_ss=parse_float(row.get(mapping.get("tns_ss", ""))),
            wns_ss=parse_float(row.get(mapping.get("wns_ss", ""))),
            tns_ff=parse_float(row.get(mapping.get("tns_ff", ""))),
            wns_ff=parse_float(row.get(mapping.get("wns_ff", ""))),
            area=parse_float(row.get(mapping.get("area", ""))),
            phase=row.get(mapping.get("phase", ""), ""),
            event=row.get(mapping.get("event", ""), ""),
        )
        if not math.isnan(item.elapsed_sec) and not math.isnan(item.best_score):
            data.progress_rows.append(item)
            parsed += 1
    warning = ""
    if parsed == 0:
        warning = "no usable progress rows"
        data.warnings.append(f"{path}: no usable progress rows for anytime figures.")
    data.manifest.append(
        ManifestRow(rel(path, run_dir), "progress.tsv", opt, tc, seed, len(rows), sha256(path), "ok", warning)
    )


def enrich_final_runs_from_progress(data: ReportData) -> None:
    if not data.progress_rows:
        if not any(not math.isnan(getattr(run, metric)) for run in data.final_runs for metric in METRIC_COLUMNS):
            data.warnings.append(
                "Aggregate results do not contain timing metrics and no progress traces were available; "
                "feasibility is reported as NA for final summary figures."
            )
        return
    last_progress: dict[tuple[str, str, str], ProgressRow] = {}
    for row in data.progress_rows:
        key = (row.optimizer, row.testcase, row.seed)
        previous = last_progress.get(key)
        if previous is None or (row.elapsed_sec, row.step) >= (previous.elapsed_sec, previous.step):
            last_progress[key] = row
    enriched = 0
    for run in data.final_runs:
        row = last_progress.get((run.optimizer, run.testcase, run.seed))
        if row is None:
            continue
        for metric in [*METRIC_COLUMNS, "area"]:
            if math.isnan(getattr(run, metric)) and not math.isnan(getattr(row, metric)):
                setattr(run, metric, getattr(row, metric))
                enriched += 1
    if enriched:
        data.warnings.append(
            f"Filled {enriched} missing final-run timing/area value(s) from matching final progress trace rows."
        )
    if not any(not math.isnan(getattr(run, metric)) for run in data.final_runs for metric in METRIC_COLUMNS):
        data.warnings.append(
            "No final-run timing metrics were available in aggregate results or matching progress traces; "
            "feasibility is reported as NA for final summary figures."
        )


def feasible_from_metrics(values: dict[str, float]) -> bool | None:
    available = [values.get(name, math.nan) for name in METRIC_COLUMNS]
    clean = [value for value in available if not math.isnan(value)]
    if not clean:
        return None
    return all(value >= -1e-12 for value in clean)


def best_known_by_testcase(final_runs: list[FinalRun]) -> dict[str, float]:
    best: dict[str, float] = {}
    for run in final_runs:
        if run.testcase not in best or run.final_score > best[run.testcase]:
            best[run.testcase] = run.final_score
    return best


def normalized_gap(final_score: float, best_known: float) -> float:
    return (best_known - final_score) / max(1.0, abs(best_known))


def final_run_rows(final_runs: list[FinalRun]) -> list[dict[str, Any]]:
    best = best_known_by_testcase(final_runs)
    rows: list[dict[str, Any]] = []
    for run in final_runs:
        metrics = {
            "tns_ss": run.tns_ss,
            "wns_ss": run.wns_ss,
            "tns_ff": run.tns_ff,
            "wns_ff": run.wns_ff,
        }
        feasible = feasible_from_metrics(metrics)
        rows.append(
            {
                "optimizer_id": OPT_ID.get(run.optimizer, ""),
                "optimizer_alias": run.optimizer,
                "testcase": run.testcase,
                "seed": run.seed,
                "final_score": run.final_score,
                "best_known_score_for_testcase": best[run.testcase],
                "normalized_gap": normalized_gap(run.final_score, best[run.testcase]),
                "feasible": "NA" if feasible is None else str(feasible).lower(),
                "area": run.area,
                "runtime_sec": run.runtime_sec,
                "tns_ss": run.tns_ss,
                "wns_ss": run.wns_ss,
                "tns_ff": run.tns_ff,
                "wns_ff": run.wns_ff,
            }
        )
    return rows


def group_progress(progress_rows: list[ProgressRow]) -> dict[tuple[str, str, str], list[ProgressRow]]:
    grouped: dict[tuple[str, str, str], list[ProgressRow]] = defaultdict(list)
    for row in progress_rows:
        grouped[(row.optimizer, row.testcase, row.seed)].append(row)
    for rows in grouped.values():
        rows.sort(key=lambda row: (row.elapsed_sec, row.step))
    return grouped


def compute_time_to_feasible(progress_rows: list[ProgressRow]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for (optimizer, testcase, seed), rows in group_progress(progress_rows).items():
        if not any(not math.isnan(getattr(row, metric)) for row in rows for metric in METRIC_COLUMNS):
            continue
        first = math.nan
        for row in rows:
            ok = feasible_from_metrics({metric: getattr(row, metric) for metric in METRIC_COLUMNS})
            if ok:
                first = row.elapsed_sec
                break
        out.append({"optimizer": optimizer, "testcase": testcase, "seed": seed, "time_to_feasible_sec": first})
    return out


def compute_time_to_gap(progress_rows: list[ProgressRow], best: dict[str, float], thresholds: list[float]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for (optimizer, testcase, seed), rows in group_progress(progress_rows).items():
        if testcase not in best:
            continue
        for threshold in thresholds:
            first = math.nan
            for row in rows:
                gap = normalized_gap(row.best_score, best[testcase])
                if gap <= threshold + 1e-12:
                    first = row.elapsed_sec
                    break
            out.append(
                {
                    "optimizer": optimizer,
                    "testcase": testcase,
                    "seed": seed,
                    "threshold": threshold,
                    "time_to_gap_sec": first,
                }
            )
    return out


def compute_progress_binned(progress_rows: list[ProgressRow], bins: int) -> list[dict[str, Any]]:
    grouped_tc_opt: dict[tuple[str, str], list[ProgressRow]] = defaultdict(list)
    for row in progress_rows:
        grouped_tc_opt[(row.optimizer, row.testcase)].append(row)
    out: list[dict[str, Any]] = []
    for (optimizer, testcase), rows in grouped_tc_opt.items():
        max_time = max((row.elapsed_sec for row in rows if not math.isnan(row.elapsed_sec)), default=math.nan)
        if math.isnan(max_time):
            continue
        width = max_time / max(1, bins)
        if width <= 0.0:
            width = 1.0
        by_bin_seed: dict[tuple[int, str], list[float]] = defaultdict(list)
        for row in rows:
            index = min(max(0, int(row.elapsed_sec / width)), bins - 1)
            by_bin_seed[(index, row.seed)].append(row.best_score)
        by_bin: dict[int, list[float]] = defaultdict(list)
        for (index, _seed), values in by_bin_seed.items():
            by_bin[index].append(values[-1])
        for index in sorted(by_bin):
            values = by_bin[index]
            out.append(
                {
                    "optimizer": optimizer,
                    "testcase": testcase,
                    "time_bin": index,
                    "time_bin_start_sec": index * width,
                    "time_bin_end_sec": (index + 1) * width,
                    "median_best_score": median(values),
                    "q25_best_score": quantile(values, 0.25),
                    "q75_best_score": quantile(values, 0.75),
                    "num_seed_traces": len(values),
                }
            )
    return out


def common_horizon(
    progress_rows: list[ProgressRow],
    optimizers: list[str] | None = None,
    max_elapsed_sec: float | None = None,
) -> tuple[float, list[str]]:
    selected = set(optimizers or [])
    by_optimizer: dict[str, list[float]] = defaultdict(list)
    for row in progress_rows:
        if selected and row.optimizer not in selected:
            continue
        if not math.isnan(row.elapsed_sec):
            by_optimizer[row.optimizer].append(row.elapsed_sec)
    present = ordered_optimizers(by_optimizer.keys())
    if not present:
        return math.nan, []
    horizon = min(max(values) for values in by_optimizer.values())
    if max_elapsed_sec is not None:
        horizon = min(horizon, max_elapsed_sec)
    return horizon, present


def compute_progress_all_testcases_average(
    progress_rows: list[ProgressRow],
    best: dict[str, float],
    bins: int,
    selected_optimizers: list[str] | None = None,
    figure_group: str = "all",
    max_elapsed_sec: float | None = None,
) -> list[dict[str, Any]]:
    horizon, present_optimizers = common_horizon(progress_rows, selected_optimizers, max_elapsed_sec)
    if math.isnan(horizon):
        return []
    by_optimizer: dict[str, list[ProgressRow]] = defaultdict(list)
    for row in progress_rows:
        if row.testcase in best and row.optimizer in present_optimizers and row.elapsed_sec <= horizon + 1e-12:
            by_optimizer[row.optimizer].append(row)
    out: list[dict[str, Any]] = []
    for optimizer, rows in by_optimizer.items():
        width = horizon / max(1, bins)
        if width <= 0.0:
            width = 1.0
        by_bin_testcase_seed: dict[tuple[int, str, str], list[float]] = defaultdict(list)
        for row in rows:
            index = min(max(0, int(row.elapsed_sec / width)), bins - 1)
            by_bin_testcase_seed[(index, row.testcase, row.seed)].append(
                normalized_gap(row.best_score, best[row.testcase])
            )
        by_bin_testcase: dict[tuple[int, str], list[float]] = defaultdict(list)
        seed_counts: dict[int, int] = defaultdict(int)
        for (index, testcase, _seed), values in by_bin_testcase_seed.items():
            by_bin_testcase[(index, testcase)].append(values[-1])
            seed_counts[index] += 1
        by_bin: dict[int, list[float]] = defaultdict(list)
        for (index, _testcase), values in by_bin_testcase.items():
            by_bin[index].append(median(values))
        for index in sorted(by_bin):
            testcase_medians = by_bin[index]
            out.append(
                {
                    "figure_group": figure_group,
                    "optimizer": optimizer,
                    "time_bin": index,
                    "time_bin_start_sec": index * width,
                    "time_bin_end_sec": (index + 1) * width,
                    "mean_normalized_gap": mean(testcase_medians),
                    "median_normalized_gap": median(testcase_medians),
                    "q25_normalized_gap": quantile(testcase_medians, 0.25),
                    "q75_normalized_gap": quantile(testcase_medians, 0.75),
                    "num_testcases": len(testcase_medians),
                    "num_seed_traces": seed_counts[index],
                }
            )
    return out


def compute_timing_all_testcases_average(
    progress_rows: list[ProgressRow],
    bins: int,
    selected_optimizers: list[str] | None = None,
    figure_group: str = "all",
    max_elapsed_sec: float | None = None,
) -> list[dict[str, Any]]:
    horizon, present_optimizers = common_horizon(progress_rows, selected_optimizers, max_elapsed_sec)
    if math.isnan(horizon):
        return []
    out: list[dict[str, Any]] = []
    for metric in METRIC_COLUMNS:
        by_optimizer: dict[str, list[ProgressRow]] = defaultdict(list)
        for row in progress_rows:
            if row.optimizer in present_optimizers and row.elapsed_sec <= horizon + 1e-12 and not math.isnan(getattr(row, metric)):
                by_optimizer[row.optimizer].append(row)
        for optimizer, rows in by_optimizer.items():
            averaged = average_metric_rows(
                optimizer, rows, metric, bins, "mean_metric", "median_metric", "q25_metric", "q75_metric", horizon
            )
            for item in averaged:
                item["figure_group"] = figure_group
                item["metric"] = metric
            out.extend(averaged)
    return out


def compute_normalized_objective_progress(
    progress_rows: list[ProgressRow],
    bins: int,
    selected_optimizers: list[str] | None = None,
    figure_group: str = "all",
    max_elapsed_sec: float | None = None,
) -> list[dict[str, Any]]:
    horizon, present_optimizers = common_horizon(progress_rows, selected_optimizers, max_elapsed_sec)
    if math.isnan(horizon):
        return []
    grouped = group_progress(progress_rows)
    initial: dict[tuple[str, str, str], ProgressRow] = {}
    for key, rows in grouped.items():
        if rows:
            initial[key] = rows[0]

    expanded: list[dict[str, Any]] = []
    for key, rows in grouped.items():
        optimizer, _testcase, _seed = key
        if optimizer not in present_optimizers:
            continue
        start = initial[key]
        for row in rows:
            if row.elapsed_sec > horizon + 1e-12:
                continue
            setup_values = [
                normalized_higher_is_better(getattr(row, metric), getattr(start, metric))
                for metric in ["tns_ss", "wns_ss"]
            ]
            hold_values = [
                normalized_higher_is_better(getattr(row, metric), getattr(start, metric))
                for metric in ["tns_ff", "wns_ff"]
            ]
            objectives = {
                "setup_slack": mean(setup_values),
                "hold_slack": mean(hold_values),
                "area": normalized_lower_is_better(row.area, start.area),
            }
            for objective, value in objectives.items():
                if not math.isnan(value):
                    expanded.append(
                        {
                            "optimizer": row.optimizer,
                            "testcase": row.testcase,
                            "seed": row.seed,
                            "elapsed_sec": row.elapsed_sec,
                            "objective": objective,
                            "value": value,
                        }
                    )

    out: list[dict[str, Any]] = []
    for objective in ["setup_slack", "hold_slack", "area"]:
        by_optimizer: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for row in expanded:
            if row["objective"] == objective:
                by_optimizer[row["optimizer"]].append(row)
        for optimizer, rows in by_optimizer.items():
            averaged = average_dict_metric_rows(optimizer, rows, objective, bins, horizon)
            for item in averaged:
                item["figure_group"] = figure_group
            out.extend(averaged)
    return out


def normalized_higher_is_better(value: float, initial: float) -> float:
    if math.isnan(value) or math.isnan(initial):
        return math.nan
    return (value - initial) / max(1.0, abs(initial))


def normalized_lower_is_better(value: float, initial: float) -> float:
    if math.isnan(value) or math.isnan(initial):
        return math.nan
    return (initial - value) / max(1.0, abs(initial))


def average_metric_rows(
    optimizer: str,
    rows: list[ProgressRow],
    metric: str,
    bins: int,
    mean_column: str,
    median_column: str,
    q25_column: str,
    q75_column: str,
    horizon: float | None = None,
) -> list[dict[str, Any]]:
    max_time = horizon if horizon is not None else max((row.elapsed_sec for row in rows if not math.isnan(row.elapsed_sec)), default=math.nan)
    if math.isnan(max_time):
        return []
    width = max_time / max(1, bins)
    if width <= 0.0:
        width = 1.0
    by_bin_testcase_seed: dict[tuple[int, str, str], list[float]] = defaultdict(list)
    for row in rows:
        value = getattr(row, metric)
        if math.isnan(value):
            continue
        index = min(max(0, int(row.elapsed_sec / width)), bins - 1)
        by_bin_testcase_seed[(index, row.testcase, row.seed)].append(value)
    return aggregate_bin_testcase_seed(
        optimizer, by_bin_testcase_seed, width, mean_column, median_column, q25_column, q75_column
    )


def average_dict_metric_rows(
    optimizer: str, rows: list[dict[str, Any]], objective: str, bins: int, horizon: float | None = None
) -> list[dict[str, Any]]:
    max_time = horizon if horizon is not None else max((float(row["elapsed_sec"]) for row in rows if not math.isnan(float(row["elapsed_sec"]))), default=math.nan)
    if math.isnan(max_time):
        return []
    width = max_time / max(1, bins)
    if width <= 0.0:
        width = 1.0
    by_bin_testcase_seed: dict[tuple[int, str, str], list[float]] = defaultdict(list)
    for row in rows:
        index = min(max(0, int(float(row["elapsed_sec"]) / width)), bins - 1)
        by_bin_testcase_seed[(index, str(row["testcase"]), str(row["seed"]))].append(float(row["value"]))
    aggregated = aggregate_bin_testcase_seed(
        optimizer,
        by_bin_testcase_seed,
        width,
        "mean_normalized_optimization",
        "median_normalized_optimization",
        "q25_normalized_optimization",
        "q75_normalized_optimization",
    )
    for row in aggregated:
        row["objective"] = objective
    return aggregated


def aggregate_bin_testcase_seed(
    optimizer: str,
    by_bin_testcase_seed: dict[tuple[int, str, str], list[float]],
    width: float,
    mean_column: str,
    median_column: str,
    q25_column: str,
    q75_column: str,
) -> list[dict[str, Any]]:
    by_bin_testcase: dict[tuple[int, str], list[float]] = defaultdict(list)
    seed_counts: dict[int, int] = defaultdict(int)
    for (index, testcase, _seed), values in by_bin_testcase_seed.items():
        by_bin_testcase[(index, testcase)].append(values[-1])
        seed_counts[index] += 1
    by_bin: dict[int, list[float]] = defaultdict(list)
    for (index, _testcase), values in by_bin_testcase.items():
        by_bin[index].append(median(values))
    out: list[dict[str, Any]] = []
    for index in sorted(by_bin):
        testcase_medians = by_bin[index]
        out.append(
            {
                "optimizer": optimizer,
                "time_bin": index,
                "time_bin_start_sec": index * width,
                "time_bin_end_sec": (index + 1) * width,
                mean_column: mean(testcase_medians),
                median_column: median(testcase_medians),
                q25_column: quantile(testcase_medians, 0.25),
                q75_column: quantile(testcase_medians, 0.75),
                "num_testcases": len(testcase_medians),
                "num_seed_traces": seed_counts[index],
            }
        )
    return out


def median_by_optimizer_testcase(rows: list[dict[str, Any]]) -> dict[tuple[str, str], dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[(row["optimizer_alias"], row["testcase"])].append(row)
    out: dict[tuple[str, str], dict[str, Any]] = {}
    for key, items in grouped.items():
        out[key] = {
            "final_score": median(float(item["final_score"]) for item in items),
            "normalized_gap": median(float(item["normalized_gap"]) for item in items),
            "feasible_rate": mean(
                1.0 if item["feasible"] == "true" else 0.0
                for item in items
                if item["feasible"] in {"true", "false"}
            ),
        }
    return out


def compute_win_tie_loss(rows: list[dict[str, Any]], baseline: str) -> list[dict[str, Any]]:
    med = median_by_optimizer_testcase(rows)
    testcases = sorted({row["testcase"] for row in rows})
    out: list[dict[str, Any]] = []
    for optimizer in ordered_optimizers({row["optimizer_alias"] for row in rows}):
        if optimizer == baseline:
            continue
        wins = ties = losses = total = 0
        for testcase in testcases:
            mine = med.get((optimizer, testcase))
            base = med.get((baseline, testcase))
            if mine is None or base is None:
                continue
            total += 1
            my_feasible = mine["feasible_rate"]
            base_feasible = base["feasible_rate"]
            if not math.isnan(my_feasible) and not math.isnan(base_feasible):
                if my_feasible > 0.5 and base_feasible <= 0.5:
                    wins += 1
                    continue
                if my_feasible <= 0.5 and base_feasible > 0.5:
                    losses += 1
                    continue
            diff = mine["normalized_gap"] - base["normalized_gap"]
            if abs(diff) <= TIE_EPSILON:
                ties += 1
            elif diff < 0.0:
                wins += 1
            else:
                losses += 1
        out.append(
            {
                "optimizer": optimizer,
                "baseline": baseline,
                "wins": wins,
                "ties": ties,
                "losses": losses,
                "total_compared": total,
            }
        )
    return out


def compute_optimizer_summary(
    final_rows: list[dict[str, Any]],
    time_to_feasible: list[dict[str, Any]],
    time_to_gap: list[dict[str, Any]],
    thresholds: list[float],
) -> list[dict[str, Any]]:
    by_opt: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in final_rows:
        by_opt[row["optimizer_alias"]].append(row)
    best_tc = {
        (row["testcase"], row["optimizer_alias"])
        for row in final_rows
        if abs(float(row["normalized_gap"])) <= 1e-12
    }
    ttf_by_opt: dict[str, list[float]] = defaultdict(list)
    for row in time_to_feasible:
        value = float(row["time_to_feasible_sec"])
        if not math.isnan(value):
            ttf_by_opt[row["optimizer"]].append(value)
    ttg_by_opt_thr: dict[tuple[str, float], list[float]] = defaultdict(list)
    for row in time_to_gap:
        value = float(row["time_to_gap_sec"])
        if not math.isnan(value):
            ttg_by_opt_thr[(row["optimizer"], float(row["threshold"]))].append(value)
    out: list[dict[str, Any]] = []
    for optimizer in ordered_optimizers(by_opt.keys()):
        rows = by_opt[optimizer]
        gaps = [float(row["normalized_gap"]) for row in rows]
        feasible_values = [
            1.0 if row["feasible"] == "true" else 0.0 for row in rows if row["feasible"] in {"true", "false"}
        ]
        area_feasible = [
            float(row["area"])
            for row in rows
            if row["feasible"] == "true" and not math.isnan(float(row["area"]))
        ]
        item: dict[str, Any] = {
            "optimizer_id": OPT_ID.get(optimizer, ""),
            "optimizer_alias": optimizer,
            "role": OPT_ROLE.get(optimizer, ""),
            "num_runs": len(rows),
            "num_testcases": len({row["testcase"] for row in rows}),
            "feasible_rate": mean(feasible_values),
            "best_count": len({tc for tc, opt in best_tc if opt == optimizer}),
            "median_normalized_gap": median(gaps),
            "mean_normalized_gap": mean(gaps),
            "q25_normalized_gap": quantile(gaps, 0.25),
            "q75_normalized_gap": quantile(gaps, 0.75),
            "median_area_feasible_only": median(area_feasible),
            "median_time_to_feasible": median(ttf_by_opt[optimizer]),
        }
        for threshold in thresholds:
            key = f"median_time_to_gap_{threshold_label(threshold)}"
            item[key] = median(ttg_by_opt_thr[(optimizer, threshold)])
        out.append(item)
    return out


def compute_gap_matrix(final_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[float]] = defaultdict(list)
    testcases = sorted({row["testcase"] for row in final_rows})
    optimizers = ordered_optimizers({row["optimizer_alias"] for row in final_rows})
    for row in final_rows:
        grouped[(row["testcase"], row["optimizer_alias"])].append(float(row["normalized_gap"]))
    matrix: list[dict[str, Any]] = []
    for testcase in testcases:
        item: dict[str, Any] = {"testcase": testcase}
        for optimizer in optimizers:
            item[optimizer] = median(grouped[(testcase, optimizer)])
        matrix.append(item)
    return matrix


def ordered_optimizers(optimizers: Iterable[str]) -> list[str]:
    found = set(optimizers)
    ordered = [optimizer for optimizer in OPT_ORDER if optimizer in found]
    ordered.extend(sorted(found - set(ordered)))
    return ordered


def optimizer_label(optimizer: str) -> str:
    return optimizer


def threshold_label(threshold: float) -> str:
    return f"{int(round(threshold * 100))}pct"


def write_tsv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({column: fmt(row.get(column, "")) for column in columns})


def write_tables(
    out_dir: Path,
    data: ReportData,
    final_rows: list[dict[str, Any]],
    optimizer_summary: list[dict[str, Any]],
    gap_matrix: list[dict[str, Any]],
    win_tie_loss: list[dict[str, Any]],
    time_to_feasible: list[dict[str, Any]],
    time_to_gap: list[dict[str, Any]],
    progress_binned: list[dict[str, Any]],
    progress_all_testcases_average: list[dict[str, Any]],
    timing_all_testcases_average: list[dict[str, Any]],
    normalized_objective_progress: list[dict[str, Any]],
    baseline: str,
    thresholds: list[float],
) -> list[Path]:
    table_dir = out_dir / "tables"
    paths: list[Path] = []
    manifest_cols = ["input_file", "file_type", "optimizer", "testcase", "seed", "rows", "sha256", "parse_status", "warning"]
    manifest_rows = [row.__dict__ for row in data.manifest]
    path = table_dir / "run_manifest.tsv"
    write_tsv(path, manifest_rows, manifest_cols)
    paths.append(path)

    final_cols = [
        "optimizer_id",
        "optimizer_alias",
        "testcase",
        "seed",
        "final_score",
        "best_known_score_for_testcase",
        "normalized_gap",
        "feasible",
        "area",
        "runtime_sec",
        "tns_ss",
        "wns_ss",
        "tns_ff",
        "wns_ff",
    ]
    path = table_dir / "final_runs_normalized.tsv"
    write_tsv(path, final_rows, final_cols)
    paths.append(path)

    summary_cols = [
        "optimizer_id",
        "optimizer_alias",
        "role",
        "num_runs",
        "num_testcases",
        "feasible_rate",
        "best_count",
        "median_normalized_gap",
        "mean_normalized_gap",
        "q25_normalized_gap",
        "q75_normalized_gap",
        "median_area_feasible_only",
        "median_time_to_feasible",
        *[f"median_time_to_gap_{threshold_label(threshold)}" for threshold in thresholds],
    ]
    path = table_dir / "optimizer_summary.tsv"
    write_tsv(path, optimizer_summary, summary_cols)
    paths.append(path)

    optimizers = ordered_optimizers({row["optimizer_alias"] for row in final_rows})
    path = table_dir / "per_testcase_gap_matrix.tsv"
    write_tsv(path, gap_matrix, ["testcase", *optimizers])
    paths.append(path)

    path = table_dir / f"win_tie_loss_vs_{baseline}.tsv"
    write_tsv(path, win_tie_loss, ["optimizer", "baseline", "wins", "ties", "losses", "total_compared"])
    paths.append(path)

    if time_to_feasible:
        path = table_dir / "time_to_feasible.tsv"
        write_tsv(path, time_to_feasible, ["optimizer", "testcase", "seed", "time_to_feasible_sec"])
        paths.append(path)
    if time_to_gap:
        path = table_dir / "time_to_gap_threshold.tsv"
        write_tsv(path, time_to_gap, ["optimizer", "testcase", "seed", "threshold", "time_to_gap_sec"])
        paths.append(path)
    if progress_binned:
        path = table_dir / "progress_binned_median_iqr.tsv"
        write_tsv(
            path,
            progress_binned,
            [
                "optimizer",
                "testcase",
                "time_bin",
                "time_bin_start_sec",
                "time_bin_end_sec",
                "median_best_score",
                "q25_best_score",
                "q75_best_score",
                "num_seed_traces",
            ],
        )
        paths.append(path)
    if progress_all_testcases_average:
        path = table_dir / "progress_all_testcases_average.tsv"
        write_tsv(
            path,
            progress_all_testcases_average,
            [
                "figure_group",
                "optimizer",
                "time_bin",
                "time_bin_start_sec",
                "time_bin_end_sec",
                "mean_normalized_gap",
                "median_normalized_gap",
                "q25_normalized_gap",
                "q75_normalized_gap",
                "num_testcases",
                "num_seed_traces",
            ],
        )
        paths.append(path)
    if timing_all_testcases_average:
        path = table_dir / "timing_all_testcases_average.tsv"
        write_tsv(
            path,
            timing_all_testcases_average,
            [
                "figure_group",
                "optimizer",
                "metric",
                "time_bin",
                "time_bin_start_sec",
                "time_bin_end_sec",
                "mean_metric",
                "median_metric",
                "q25_metric",
                "q75_metric",
                "num_testcases",
                "num_seed_traces",
            ],
        )
        paths.append(path)
    if normalized_objective_progress:
        path = table_dir / "normalized_objective_progress.tsv"
        write_tsv(
            path,
            normalized_objective_progress,
            [
                "figure_group",
                "optimizer",
                "objective",
                "time_bin",
                "time_bin_start_sec",
                "time_bin_end_sec",
                "mean_normalized_optimization",
                "median_normalized_optimization",
                "q25_normalized_optimization",
                "q75_normalized_optimization",
                "num_testcases",
                "num_seed_traces",
            ],
        )
        paths.append(path)
    return paths


def setup_matplotlib() -> Any:
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/cadd0040-matplotlib")
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update(
        {
            "font.size": 10,
            "axes.titlesize": 12,
            "axes.labelsize": 10,
            "legend.fontsize": 8,
            "xtick.labelsize": 8,
            "ytick.labelsize": 8,
            "figure.dpi": 120,
            "savefig.dpi": 240,
        }
    )
    return plt


def save_figure(fig: Any, figures_dir: Path, name: str) -> list[Path]:
    figures_dir.mkdir(parents=True, exist_ok=True)
    png = figures_dir / f"{name}.png"
    pdf = figures_dir / f"{name}.pdf"
    fig.tight_layout()
    fig.savefig(png, dpi=240)
    fig.savefig(pdf)
    import matplotlib.pyplot as plt

    plt.close(fig)
    return [png, pdf]


def plot_bar(
    plt: Any,
    figures_dir: Path,
    name: str,
    title: str,
    ylabel: str,
    optimizers: list[str],
    values: list[float],
    annotations: list[str],
) -> list[Path]:
    labels = [optimizer_label(opt) for opt in optimizers]
    fig, ax = plt.subplots(figsize=(8.5, 4.8))
    ax.bar(labels, values, color="#4C78A8")
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    ax.set_xlabel("Optimizer")
    ax.grid(axis="y", alpha=0.3, linewidth=0.6)
    ax.tick_params(axis="x", labelrotation=25)
    for index, (value, annotation) in enumerate(zip(values, annotations)):
        if not math.isnan(value):
            ax.text(index, value, annotation, ha="center", va="bottom", fontsize=8)
    return save_figure(fig, figures_dir, name)


def generate_figures(
    out_dir: Path,
    final_rows: list[dict[str, Any]],
    optimizer_summary: list[dict[str, Any]],
    gap_matrix: list[dict[str, Any]],
    win_tie_loss: list[dict[str, Any]],
    time_to_feasible: list[dict[str, Any]],
    time_to_gap: list[dict[str, Any]],
    progress_binned: list[dict[str, Any]],
    progress_all_testcases_average: list[dict[str, Any]],
    timing_all_testcases_average: list[dict[str, Any]],
    normalized_objective_progress: list[dict[str, Any]],
    data: ReportData,
    thresholds: list[float],
    baseline: str,
) -> list[Path]:
    plt = setup_matplotlib()
    figures_dir = out_dir / "figures"
    paths: list[Path] = []
    summary_by_opt = {row["optimizer_alias"]: row for row in optimizer_summary}
    optimizers = ordered_optimizers(summary_by_opt.keys())

    values = [float(summary_by_opt[opt]["feasible_rate"]) for opt in optimizers]
    annotations = [
        ("NA" if math.isnan(value) else f"{100 * value:.0f}%\n(n={summary_by_opt[opt]['num_runs']})")
        for opt, value in zip(optimizers, values)
    ]
    paths.extend(
        plot_bar(
            plt,
            figures_dir,
            "fig1_feasible_rate_by_optimizer",
            "Feasible Rate by Optimizer",
            "Feasible runs / runs with timing metrics",
            optimizers,
            [0.0 if math.isnan(v) else v for v in values],
            annotations,
        )
    )

    values = [float(summary_by_opt[opt]["median_normalized_gap"]) for opt in optimizers]
    annotations = ["NA" if math.isnan(value) else f"{value:.3g}" for value in values]
    paths.extend(
        plot_bar(
            plt,
            figures_dir,
            "fig2_median_normalized_gap_by_optimizer",
            "Median Normalized Score Gap by Optimizer",
            "Median normalized gap (lower is better)",
            optimizers,
            [0.0 if math.isnan(v) else v for v in values],
            annotations,
        )
    )

    values = [float(summary_by_opt[opt]["best_count"]) for opt in optimizers]
    annotations = [f"{int(value)}" for value in values]
    paths.extend(
        plot_bar(
            plt,
            figures_dir,
            "fig3_best_count_by_optimizer",
            "Best-Known Testcase Count by Optimizer",
            "Testcases where median score reaches best-known",
            optimizers,
            values,
            annotations,
        )
    )

    paths.extend(plot_heatmap(plt, figures_dir, gap_matrix, optimizers))
    paths.extend(plot_win_tie_loss(plt, figures_dir, win_tie_loss, baseline))
    if time_to_feasible:
        paths.extend(plot_cactus(plt, figures_dir, "fig6_cactus_time_to_feasible", time_to_feasible, "time_to_feasible_sec", "Time to Feasible Timing"))
    if time_to_gap:
        for threshold in thresholds:
            rows = [row for row in time_to_gap if abs(float(row["threshold"]) - threshold) <= 1e-12]
            paths.extend(
                plot_cactus(
                    plt,
                    figures_dir,
                    f"fig7_cactus_time_to_gap_{threshold_label(threshold)}",
                    rows,
                    "time_to_gap_sec",
                    f"Time to Within {threshold:.0%} of Best-Known Score",
                )
            )
    if progress_binned:
        selected = select_testcases(data, final_rows, time_to_feasible)
        paths.extend(plot_selected_best_progress(plt, figures_dir, progress_binned, selected))
        if has_timing_progress(data.progress_rows):
            paths.extend(plot_selected_timing_progress(plt, figures_dir, data.progress_rows, selected))
        if has_area_progress(data.progress_rows) and time_to_feasible:
            paths.extend(plot_area_after_feasible(plt, figures_dir, data.progress_rows, time_to_feasible, selected))
    if progress_all_testcases_average:
        paths.extend(
            plot_all_testcases_average_best_progress(
                plt,
                figures_dir,
                rows_for_group(progress_all_testcases_average, "all"),
                "fig11_all_testcases_average_best_score_progress",
                "Average Best Score Progress Across All Testcases",
            )
        )
    if timing_all_testcases_average:
        paths.extend(plot_timing_all_testcases_average(plt, figures_dir, rows_for_group(timing_all_testcases_average, "all")))
    if normalized_objective_progress:
        paths.extend(plot_normalized_objective_progress(plt, figures_dir, rows_for_group(normalized_objective_progress, "all")))
    paths.extend(
        plot_group_average_figures(
            plt, figures_dir, progress_all_testcases_average, normalized_objective_progress
        )
    )
    plt.close("all")
    return paths


def rows_for_group(rows: list[dict[str, Any]], figure_group: str) -> list[dict[str, Any]]:
    return [row for row in rows if row.get("figure_group", "all") == figure_group]


def plot_heatmap(plt: Any, figures_dir: Path, matrix: list[dict[str, Any]], optimizers: list[str]) -> list[Path]:
    testcases = [row["testcase"] for row in matrix]
    values = [[parse_float(row.get(opt)) for opt in optimizers] for row in matrix]
    fig, ax = plt.subplots(figsize=(max(7.0, len(optimizers) * 0.75), max(4.5, len(testcases) * 0.35)))
    image = ax.imshow(values, aspect="auto", cmap="viridis_r")
    ax.set_title("Median Normalized Gap by Testcase and Optimizer")
    ax.set_xticks(range(len(optimizers)), [optimizer_label(opt) for opt in optimizers], rotation=30, ha="right")
    ax.set_yticks(range(len(testcases)), testcases)
    ax.set_xlabel("Optimizer")
    ax.set_ylabel("Testcase")
    cbar = fig.colorbar(image, ax=ax)
    cbar.set_label("Median normalized gap (lower is better)")
    if len(testcases) * len(optimizers) <= 120:
        for y, row in enumerate(values):
            for x, value in enumerate(row):
                if not math.isnan(value):
                    ax.text(x, y, f"{value:.2g}", ha="center", va="center", fontsize=7, color="white")
    return save_figure(fig, figures_dir, "fig4_normalized_gap_heatmap")


def plot_win_tie_loss(plt: Any, figures_dir: Path, rows: list[dict[str, Any]], baseline: str) -> list[Path]:
    labels = [optimizer_label(row["optimizer"]) for row in rows]
    wins = [int(row["wins"]) for row in rows]
    ties = [int(row["ties"]) for row in rows]
    losses = [int(row["losses"]) for row in rows]
    y = list(range(len(rows)))
    fig, ax = plt.subplots(figsize=(8.5, max(3.5, len(rows) * 0.45)))
    ax.barh(y, wins, color="#59A14F", label="wins")
    ax.barh(y, ties, left=wins, color="#BAB0AC", label="ties")
    left_loss = [w + t for w, t in zip(wins, ties)]
    ax.barh(y, losses, left=left_loss, color="#E15759", label="losses")
    ax.set_yticks(y, labels)
    ax.set_xlabel("Testcase comparisons")
    ax.set_title(f"Win/Tie/Loss vs Baseline ({baseline})")
    ax.legend()
    ax.grid(axis="x", alpha=0.3, linewidth=0.6)
    return save_figure(fig, figures_dir, "fig5_win_tie_loss_vs_baseline")


def plot_cactus(plt: Any, figures_dir: Path, name: str, rows: list[dict[str, Any]], value_key: str, title: str) -> list[Path]:
    by_opt: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        value = parse_float(row.get(value_key))
        if not math.isnan(value):
            by_opt[row["optimizer"]].append(value)
    fig, ax = plt.subplots(figsize=(8.5, 4.8))
    for optimizer in ordered_optimizers(by_opt.keys()):
        values = sorted(by_opt[optimizer])
        ax.step(values, list(range(1, len(values) + 1)), where="post", label=optimizer_label(optimizer))
    ax.set_title(title)
    ax.set_xlabel("Elapsed seconds")
    ax.set_ylabel("Cumulative runs reached")
    ax.grid(True, alpha=0.3, linewidth=0.6)
    handles, labels = ax.get_legend_handles_labels()
    if labels:
        ax.legend(handles, labels, ncol=2)
    return save_figure(fig, figures_dir, name)


def select_testcases(data: ReportData, final_rows: list[dict[str, Any]], time_to_feasible: list[dict[str, Any]]) -> list[str]:
    if data.selected_testcases:
        return data.selected_testcases
    gap_by_tc: dict[str, list[float]] = defaultdict(list)
    for row in final_rows:
        gap_by_tc[row["testcase"]].append(float(row["normalized_gap"]))
    scores = [(testcase, median(values)) for testcase, values in gap_by_tc.items()]
    scores.sort(key=lambda item: (math.inf if math.isnan(item[1]) else item[1], item[0]))
    if not scores:
        return []
    indices = sorted({0, len(scores) // 2, len(scores) - 1})
    data.selected_testcases = [scores[index][0] for index in indices]
    return data.selected_testcases


def plot_selected_best_progress(
    plt: Any, figures_dir: Path, rows: list[dict[str, Any]], selected: list[str]
) -> list[Path]:
    paths: list[Path] = []
    for testcase in selected:
        fig, ax = plt.subplots(figsize=(8.5, 4.8))
        for optimizer in ordered_optimizers({row["optimizer"] for row in rows if row["testcase"] == testcase}):
            opt_rows = sorted(
                [row for row in rows if row["testcase"] == testcase and row["optimizer"] == optimizer],
                key=lambda row: float(row["time_bin_start_sec"]),
            )
            xs = [0.5 * (float(row["time_bin_start_sec"]) + float(row["time_bin_end_sec"])) for row in opt_rows]
            med = [float(row["median_best_score"]) for row in opt_rows]
            q25 = [float(row["q25_best_score"]) for row in opt_rows]
            q75 = [float(row["q75_best_score"]) for row in opt_rows]
            line = ax.plot(xs, med, label=optimizer_label(optimizer), linewidth=1.8)[0]
            ax.fill_between(xs, q25, q75, color=line.get_color(), alpha=0.16, linewidth=0)
        ax.set_title(f"Best Score Progress: {testcase}")
        ax.set_xlabel("Elapsed seconds")
        ax.set_ylabel("Best score")
        ax.grid(True, alpha=0.3, linewidth=0.6)
        ax.legend(ncol=2)
        paths.extend(save_figure(fig, figures_dir, f"fig8_selected_testcase_best_score_progress_{testcase}"))
    return paths


def plot_all_testcases_average_best_progress(
    plt: Any, figures_dir: Path, rows: list[dict[str, Any]], filename: str, title: str
) -> list[Path]:
    if not rows:
        return []
    fig, ax = plt.subplots(figsize=(8.5, 4.8))
    for optimizer in ordered_optimizers({row["optimizer"] for row in rows}):
        opt_rows = sorted(
            [row for row in rows if row["optimizer"] == optimizer],
            key=lambda row: float(row["time_bin_start_sec"]),
        )
        xs = [0.5 * (float(row["time_bin_start_sec"]) + float(row["time_bin_end_sec"])) for row in opt_rows]
        mean_gap = [float(row["mean_normalized_gap"]) for row in opt_rows]
        q25 = [float(row["q25_normalized_gap"]) for row in opt_rows]
        q75 = [float(row["q75_normalized_gap"]) for row in opt_rows]
        line = ax.plot(xs, mean_gap, label=optimizer_label(optimizer), linewidth=1.8)[0]
        ax.fill_between(xs, q25, q75, color=line.get_color(), alpha=0.16, linewidth=0)
    ax.set_title(title)
    ax.set_xlabel("Elapsed seconds")
    ax.set_ylabel("Average normalized best-score gap (lower is better)")
    ax.grid(True, alpha=0.3, linewidth=0.6)
    ax.legend(ncol=2)
    return save_figure(fig, figures_dir, filename)


def plot_timing_all_testcases_average(
    plt: Any, figures_dir: Path, rows: list[dict[str, Any]]
) -> list[Path]:
    if not rows:
        return []
    fig, axes = plt.subplots(2, 2, figsize=(10, 7), sharex=True)
    for ax, metric in zip(axes.flat, METRIC_COLUMNS):
        metric_rows = [row for row in rows if row["metric"] == metric]
        for optimizer in ordered_optimizers({row["optimizer"] for row in metric_rows}):
            opt_rows = sorted(
                [row for row in metric_rows if row["optimizer"] == optimizer],
                key=lambda row: float(row["time_bin_start_sec"]),
            )
            xs = [0.5 * (float(row["time_bin_start_sec"]) + float(row["time_bin_end_sec"])) for row in opt_rows]
            mean_values = [float(row["mean_metric"]) for row in opt_rows]
            q25 = [float(row["q25_metric"]) for row in opt_rows]
            q75 = [float(row["q75_metric"]) for row in opt_rows]
            line = ax.plot(xs, mean_values, label=optimizer_label(optimizer), linewidth=1.5)[0]
            ax.fill_between(xs, q25, q75, color=line.get_color(), alpha=0.12, linewidth=0)
        ax.axhline(0.0, color="black", linewidth=0.8, alpha=0.6)
        ax.set_title(metric)
        ax.set_ylabel(f"Average {metric}")
        ax.grid(True, alpha=0.25, linewidth=0.5)
    axes[-1, 0].set_xlabel("Elapsed seconds")
    axes[-1, 1].set_xlabel("Elapsed seconds")
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=4)
    fig.suptitle("Average Timing Metric Progress Across All Testcases", y=1.02)
    return save_figure(fig, figures_dir, "fig12_all_testcases_average_timing_progress")


def plot_normalized_objective_progress(
    plt: Any, figures_dir: Path, rows: list[dict[str, Any]]
) -> list[Path]:
    specs = [
        ("setup_slack", "fig13_normalized_setup_slack_optimization_average", "Normalized Setup Slack Optimization"),
        ("hold_slack", "fig14_normalized_hold_slack_optimization_average", "Normalized Hold Slack Optimization"),
        ("area", "fig15_normalized_area_optimization_average", "Normalized Area Optimization"),
    ]
    paths: list[Path] = []
    for objective, filename, title in specs:
        paths.extend(
            plot_one_normalized_objective_progress(
                plt, figures_dir, rows, objective, filename, f"{title} Across All Testcases"
            )
        )
    return paths


def plot_one_normalized_objective_progress(
    plt: Any,
    figures_dir: Path,
    rows: list[dict[str, Any]],
    objective: str,
    filename: str,
    title: str,
) -> list[Path]:
    objective_rows = [row for row in rows if row["objective"] == objective]
    if not objective_rows:
        return []
    fig, ax = plt.subplots(figsize=(8.5, 4.8))
    for optimizer in ordered_optimizers({row["optimizer"] for row in objective_rows}):
        opt_rows = sorted(
            [row for row in objective_rows if row["optimizer"] == optimizer],
            key=lambda row: float(row["time_bin_start_sec"]),
        )
        xs = [0.5 * (float(row["time_bin_start_sec"]) + float(row["time_bin_end_sec"])) for row in opt_rows]
        mean_values = [float(row["mean_normalized_optimization"]) for row in opt_rows]
        q25 = [float(row["q25_normalized_optimization"]) for row in opt_rows]
        q75 = [float(row["q75_normalized_optimization"]) for row in opt_rows]
        line = ax.plot(xs, mean_values, label=optimizer_label(optimizer), linewidth=1.8)[0]
        ax.fill_between(xs, q25, q75, color=line.get_color(), alpha=0.16, linewidth=0)
    ax.axhline(0.0, color="black", linewidth=0.8, alpha=0.6)
    ax.set_title(title)
    ax.set_xlabel("Elapsed seconds")
    ax.set_ylabel("Average normalized optimization (higher is better)")
    ax.grid(True, alpha=0.3, linewidth=0.6)
    ax.legend(ncol=2)
    return save_figure(fig, figures_dir, filename)


def plot_group_average_figures(
    plt: Any,
    figures_dir: Path,
    best_rows: list[dict[str, Any]],
    objective_rows: list[dict[str, Any]],
) -> list[Path]:
    paths: list[Path] = []
    for figure_group, optimizers, filenames in GROUP_AVERAGE_SPECS:
        title_suffix = ", ".join(optimizers)
        group_best_rows = rows_for_group(best_rows, figure_group)
        paths.extend(
            plot_all_testcases_average_best_progress(
                plt,
                figures_dir,
                group_best_rows,
                filenames["best"],
                f"Average Best Score Progress Across All Testcases: {figure_group}",
            )
        )
        group_objective_rows = rows_for_group(objective_rows, figure_group)
        paths.extend(
            plot_one_normalized_objective_progress(
                plt,
                figures_dir,
                group_objective_rows,
                "setup_slack",
                filenames["setup"],
                f"Normalized Setup Slack Optimization: {figure_group}",
            )
        )
        paths.extend(
            plot_one_normalized_objective_progress(
                plt,
                figures_dir,
                group_objective_rows,
                "hold_slack",
                filenames["hold"],
                f"Normalized Hold Slack Optimization: {figure_group}",
            )
        )
        paths.extend(
            plot_one_normalized_objective_progress(
                plt,
                figures_dir,
                group_objective_rows,
                "area",
                filenames["area"],
                f"Normalized Area Optimization: {figure_group}",
            )
        )
    return paths


def has_timing_progress(rows: list[ProgressRow]) -> bool:
    return any(not math.isnan(getattr(row, metric)) for row in rows for metric in METRIC_COLUMNS)


def has_area_progress(rows: list[ProgressRow]) -> bool:
    return any(not math.isnan(row.area) for row in rows)


def binned_metric_for_testcase(rows: list[ProgressRow], testcase: str, metric: str, bins: int = 80) -> dict[str, list[dict[str, float]]]:
    by_opt: dict[str, list[ProgressRow]] = defaultdict(list)
    for row in rows:
        if row.testcase == testcase and not math.isnan(getattr(row, metric)):
            by_opt[row.optimizer].append(row)
    out: dict[str, list[dict[str, float]]] = {}
    for optimizer, opt_rows in by_opt.items():
        max_time = max(row.elapsed_sec for row in opt_rows)
        width = max_time / max(1, bins) if max_time > 0.0 else 1.0
        by_bin_seed: dict[tuple[int, str], list[float]] = defaultdict(list)
        for row in opt_rows:
            index = min(max(0, int(row.elapsed_sec / width)), bins - 1)
            by_bin_seed[(index, row.seed)].append(getattr(row, metric))
        by_bin: dict[int, list[float]] = defaultdict(list)
        for (index, _seed), values in by_bin_seed.items():
            by_bin[index].append(values[-1])
        out[optimizer] = [
            {
                "x": (index + 0.5) * width,
                "median": median(values),
                "q25": quantile(values, 0.25),
                "q75": quantile(values, 0.75),
            }
            for index, values in sorted(by_bin.items())
        ]
    return out


def plot_selected_timing_progress(plt: Any, figures_dir: Path, rows: list[ProgressRow], selected: list[str]) -> list[Path]:
    paths: list[Path] = []
    for testcase in selected:
        fig, axes = plt.subplots(2, 2, figsize=(10, 7), sharex=True)
        for ax, metric in zip(axes.flat, METRIC_COLUMNS):
            series = binned_metric_for_testcase(rows, testcase, metric)
            for optimizer in ordered_optimizers(series.keys()):
                points = series[optimizer]
                xs = [point["x"] for point in points]
                med = [point["median"] for point in points]
                q25 = [point["q25"] for point in points]
                q75 = [point["q75"] for point in points]
                line = ax.plot(xs, med, label=optimizer_label(optimizer), linewidth=1.4)[0]
                ax.fill_between(xs, q25, q75, color=line.get_color(), alpha=0.12, linewidth=0)
            ax.axhline(0.0, color="black", linewidth=0.8, alpha=0.6)
            ax.set_title(metric)
            ax.set_ylabel(metric)
            ax.grid(True, alpha=0.25, linewidth=0.5)
        axes[-1, 0].set_xlabel("Elapsed seconds")
        axes[-1, 1].set_xlabel("Elapsed seconds")
        handles, labels = axes[0, 0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="upper center", ncol=4)
        fig.suptitle(f"Timing Metric Progress: {testcase}", y=1.02)
        paths.extend(save_figure(fig, figures_dir, f"fig9_selected_testcase_timing_progress_{testcase}"))
    return paths


def plot_area_after_feasible(
    plt: Any, figures_dir: Path, progress_rows: list[ProgressRow], time_to_feasible: list[dict[str, Any]], selected: list[str]
) -> list[Path]:
    first: dict[tuple[str, str, str], float] = {
        (row["optimizer"], row["testcase"], row["seed"]): float(row["time_to_feasible_sec"])
        for row in time_to_feasible
        if not math.isnan(parse_float(row["time_to_feasible_sec"]))
    }
    filtered = [
        row
        for row in progress_rows
        if row.testcase in selected
        and not math.isnan(row.area)
        and (row.optimizer, row.testcase, row.seed) in first
        and row.elapsed_sec >= first[(row.optimizer, row.testcase, row.seed)]
    ]
    paths: list[Path] = []
    for testcase in selected:
        fig, ax = plt.subplots(figsize=(8.5, 4.8))
        for optimizer in ordered_optimizers({row.optimizer for row in filtered if row.testcase == testcase}):
            series = binned_metric_for_testcase(filtered, testcase, "area")
            points = series.get(optimizer, [])
            xs = [point["x"] for point in points]
            med = [point["median"] for point in points]
            q25 = [point["q25"] for point in points]
            q75 = [point["q75"] for point in points]
            if not xs:
                continue
            line = ax.plot(xs, med, label=optimizer_label(optimizer), linewidth=1.8)[0]
            ax.fill_between(xs, q25, q75, color=line.get_color(), alpha=0.16, linewidth=0)
        ax.set_title(f"Area After First Feasible Point: {testcase}")
        ax.set_xlabel("Elapsed seconds")
        ax.set_ylabel("Area")
        ax.grid(True, alpha=0.3, linewidth=0.6)
        handles, labels = ax.get_legend_handles_labels()
        if labels:
            ax.legend(handles, labels, ncol=2)
        paths.extend(save_figure(fig, figures_dir, f"fig10_area_after_feasible_{testcase}"))
    return paths


def write_markdown(
    out_dir: Path,
    data: ReportData,
    final_rows: list[dict[str, Any]],
    optimizer_summary: list[dict[str, Any]],
    figure_paths: list[Path],
    table_paths: list[Path],
    baseline: str,
    thresholds: list[float],
) -> None:
    derived = out_dir / "DERIVED_METRICS.md"
    derived.write_text(
        "\n".join(
            [
                "# Derived Metrics",
                "",
                "The project score is defined in `src/evaluation.cpp::score` and is treated as higher-is-better.",
                "`src/evaluation.hpp` documents timing metrics as `<= 0.0`; `evaluate()` accumulates only negative slack values.",
                "",
                "## Best-Known Score",
                "",
                "`best_known_score[testcase] = max(final_score)` over all OK optimizer/seed runs for that testcase.",
                "",
                "## Normalized Gap",
                "",
                "`normalized_gap = (best_known_score[testcase] - final_score) / max(1, abs(best_known_score[testcase]))`.",
                "Lower is better; zero means the run reached the best-known score in this run directory.",
                "",
                "## Feasibility",
                "",
                "Timing metrics are slack-like and non-positive in the C++ `Metrics` struct. A run is feasible iff every available "
                "`tns_ss`, `wns_ss`, `tns_ff`, and `wns_ff` value is `>= 0` within `1e-12` tolerance. If aggregate timing metrics "
                "are missing, feasibility is reported as `NA` for aggregate figures.",
                "",
                "## Time to Feasible",
                "",
                "For each progress trace, this is the first `elapsed_sec` where the timing feasibility rule above is true.",
                "",
                "## Time to Near-Best",
                "",
                "For each threshold, this is the first `elapsed_sec` where the progress-row `best_score` normalized gap is less than "
                "or equal to the threshold.",
                "",
                "## Progress Median/IQR",
                "",
                "Progress traces are binned by elapsed time using `--time-bins`. Within each optimizer/testcase/bin, each seed "
                "contributes one last observed best score. The table reports median, q25, q75, and contributing seed count.",
                "",
                "## All-Testcase Average Best Score Progress",
                "",
                "`fig11` uses `progress_all_testcases_average.tsv`. For each optimizer/time bin, each testcase contributes the "
                "median normalized best-score gap across available seeds, then the figure plots the mean across testcases with "
                "q25-q75 bands over testcase medians. This avoids averaging raw scores with different testcase scales.",
                "Average progress figures are truncated to the common elapsed-time horizon shared by the optimizers in that "
                "figure before binning, so plotted curves have equal-length comparable data.",
                f"Group comparison figures `fig16`-`fig31` additionally discard rows with `elapsed_sec > {GROUP_AVERAGE_MAX_ELAPSED_SEC:g}`.",
                "",
                "## All-Testcase Average Timing Progress",
                "",
                "`fig12` uses `timing_all_testcases_average.tsv`. For each timing metric and optimizer/time bin, each testcase "
                "contributes the median raw timing metric across available seeds, then the figure plots the mean across testcases "
                "with q25-q75 bands over testcase medians. The same common-horizon truncation rule is used.",
                "",
                "## Normalized Optimization Progress",
                "",
                "`fig13`, `fig14`, and `fig15` use `normalized_objective_progress.tsv`. For slack-like metrics where higher is "
                "better, normalized improvement is `(value - initial_value) / max(1, abs(initial_value))`. Setup slack is the "
                "mean of normalized `tns_ss` and `wns_ss`; hold slack is the mean of normalized `tns_ff` and `wns_ff`. For area, "
                "where lower is better, normalized improvement is `(initial_area - area) / max(1, abs(initial_area))`. Each "
                "testcase contributes the median normalized improvement across available seeds before the cross-testcase average "
                "is computed. The same common-horizon truncation rule is used.",
            ]
        )
        + "\n"
    )

    captions = ["# Report Figure Captions", ""]
    figure_names = sorted({path.stem for path in figure_paths})
    for name in figure_names:
        captions.extend(caption_for(name, baseline, thresholds))
    (out_dir / "REPORT_FIGURE_CAPTIONS.md").write_text("\n".join(captions) + "\n")

    optimizers = {row["optimizer_alias"] for row in final_rows}
    testcases = {row["testcase"] for row in final_rows}
    seeds = {row["seed"] for row in final_rows}
    best_gap = min(optimizer_summary, key=lambda row: math.inf if math.isnan(float(row["median_normalized_gap"])) else float(row["median_normalized_gap"]))
    lines = [
        "# Report Summary",
        "",
        f"- Optimizers detected: {len(optimizers)}",
        f"- Testcases detected: {len(testcases)}",
        f"- Seeds detected: {len(seeds)}",
        f"- Total OK final runs: {len(final_rows)}",
        f"- Progress trace rows: {len(data.progress_rows)}",
        f"- Figures generated: {len({path.stem for path in figure_paths})}",
        f"- Tables generated: {len(table_paths)}",
        "",
        "## Candidate Conclusions",
        "",
        f"- Optimizer `{best_gap['optimizer_alias']}` has the lowest median normalized gap under this run directory.",
    ]
    ttf_values = [
        row for row in optimizer_summary if not math.isnan(parse_float(row.get("median_time_to_feasible")))
    ]
    if ttf_values:
        fastest = min(ttf_values, key=lambda row: float(row["median_time_to_feasible"]))
        lines.append(
            f"- Optimizer `{fastest['optimizer_alias']}` reaches feasibility fastest among runs with available progress traces."
        )
    lines.extend(["", "## Warnings", ""])
    if data.warnings:
        lines.extend(f"- {warning}" for warning in data.warnings)
    else:
        lines.append("- None.")
    (out_dir / "REPORT_SUMMARY.md").write_text("\n".join(lines) + "\n")


def caption_for(name: str, baseline: str, thresholds: list[float]) -> list[str]:
    table = "tables/optimizer_summary.tsv"
    text = {
        "fig1_feasible_rate_by_optimizer": "Measures feasible run rate using the explicit timing feasibility rule.",
        "fig2_median_normalized_gap_by_optimizer": "Measures median normalized score gap; lower is better.",
        "fig3_best_count_by_optimizer": "Counts testcases where optimizer median final score reaches the best-known testcase score.",
        "fig4_normalized_gap_heatmap": "Shows testcase-by-optimizer median normalized gap matrix.",
        "fig5_win_tie_loss_vs_baseline": f"Compares per-testcase median optimizer results against baseline `{baseline}`.",
        "fig6_cactus_time_to_feasible": "Shows cumulative runs that reached feasible timing over elapsed time.",
    }
    if name.startswith("fig7_"):
        desc = "Shows cumulative runs that reached the selected near-best normalized gap threshold."
        table = "tables/time_to_gap_threshold.tsv"
    elif name.startswith("fig8_"):
        desc = "Shows selected-testcase median best-score progress with IQR bands across seeds."
        table = "tables/progress_binned_median_iqr.tsv"
    elif name.startswith("fig9_"):
        desc = "Shows selected-testcase timing metric progress with median/IQR bands."
        table = "progress.tsv inputs summarized from available traces"
    elif name.startswith("fig10_"):
        desc = "Shows area progress only after each run first becomes feasible."
        table = "progress.tsv inputs plus tables/time_to_feasible.tsv"
    elif name.startswith("fig11_"):
        desc = "Shows average all-testcase best-score progress as mean normalized gap, with q25-q75 bands over testcase medians."
        table = "tables/progress_all_testcases_average.tsv"
    elif name.startswith("fig12_"):
        desc = "Shows all-testcase average timing progress for tns_ss, wns_ss, tns_ff, and wns_ff."
        table = "tables/timing_all_testcases_average.tsv"
    elif name.startswith("fig13_"):
        desc = "Shows all-testcase average normalized setup slack optimization progress."
        table = "tables/normalized_objective_progress.tsv"
    elif name.startswith("fig14_"):
        desc = "Shows all-testcase average normalized hold slack optimization progress."
        table = "tables/normalized_objective_progress.tsv"
    elif name.startswith("fig15_"):
        desc = "Shows all-testcase average normalized area optimization progress."
        table = "tables/normalized_objective_progress.tsv"
    elif name.startswith(("fig16_", "fig20_", "fig24_", "fig28_")):
        desc = "Shows group-filtered all-testcase average best-score progress as mean normalized gap."
        table = "tables/progress_all_testcases_average.tsv"
    elif name.startswith(("fig17_", "fig21_", "fig25_", "fig29_")):
        desc = "Shows group-filtered all-testcase average normalized setup slack optimization progress."
        table = "tables/normalized_objective_progress.tsv"
    elif name.startswith(("fig18_", "fig22_", "fig26_", "fig30_")):
        desc = "Shows group-filtered all-testcase average normalized hold slack optimization progress."
        table = "tables/normalized_objective_progress.tsv"
    elif name.startswith(("fig19_", "fig23_", "fig27_", "fig31_")):
        desc = "Shows group-filtered all-testcase average normalized area optimization progress."
        table = "tables/normalized_objective_progress.tsv"
    else:
        desc = text.get(name, "Report figure.")
    return [
        f"## `{name}.png` / `{name}.pdf`",
        "",
        f"- Measures: {desc}",
        f"- Input table: `{table}`",
        "- Formula: see `DERIVED_METRICS.md`.",
        "- Interpretation: compare A1-A8 in canonical order; lower normalized gap is better, higher feasibility/win counts are better.",
        "- Caveats: progress-derived figures use only runs with available `progress.tsv` traces and should not be read as full-run conclusions when trace coverage is partial.",
        "",
    ]


def write_audit(
    out_dir: Path,
    args: argparse.Namespace,
    data: ReportData,
    figure_paths: list[Path],
    table_paths: list[Path],
) -> None:
    try:
        import matplotlib

        matplotlib_version = matplotlib.__version__
    except Exception:
        matplotlib_version = "unavailable"
    versions: dict[str, str] = {}
    for module_name in ["numpy", "pandas"]:
        try:
            module = __import__(module_name)
            versions[module_name] = module.__version__
        except Exception:
            versions[module_name] = "not used/unavailable"
    audit = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "arguments": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
        "python_version": sys.version,
        "platform": platform.platform(),
        "matplotlib_version": matplotlib_version,
        "package_versions": versions,
        "input_files": [row.__dict__ for row in data.manifest],
        "detected_columns": data.detected_columns,
        "canonical_column_mapping": data.column_mapping,
        "feasibility_rule": "all available tns_ss, wns_ss, tns_ff, wns_ff >= 0 using slack-like metrics from src/evaluation.hpp",
        "score_direction": "higher-is-better, confirmed from src/evaluation.cpp::score",
        "optimizer_ordering": OPTIMIZERS,
        "selected_testcases": data.selected_testcases,
        "warnings": data.warnings,
        "figures": [str(path.relative_to(out_dir)) for path in figure_paths],
        "tables": [str(path.relative_to(out_dir)) for path in table_paths],
    }
    (out_dir / "RUN_AUDIT.json").write_text(json.dumps(audit, indent=2, sort_keys=True) + "\n")


def run_report(args: argparse.Namespace) -> tuple[int, int, int, Path]:
    run_dir = args.run_dir
    out_dir = args.out_dir if args.out_dir else run_dir / "report_plots"
    out_dir.mkdir(parents=True, exist_ok=True)
    thresholds = parse_thresholds(args.gap_thresholds)
    data = load_inputs(run_dir)
    if args.selected_testcases != "auto":
        data.selected_testcases = [value.strip() for value in args.selected_testcases.split(",") if value.strip()]

    progress_keys = {(row.optimizer, row.testcase, row.seed) for row in data.progress_rows}
    final_keys = {(row.optimizer, row.testcase, row.seed) for row in data.final_runs}
    if data.progress_rows and progress_keys != final_keys:
        data.warnings.append(
            f"Progress trace coverage is partial: {len(progress_keys)} traced run(s) for {len(final_keys)} final run(s). "
            "Anytime figures use only available traces."
        )

    final_rows = final_run_rows(data.final_runs)
    best = best_known_by_testcase(data.final_runs)
    time_to_feasible = compute_time_to_feasible(data.progress_rows)
    time_to_gap = compute_time_to_gap(data.progress_rows, best, thresholds)
    progress_binned = compute_progress_binned(data.progress_rows, args.time_bins)
    progress_all_testcases_average = compute_progress_all_testcases_average(data.progress_rows, best, args.time_bins)
    timing_all_testcases_average = compute_timing_all_testcases_average(data.progress_rows, args.time_bins)
    normalized_objective_progress = compute_normalized_objective_progress(data.progress_rows, args.time_bins)
    present_progress_optimizers = {row.optimizer for row in data.progress_rows}
    for figure_group, optimizers, _filenames in GROUP_AVERAGE_SPECS:
        missing = [optimizer for optimizer in optimizers if optimizer not in present_progress_optimizers]
        available = [optimizer for optimizer in optimizers if optimizer in present_progress_optimizers]
        if not available:
            continue
        if missing:
            data.warnings.append(
                f"{figure_group}: missing progress traces for optimizer(s): {', '.join(missing)}. "
                "The corresponding group figures use available optimizers only."
            )
        progress_all_testcases_average.extend(
            compute_progress_all_testcases_average(
                data.progress_rows,
                best,
                args.time_bins,
                available,
                figure_group,
                GROUP_AVERAGE_MAX_ELAPSED_SEC,
            )
        )
        normalized_objective_progress.extend(
            compute_normalized_objective_progress(
                data.progress_rows,
                args.time_bins,
                available,
                figure_group,
                GROUP_AVERAGE_MAX_ELAPSED_SEC,
            )
        )
    optimizer_summary = compute_optimizer_summary(final_rows, time_to_feasible, time_to_gap, thresholds)
    gap_matrix = compute_gap_matrix(final_rows)
    win_tie_loss = compute_win_tie_loss(final_rows, args.baseline)
    if not any(row["optimizer_alias"] == args.baseline for row in final_rows):
        data.warnings.append(f"Baseline optimizer `{args.baseline}` not found in final results; win/tie/loss table is empty.")

    table_paths = write_tables(
        out_dir,
        data,
        final_rows,
        optimizer_summary,
        gap_matrix,
        win_tie_loss,
        time_to_feasible,
        time_to_gap,
        progress_binned,
        progress_all_testcases_average,
        timing_all_testcases_average,
        normalized_objective_progress,
        args.baseline,
        thresholds,
    )
    figure_paths = generate_figures(
        out_dir,
        final_rows,
        optimizer_summary,
        gap_matrix,
        win_tie_loss,
        time_to_feasible,
        time_to_gap,
        progress_binned,
        progress_all_testcases_average,
        timing_all_testcases_average,
        normalized_objective_progress,
        data,
        thresholds,
        args.baseline,
    )
    write_markdown(out_dir, data, final_rows, optimizer_summary, figure_paths, table_paths, args.baseline, thresholds)
    write_audit(out_dir, args, data, figure_paths, table_paths)
    return len({path.stem for path in figure_paths}), len(table_paths), len(data.warnings), out_dir


def parse_thresholds(text: str) -> list[float]:
    values: list[float] = []
    for part in text.split(","):
        stripped = part.strip()
        if not stripped:
            continue
        value = float(stripped)
        if value < 0.0:
            raise ReportError(f"Gap threshold must be non-negative: {value}")
        values.append(value)
    return values or [0.01, 0.03, 0.05]


def self_test() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        run = root / "run"
        run.mkdir()
        (run / "results.tsv").write_text(
            "\n".join(
                [
                    "OPTIMIZER\tTESTCASE\tSEED_RUN\tSEED\tINITIAL\tFINAL\tTIME(s)\tEXIT\tSTATUS\tTNS_SS\tWNS_SS\tTNS_FF\tWNS_FF\tAREA",
                    "isa\ttc_easy\t0\t1\t0\t100\t10\t0\tOK\t0\t0\t0\t0\t50",
                    "sa\ttc_easy\t0\t1\t0\t98\t12\t0\tOK\t0\t0\t0\t0\t45",
                    "isa\ttc_hard\t0\t1\t0\t80\t10\t0\tOK\t-1\t-0.5\t0\t0\t60",
                    "sa\ttc_hard\t0\t1\t0\t90\t11\t0\tOK\t0\t0\t0\t0\t55",
                ]
            )
            + "\n"
        )
        for optimizer, testcase, seed, rows in [
            (
                "isa",
                "tc_easy",
                "1",
                [
                    (0, 0.0, 90, -1, -1, 0, 0, 60),
                    (1, 1.0, 100, 0, 0, 0, 0, 50),
                ],
            ),
            (
                "sa",
                "tc_easy",
                "1",
                [
                    (0, 0.0, 80, -1, -1, 0, 0, 60),
                    (1, 2.0, 98, 0, 0, 0, 0, 45),
                ],
            ),
            (
                "isa",
                "tc_hard",
                "1",
                [
                    (0, 0.0, 70, -1, -1, 0, 0, 70),
                    (1, 3.0, 80, -1, -0.5, 0, 0, 60),
                ],
            ),
            (
                "sa",
                "tc_hard",
                "1",
                [
                    (0, 0.0, 70, -1, -1, 0, 0, 70),
                    (1, 4.0, 90, 0, 0, 0, 0, 55),
                ],
            ),
        ]:
            directory = run / "progress" / optimizer / f"seed_{seed}" / testcase
            directory.mkdir(parents=True)
            lines = ["optimizer\ttestcase\tstep\telapsed_sec\tphase\tround\tevent\tcurrent_score\tbest_score\tdelta_score\ttns_ss\twns_ss\ttns_ff\twns_ff\tarea"]
            for step, elapsed, best_score, tns_ss, wns_ss, tns_ff, wns_ff, area in rows:
                lines.append(
                    f"{optimizer}\t{testcase}\t{step}\t{elapsed}\tmain\t0\taccepted\t{best_score}\t{best_score}\t0\t{tns_ss}\t{wns_ss}\t{tns_ff}\t{wns_ff}\t{area}"
                )
            (directory / "progress.tsv").write_text("\n".join(lines) + "\n")
        args = argparse.Namespace(
            run_dir=run,
            out_dir=run / "report_plots",
            baseline="isa",
            gap_thresholds="0.01,0.03,0.05",
            time_bins=2,
            selected_testcases="auto",
            self_test=False,
        )
        figures, tables, warnings, out_dir = run_report(args)
        final_rows = read_table(out_dir / "tables" / "final_runs_normalized.tsv")
        gap = {
            (row["optimizer_alias"], row["testcase"]): float(row["normalized_gap"])
            for row in final_rows
        }
        assert abs(gap[("isa", "tc_easy")] - 0.0) <= 1e-12
        assert abs(gap[("sa", "tc_easy")] - 0.02) <= 1e-12
        assert abs(gap[("isa", "tc_hard")] - (10.0 / 90.0)) <= 1e-9
        assert gap[("sa", "tc_hard")] == 0.0
        feasible = {(row["optimizer_alias"], row["testcase"]): row["feasible"] for row in final_rows}
        assert feasible[("isa", "tc_hard")] == "false"
        assert feasible[("sa", "tc_hard")] == "true"
        wtl = read_table(out_dir / "tables" / "win_tie_loss_vs_isa.tsv")
        sa = next(row for row in wtl if row["optimizer"] == "sa")
        assert (int(sa["wins"]), int(sa["ties"]), int(sa["losses"])) == (1, 0, 1)
        ttf = {
            (row["optimizer"], row["testcase"]): row["time_to_feasible_sec"]
            for row in read_table(out_dir / "tables" / "time_to_feasible.tsv")
        }
        assert ttf[("isa", "tc_easy")] == "1"
        assert ttf[("isa", "tc_hard")] == "NA"
        ttg = read_table(out_dir / "tables" / "time_to_gap_threshold.tsv")
        sa_hard_1pct = next(
            row for row in ttg if row["optimizer"] == "sa" and row["testcase"] == "tc_hard" and row["threshold"] == "0.01"
        )
        assert sa_hard_1pct["time_to_gap_sec"] == "4"
        binned = read_table(out_dir / "tables" / "progress_binned_median_iqr.tsv")
        assert any(row["num_seed_traces"] == "1" for row in binned)
        averaged = read_table(out_dir / "tables" / "progress_all_testcases_average.tsv")
        assert any(row["optimizer"] == "sa" and row["time_bin"] == "1" for row in averaged)
        timing_average = read_table(out_dir / "tables" / "timing_all_testcases_average.tsv")
        assert any(row["metric"] == "wns_ss" and row["optimizer"] == "sa" for row in timing_average)
        objectives = read_table(out_dir / "tables" / "normalized_objective_progress.tsv")
        assert any(row["objective"] == "setup_slack" and row["optimizer"] == "sa" for row in objectives)
        assert any(row["objective"] == "hold_slack" and row["optimizer"] == "sa" for row in objectives)
        assert any(row["objective"] == "area" and row["optimizer"] == "sa" for row in objectives)
        assert (out_dir / "figures" / "fig1_feasible_rate_by_optimizer.png").is_file()
        assert (out_dir / "figures" / "fig4_normalized_gap_heatmap.pdf").is_file()
        assert (out_dir / "figures" / "fig11_all_testcases_average_best_score_progress.png").is_file()
        assert (out_dir / "figures" / "fig12_all_testcases_average_timing_progress.png").is_file()
        assert (out_dir / "figures" / "fig13_normalized_setup_slack_optimization_average.png").is_file()
        assert (out_dir / "figures" / "fig14_normalized_hold_slack_optimization_average.png").is_file()
        assert (out_dir / "figures" / "fig15_normalized_area_optimization_average.png").is_file()
        assert (out_dir / "RUN_AUDIT.json").is_file()
        assert figures >= 8
        assert tables >= 8
        assert warnings == 0


def read_table(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, help="Aggregated Slurm/local run directory")
    parser.add_argument("--out-dir", type=Path, help="Output directory, default: <run-dir>/report_plots")
    parser.add_argument("--baseline", default="isa", help="Baseline optimizer for win/tie/loss comparison")
    parser.add_argument("--gap-thresholds", default="0.01,0.03,0.05", help="Comma-separated normalized gap thresholds")
    parser.add_argument("--time-bins", type=int, default=80, help="Number of elapsed-time bins for progress summaries")
    parser.add_argument("--selected-testcases", default="auto", help="'auto' or comma-separated testcase names")
    parser.add_argument("--self-test", action="store_true", help="Run a synthetic end-to-end self-test")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            print("Self-test passed.")
            return 0
        if args.run_dir is None:
            raise ReportError("--run-dir is required unless --self-test is used")
        if args.time_bins <= 0:
            raise ReportError("--time-bins must be positive")
        figures, tables, warnings, out_dir = run_report(args)
        print(f"Report output: {out_dir}")
        print(f"Figures: {figures}")
        print(f"Tables: {tables}")
        print(f"Warnings: {warnings}")
        print(f"Summary: {out_dir / 'REPORT_SUMMARY.md'}")
        return 0
    except ReportError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except AssertionError as exc:
        print(f"self-test failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
