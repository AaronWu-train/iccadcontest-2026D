#!/usr/bin/env python3
"""Plot optimizer progress with the score definitions used in the report.

This is intentionally kept simple:
  - edit the CONFIG section near the top to change figure groups, colors, or axis labels
  - the main plotting logic is in main()
  - only a few small helper functions are used for repeated file/aggregation work

Output:
  fig1.png  Candidate policy
  fig2.png  Acceptance strategy: random
  fig3.png  Acceptance strategy: union
  fig4.png  ISA vs. Tabu

Panel definitions:
  Total panel:
      upward best-score gap = row.best_score - best_known_score[testcase]
      This matches the original plot_optimizer_report.py gap normalization, but flips
      the sign so curves move upward toward 0.

  Setup panel:
      setup_score =
          (1 - tns_ss / tns_ss_ori) + (1 - wns_ss / wns_ss_ori)

  Hold panel:
      hold_score =
          (1 - tns_ff / tns_ff_ori) + (1 - wns_ff / wns_ff_ori)

  Area panel:
      area_score =
          1 - area / area_ori

Setup/Hold/Area use the current progress row values. They are not normalized
again by best-observed values.
"""

from __future__ import annotations

import argparse
import glob
import math
import os
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


# =============================================================================
# CONFIG: edit here first
# =============================================================================

DEFAULT_OUT_DIR = "report/images"
DEFAULT_TIME_BINS = 114
DEFAULT_MAX_TIME = 570.0

FIGURES = [
    {
        "filename": "fig1",
        "title": "Candidate-policy comparison under greedy acceptance",
        "optimizers": [
            "greedy-union-pool",
            "greedy-random",
            "greedy-upstream-window",
            "greedy-violation-path",
            "greedy-critical-endpoint",
        ],
    },
    {
        "filename": "fig2",
        "title": "Acceptance-policy comparison under the random candidate policy.",
        "optimizers": [
            "greedy-random",
            "two-step-random",
            "sa-random",
            "isa-random",
            "tabu-random",
        ],
    },
    {
        "filename": "fig3",
        "title": "Acceptance-policy comparison under the union-pool candidate policy.",
        "optimizers": [
            "greedy-union-pool",
            "two-step-union-pool",
            "sa-sampled-union-pool",
            "isa-sampled-union-pool",
            "tabu-union-pool",
        ],
    },
    {
        "filename": "fig4",
        "title": "ISA vs. Tabu",
        "optimizers": [
            "isa-random",
            "isa-sampled-union-pool",
            "tabu-random",
            "tabu-union-pool",
        ],
    },
]

# One stable color per exact optimizer name across all figures.
COLORS = {
    "greedy-random": "#1031FF",
    "greedy-violation-path": "#008000",
    "greedy-upstream-window": "#008080",
    "greedy-critical-endpoint": "#85A91C",
    "greedy-union-pool": "#2792FF",
    "two-step-random": "#9A5E24",
    "two-step-union-pool": "#B49971",
    "isa-random": "#9B0101",
    "isa-sampled-union-pool": "#FF0000",
    "sa-random": "#FF5F15",
    "sa-sampled-union-pool": "#FA8C01",
    "tabu-random": "#9B259B",
    "tabu-union-pool": "#B47FCA",
}

LINE_WIDTH = 1.3
SHADE_ALPHA = 0.055
FIG_SIZE = (13.0, 8.4)
DPI = 300

# Font sizes tuned for report figures.
PANEL_TITLE_FONT_SIZE = 12.0
FIGURE_TITLE_FONT_SIZE = 14.5
AXIS_LABEL_FONT_SIZE = 10
TICK_LABEL_FONT_SIZE = 8.8
LEGEND_FONT_SIZE = 13.0

PANELS = [
    ("best_gap", "Best-score gap", "Gap to best"),
    ("setup_score", "Setup score", "Score"),
    ("hold_score", "Hold score", "Score"),
    ("area_score", "Area score", "Score"),
]


# =============================================================================
# Small helpers
# =============================================================================


def score_term(value: pd.Series, original: pd.Series) -> pd.Series:
    """Compute 1 - value/original with a zero-denominator guard."""
    out = 1.0 - value / original
    out = out.where(original.abs() > 1e-12, 0.0)
    return out.replace([np.inf, -np.inf], np.nan).fillna(0.0)


def load_all_progress(run_dir: Path) -> pd.DataFrame:
    """Load progress.tsv files and compute the four report scores."""
    pattern = str(run_dir / "progress" / "*" / "seed_*" / "*" / "progress.tsv")
    paths = sorted(glob.glob(pattern))
    if not paths:
        raise SystemExit(f"No progress.tsv files found under: {pattern}")

    all_runs = []

    for path_str in paths:
        path = Path(path_str)
        parts = path.parts

        try:
            progress_idx = parts.index("progress")
            optimizer = parts[progress_idx + 1]
        except ValueError:
            continue

        seed = next((p for p in parts if p.startswith("seed_")), "seed_unknown")
        testcase = path.parent.name

        df = pd.read_csv(path, sep="\t")
        if df.empty:
            continue

        # Accept a few common time-column names.
        rename = {}
        if "elapsed_seconds" in df.columns:
            rename["elapsed_seconds"] = "elapsed_sec"
        if "time_sec" in df.columns:
            rename["time_sec"] = "elapsed_sec"
        df = df.rename(columns=rename)

        required = [
            "elapsed_sec",
            "best_score",
            "tns_ss",
            "wns_ss",
            "tns_ff",
            "wns_ff",
            "area",
        ]
        missing = [c for c in required if c not in df.columns]
        if missing:
            print(f"[skip] {path}: missing columns {missing}")
            continue

        for col in required:
            df[col] = pd.to_numeric(df[col], errors="coerce")
        if "step" in df.columns:
            df["step"] = pd.to_numeric(df["step"], errors="coerce").fillna(-1)
        else:
            df["step"] = -1

        df = df.dropna(subset=required).sort_values(["elapsed_sec", "step"]).copy()
        if df.empty:
            continue

        first = df.iloc[0]
        df["optimizer"] = optimizer
        df["seed"] = seed
        df["testcase"] = testcase

        df["tns_ss_ori"] = float(first["tns_ss"])
        df["wns_ss_ori"] = float(first["wns_ss"])
        df["tns_ff_ori"] = float(first["tns_ff"])
        df["wns_ff_ori"] = float(first["wns_ff"])
        df["area_ori"] = float(first["area"])

        df["setup_score"] = score_term(df["tns_ss"], df["tns_ss_ori"]) + score_term(
            df["wns_ss"], df["wns_ss_ori"]
        )
        df["hold_score"] = score_term(df["tns_ff"], df["tns_ff_ori"]) + score_term(
            df["wns_ff"], df["wns_ff_ori"]
        )
        df["area_score"] = score_term(df["area"], df["area_ori"])

        all_runs.append(df)

    if not all_runs:
        raise SystemExit("No usable progress.tsv files were loaded.")

    data = pd.concat(all_runs, ignore_index=True)

    # best_known_score[testcase] = best observed best_score in this dataset.
    best_known = data.groupby("testcase")["best_score"].max().rename("best_known_score")
    data = data.merge(best_known, on="testcase", how="left")

    data["best_gap"] = data["best_score"] - data["best_known_score"]

    return data


def aggregate_metric(
    data: pd.DataFrame,
    optimizer: str,
    metric: str,
    bins: np.ndarray,
) -> pd.DataFrame:
    """Aggregate one metric for one optimizer.

    This follows the original report logic:
      1. within each optimizer/testcase/seed/time-bin, use the last logged row
      2. forward-fill missing bins within each run
      3. take median over seeds for each testcase
      4. take mean over testcases
      5. q25/q75 show testcase-level spread
    """
    sub = data[data["optimizer"] == optimizer].copy()
    if sub.empty:
        return pd.DataFrame()

    n_bins = len(bins) - 1
    centers = 0.5 * (bins[:-1] + bins[1:])
    sub["bin"] = pd.cut(
        sub["elapsed_sec"], bins=bins, labels=False, include_lowest=True
    )
    sub = sub.dropna(subset=["bin"]).copy()
    sub["bin"] = sub["bin"].astype(int)

    per_run = []
    for (testcase, seed), run in sub.groupby(["testcase", "seed"]):
        last_in_bin = (
            run.sort_values(["elapsed_sec", "step"])
            .groupby("bin", as_index=True)[metric]
            .last()
            .reindex(range(n_bins))
            .ffill()
            .fillna(0.0)
        )
        per_run.append(
            pd.DataFrame(
                {
                    "testcase": testcase,
                    "seed": seed,
                    "bin": np.arange(n_bins),
                    metric: last_in_bin.to_numpy(),
                }
            )
        )

    if not per_run:
        return pd.DataFrame()

    per_run_df = pd.concat(per_run, ignore_index=True)

    seed_median = per_run_df.groupby(["testcase", "bin"], as_index=False)[
        metric
    ].median()

    testcase_values = seed_median.groupby("bin")[metric]
    agg = pd.DataFrame(
        {
            "time": centers,
            "mean": testcase_values.mean()
            .reindex(range(n_bins))
            .ffill()
            .fillna(0.0)
            .to_numpy(),
            "q25": testcase_values.quantile(0.25)
            .reindex(range(n_bins))
            .ffill()
            .fillna(0.0)
            .to_numpy(),
            "q75": testcase_values.quantile(0.75)
            .reindex(range(n_bins))
            .ffill()
            .fillna(0.0)
            .to_numpy(),
        }
    )
    return agg


def set_y_limits(ax: plt.Axes, values: list[float]) -> None:
    clean = [v for v in values if math.isfinite(v)]
    if not clean:
        ax.set_ylim(-0.05, 1.05)
        return
    lo = min(clean)
    hi = max(clean)
    if abs(hi - lo) < 1e-9:
        pad = max(0.05, abs(hi) * 0.1)
    else:
        pad = 0.08 * (hi - lo)
    ax.set_ylim(lo - pad, hi + pad)


# =============================================================================
# Main plotting code
# =============================================================================


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, default=Path(DEFAULT_OUT_DIR))
    parser.add_argument("--time-bins", type=int, default=DEFAULT_TIME_BINS)
    parser.add_argument("--max-time", type=float, default=DEFAULT_MAX_TIME)
    args = parser.parse_args()

    data = load_all_progress(args.run_dir)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    bins = np.linspace(0.0, args.max_time, args.time_bins + 1)

    for fig_cfg in FIGURES:
        optimizers = [
            opt for opt in fig_cfg["optimizers"] if opt in set(data["optimizer"])
        ]
        if not optimizers:
            print(f"[skip] {fig_cfg['filename']}: no matching optimizers")
            continue

        fig, axes = plt.subplots(2, 2, figsize=FIG_SIZE, sharex=True)
        axes = axes.flatten()

        legend_handles = None
        legend_labels = None

        for ax, (metric, panel_title, y_label) in zip(axes, PANELS):
            y_values = []

            for optimizer in optimizers:
                agg = aggregate_metric(data, optimizer, metric, bins)
                if agg.empty:
                    continue

                color = COLORS.get(optimizer, None)
                line = ax.plot(
                    agg["time"],
                    agg["mean"],
                    label=optimizer,
                    color=color,
                    linewidth=LINE_WIDTH,
                )[0]

                ax.fill_between(
                    agg["time"].to_numpy(dtype=float),
                    agg["q25"].to_numpy(dtype=float),
                    agg["q75"].to_numpy(dtype=float),
                    color=line.get_color(),
                    alpha=SHADE_ALPHA,
                    linewidth=0,
                )

                y_values.extend(agg["mean"].tolist())
                y_values.extend(agg["q25"].tolist())
                y_values.extend(agg["q75"].tolist())

            ax.set_title(panel_title, fontsize=PANEL_TITLE_FONT_SIZE, fontweight="bold")
            ax.set_xlabel("Time (s)", fontsize=AXIS_LABEL_FONT_SIZE)
            ax.set_ylabel(y_label, fontsize=AXIS_LABEL_FONT_SIZE)
            ax.tick_params(axis="both", labelsize=TICK_LABEL_FONT_SIZE)
            ax.grid(True, linestyle="--", linewidth=0.55, alpha=0.25)
            ax.axhline(0.0, color="black", linestyle=":", linewidth=0.7, alpha=0.8)
            set_y_limits(ax, y_values)

            if legend_handles is None:
                legend_handles, legend_labels = ax.get_legend_handles_labels()

        fig.suptitle(fig_cfg["title"], fontsize=FIGURE_TITLE_FONT_SIZE, fontweight="bold", y=0.992)

        if legend_handles and legend_labels:
            fig.legend(
                legend_handles,
                legend_labels,
                loc="lower center",
                ncol=min(3, len(legend_labels)),
                frameon=True,
                fontsize=LEGEND_FONT_SIZE,
                bbox_to_anchor=(0.5, -0.03),
            )

        fig.tight_layout(rect=(0, 0.04, 1, 1))

        out_path = args.out_dir / f"{fig_cfg['filename']}.png"
        fig.savefig(out_path, bbox_inches="tight", dpi=DPI)
        plt.close(fig)
        print(f"saved: {out_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
