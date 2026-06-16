#!/usr/bin/env python3
"""Plot optimizer numeric event traces produced by CADD0040_PROGRESS_TRACE=1."""

from __future__ import annotations

import argparse
import csv
import math
import re
import struct
import sys
import zlib
from collections import defaultdict
from pathlib import Path


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def parse_float(value: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return math.nan


def parse_int(value: str) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return -1


def find_progress_files(run_dir: Path) -> list[Path]:
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
    seed = row.get("seed", "")
    parent = path.parent
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


def read_progress(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            optimizer, testcase, seed = infer_progress_context(path, row)
            rows.append(
                {
                    "optimizer": optimizer,
                    "testcase": testcase,
                    "seed": seed,
                    "step": parse_int(row.get("step", "")),
                    "elapsed_sec": parse_float(row.get("elapsed_sec", "")),
                    "phase": row.get("phase", ""),
                    "event": row.get("event", ""),
                    "current_score": parse_float(row.get("current_score", "")),
                    "best_score": parse_float(row.get("best_score", "")),
                    "tns_ss": parse_float(row.get("tns_ss", "")),
                    "wns_ss": parse_float(row.get("wns_ss", "")),
                    "tns_ff": parse_float(row.get("tns_ff", "")),
                    "wns_ff": parse_float(row.get("wns_ff", "")),
                    "area": parse_float(row.get("area", "")),
                }
            )
    return rows


def numeric_series(rows: list[dict[str, object]], x_key: str, y_key: str) -> tuple[list[float], list[float]]:
    xs: list[float] = []
    ys: list[float] = []
    for row in rows:
        x = row["elapsed_sec"] if x_key in {"time", "elapsed_sec"} else row["step"]
        y = row[y_key]
        if isinstance(x, int):
            x = float(x)
        if isinstance(y, float) and isinstance(x, float) and not math.isnan(x) and not math.isnan(y):
            xs.append(x)
            ys.append(y)
    return xs, ys


def mean_series(rows: list[dict[str, object]], x_key: str, y_key: str, bins: int = 80) -> tuple[list[float], list[float]]:
    seed_points: dict[str, dict[float, list[float]]] = defaultdict(lambda: defaultdict(list))
    raw_points: list[tuple[str, float, float]] = []
    for row in rows:
        x = row["elapsed_sec"] if x_key in {"time", "elapsed_sec"} else row["step"]
        y = row[y_key]
        if isinstance(x, int):
            x = float(x)
        if not (isinstance(x, float) and isinstance(y, float)):
            continue
        if math.isnan(x) or math.isnan(y):
            continue
        raw_points.append((str(row.get("seed", "default")), x, y))

    if not raw_points:
        return [], []

    if x_key in {"time", "elapsed_sec"}:
        xmin = min(point[1] for point in raw_points)
        xmax = max(point[1] for point in raw_points)
        if xmax > xmin:
            width = (xmax - xmin) / max(1, bins)
            normalized_points: list[tuple[str, float, float]] = []
            for seed, x, y in raw_points:
                bin_index = min(max(0, int((x - xmin) / width)), bins - 1)
                bin_x = xmin + (bin_index + 0.5) * width
                normalized_points.append((seed, bin_x, y))
            raw_points = normalized_points

    for seed, x, y in raw_points:
        seed_points[seed][x].append(y)

    by_x: dict[float, list[float]] = defaultdict(list)
    for points_by_x in seed_points.values():
        for x, values in points_by_x.items():
            by_x[x].append(sum(values) / len(values))

    xs: list[float] = []
    ys: list[float] = []
    for x in sorted(by_x):
        values = by_x[x]
        xs.append(x)
        ys.append(sum(values) / len(values))
    return xs, ys


def plot_by_testcase(rows: list[dict[str, object]], out_dir: Path, y_key: str, x_key: str) -> None:
    import matplotlib.pyplot as plt

    grouped: dict[str, dict[str, list[dict[str, object]]]] = defaultdict(lambda: defaultdict(list))
    for row in rows:
        grouped[str(row["testcase"])][str(row["optimizer"])].append(row)

    output_dir = out_dir / "by_testcase"
    output_dir.mkdir(parents=True, exist_ok=True)
    label = "time" if x_key in {"time", "elapsed_sec"} else "step"
    for testcase, by_optimizer in grouped.items():
        fig, ax = plt.subplots(figsize=(10, 5.5))
        for color_index, (optimizer, optimizer_rows) in enumerate(sorted(by_optimizer.items())):
            optimizer_rows.sort(key=lambda r: (float(r["elapsed_sec"]), int(r["step"])))
            color = plt.cm.tab10(color_index % 10)
            xs, ys = numeric_series(optimizer_rows, x_key, y_key)
            if xs:
                seeds = {str(row.get("seed", "default")) for row in optimizer_rows}
                ax.scatter(xs, ys, s=5, alpha=0.16, color=color, linewidths=0)
                mean_xs, mean_ys = mean_series(optimizer_rows, x_key, y_key)
                if mean_xs:
                    ax.plot(
                        mean_xs,
                        mean_ys,
                        color=color,
                        linewidth=2.0,
                        label=f"{optimizer} mean (n={len(seeds)})",
                    )
        ax.set_title(f"{testcase}: {y_key} vs {label}")
        ax.set_xlabel("elapsed seconds" if label == "time" else "logical step")
        ax.set_ylabel(y_key)
        ax.grid(True, linewidth=0.4, alpha=0.35)
        ax.legend(fontsize=8)
        fig.tight_layout()
        fig.savefig(output_dir / f"{safe_name(testcase)}_{safe_name(y_key)}_vs_{label}.png", dpi=160)
        plt.close(fig)


def plot_by_run(rows: list[dict[str, object]], out_dir: Path, y_key: str, x_key: str) -> None:
    import matplotlib.pyplot as plt

    grouped: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[(str(row["optimizer"]), str(row["testcase"]), str(row.get("seed", "default")))].append(row)

    output_dir = out_dir / "by_run"
    output_dir.mkdir(parents=True, exist_ok=True)
    label = "time" if x_key in {"time", "elapsed_sec"} else "step"
    for (optimizer, testcase, seed), run_rows in sorted(grouped.items()):
        run_rows.sort(key=lambda r: (float(r["elapsed_sec"]), int(r["step"])))
        xs, ys = numeric_series(run_rows, x_key, y_key)
        if not xs:
            continue
        phase_names = sorted({str(row["phase"]) for row in run_rows})
        phase_to_index = {phase: idx for idx, phase in enumerate(phase_names)}
        fig, ax = plt.subplots(figsize=(10, 5.5))
        ax.plot(xs, ys, color="black", linewidth=1.0, alpha=0.55)
        for phase in phase_names:
            phase_rows = [row for row in run_rows if row["phase"] == phase]
            px, py = numeric_series(phase_rows, x_key, y_key)
            if px:
                ax.scatter(px, py, s=16, label=phase, alpha=0.85)
        for row in run_rows:
            if row["event"] in {"phase_start", "phase_end", "final", "restart", "best_update"}:
                x = row["elapsed_sec"] if label == "time" else row["step"]
                if isinstance(x, int):
                    x = float(x)
                if isinstance(x, float) and not math.isnan(x):
                    alpha = 0.10 if row["event"] != "best_update" else 0.18
                    color_index = phase_to_index.get(str(row["phase"]), 0)
                    color = plt.cm.tab20(color_index % 20)
                    ax.axvline(x, color=color, alpha=alpha, linewidth=0.8)
        ax.set_title(f"{optimizer} / {testcase} / seed {seed}: phases")
        ax.set_xlabel("elapsed seconds" if label == "time" else "logical step")
        ax.set_ylabel(y_key)
        ax.grid(True, linewidth=0.4, alpha=0.35)
        ax.legend(fontsize=8, ncol=2)
        fig.tight_layout()
        filename = f"{safe_name(optimizer)}__seed_{safe_name(seed)}__{safe_name(testcase)}_phases.png"
        fig.savefig(output_dir / filename, dpi=160)
        plt.close(fig)


PALETTE = [
    (31, 119, 180),
    (255, 127, 14),
    (44, 160, 44),
    (214, 39, 40),
    (148, 103, 189),
    (140, 86, 75),
    (227, 119, 194),
    (127, 127, 127),
    (188, 189, 34),
    (23, 190, 207),
]


def scale_points(xs: list[float], ys: list[float], width: int, height: int) -> list[tuple[int, int]]:
    if not xs or not ys:
        return []
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if xmax == xmin:
        xmax = xmin + 1.0
    if ymax == ymin:
        ymax = ymin + 1.0
    left, right, top, bottom = 70, width - 30, 35, height - 55
    points: list[tuple[int, int]] = []
    for x, y in zip(xs, ys):
        px = left + int((x - xmin) / (xmax - xmin) * (right - left))
        py = bottom - int((y - ymin) / (ymax - ymin) * (bottom - top))
        points.append((px, py))
    return points


def scale_points_with_domain(
    xs: list[float],
    ys: list[float],
    width: int,
    height: int,
    xmin: float,
    xmax: float,
    ymin: float,
    ymax: float,
) -> list[tuple[int, int]]:
    if not xs or not ys:
        return []
    if xmax == xmin:
        xmax = xmin + 1.0
    if ymax == ymin:
        ymax = ymin + 1.0
    left, right, top, bottom = 70, width - 30, 35, height - 55
    points: list[tuple[int, int]] = []
    for x, y in zip(xs, ys):
        px = left + int((x - xmin) / (xmax - xmin) * (right - left))
        py = bottom - int((y - ymin) / (ymax - ymin) * (bottom - top))
        points.append((px, py))
    return points


def set_pixel(pixels: bytearray, width: int, height: int, x: int, y: int, color: tuple[int, int, int]) -> None:
    if x < 0 or y < 0 or x >= width or y >= height:
        return
    index = (y * width + x) * 3
    pixels[index : index + 3] = bytes(color)


def draw_line(
    pixels: bytearray,
    width: int,
    height: int,
    start: tuple[int, int],
    end: tuple[int, int],
    color: tuple[int, int, int],
) -> None:
    x0, y0 = start
    x1, y1 = end
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        set_pixel(pixels, width, height, x0, y0, color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def draw_thick_line(
    pixels: bytearray,
    width: int,
    height: int,
    start: tuple[int, int],
    end: tuple[int, int],
    color: tuple[int, int, int],
) -> None:
    for offset in (-1, 0, 1):
        draw_line(pixels, width, height, (start[0], start[1] + offset), (end[0], end[1] + offset), color)


def draw_rect(
    pixels: bytearray,
    width: int,
    height: int,
    left: int,
    top: int,
    right: int,
    bottom: int,
    color: tuple[int, int, int],
) -> None:
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            set_pixel(pixels, width, height, x, y, color)


def draw_point(
    pixels: bytearray, width: int, height: int, point: tuple[int, int], color: tuple[int, int, int]
) -> None:
    x, y = point
    draw_rect(pixels, width, height, x - 3, y - 3, x + 3, y + 3, color)


def draw_small_point(
    pixels: bytearray, width: int, height: int, point: tuple[int, int], color: tuple[int, int, int]
) -> None:
    x, y = point
    draw_rect(pixels, width, height, x - 1, y - 1, x + 1, y + 1, color)


def save_png(path: Path, width: int, height: int, pixels: bytearray) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    rows = bytearray()
    stride = width * 3
    for y in range(height):
        rows.append(0)
        rows.extend(pixels[y * stride : (y + 1) * stride])
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    png.extend(chunk(b"IDAT", zlib.compress(bytes(rows), 6)))
    png.extend(chunk(b"IEND", b""))
    path.write_bytes(png)


def draw_basic_chart(
    path: Path,
    title: str,
    line_series: list[tuple[str, list[float], list[float]]],
    point_series: list[tuple[str, list[float], list[float]]] | None = None,
) -> None:
    width, height = 1000, 560
    pixels = bytearray([255, 255, 255]) * (width * height)
    draw_line(pixels, width, height, (70, 35), (70, height - 55), (40, 40, 40))
    draw_line(pixels, width, height, (70, height - 55), (width - 30, height - 55), (40, 40, 40))

    all_series = line_series + (point_series or [])
    all_xs = [x for _label, xs, _ys in all_series for x in xs]
    all_ys = [y for _label, _xs, ys in all_series for y in ys]
    if not all_xs or not all_ys:
        save_png(path, width, height, pixels)
        return
    xmin, xmax = min(all_xs), max(all_xs)
    ymin, ymax = min(all_ys), max(all_ys)

    if point_series:
        for index, (_label, xs, ys) in enumerate(point_series):
            points = scale_points_with_domain(xs, ys, width, height, xmin, xmax, ymin, ymax)
            color = PALETTE[index % len(PALETTE)]
            pale_color = tuple(min(255, int(channel + (255 - channel) * 0.55)) for channel in color)
            for point in points:
                draw_small_point(pixels, width, height, point, pale_color)

    legend_y = 36
    for index, (label, xs, ys) in enumerate(line_series):
        points = scale_points_with_domain(xs, ys, width, height, xmin, xmax, ymin, ymax)
        if not points:
            continue
        color = PALETTE[index % len(PALETTE)]
        for start, end in zip(points, points[1:]):
            draw_thick_line(pixels, width, height, start, end, color)
        for point in points:
            draw_point(pixels, width, height, point, color)
        draw_rect(pixels, width, height, width - 220, legend_y, width - 206, legend_y + 10, color)
        legend_y += 14

    save_png(path, width, height, pixels)


def plot_by_testcase_pillow(
    rows: list[dict[str, object]], out_dir: Path, y_key: str, x_key: str
) -> None:
    grouped: dict[str, dict[str, list[dict[str, object]]]] = defaultdict(lambda: defaultdict(list))
    for row in rows:
        grouped[str(row["testcase"])][str(row["optimizer"])].append(row)

    output_dir = out_dir / "by_testcase"
    output_dir.mkdir(parents=True, exist_ok=True)
    label = "time" if x_key in {"time", "elapsed_sec"} else "step"
    for testcase, by_optimizer in grouped.items():
        point_series: list[tuple[str, list[float], list[float]]] = []
        line_series: list[tuple[str, list[float], list[float]]] = []
        for optimizer, optimizer_rows in sorted(by_optimizer.items()):
            optimizer_rows.sort(key=lambda r: (float(r["elapsed_sec"]), int(r["step"])))
            xs, ys = numeric_series(optimizer_rows, x_key, y_key)
            seeds = {str(row.get("seed", "default")) for row in optimizer_rows}
            mean_xs, mean_ys = mean_series(optimizer_rows, x_key, y_key)
            point_series.append((optimizer, xs, ys))
            line_series.append((f"{optimizer} mean (n={len(seeds)})", mean_xs, mean_ys))
        draw_basic_chart(
            output_dir / f"{safe_name(testcase)}_{safe_name(y_key)}_vs_{label}.png",
            f"{testcase}: {y_key} vs {label}",
            line_series,
            point_series,
        )


def plot_by_run_pillow(rows: list[dict[str, object]], out_dir: Path, y_key: str, x_key: str) -> None:
    grouped: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[(str(row["optimizer"]), str(row["testcase"]), str(row.get("seed", "default")))].append(row)

    output_dir = out_dir / "by_run"
    output_dir.mkdir(parents=True, exist_ok=True)
    label = "time" if x_key in {"time", "elapsed_sec"} else "step"
    for (optimizer, testcase, seed), run_rows in sorted(grouped.items()):
        run_rows.sort(key=lambda r: (float(r["elapsed_sec"]), int(r["step"])))
        series: list[tuple[str, list[float], list[float]]] = []
        for phase in sorted({str(row["phase"]) for row in run_rows}):
            phase_rows = [row for row in run_rows if row["phase"] == phase]
            xs, ys = numeric_series(phase_rows, x_key, y_key)
            series.append((phase, xs, ys))
        filename = f"{safe_name(optimizer)}__seed_{safe_name(seed)}__{safe_name(testcase)}_phases.png"
        draw_basic_chart(output_dir / filename, f"{optimizer} / {testcase} / seed {seed}: phases", series)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, required=True, help="Slurm/local run directory")
    parser.add_argument(
        "--x",
        choices=["step", "time", "elapsed_sec"],
        default="step",
        help="X axis for per-seed phase plots. Comparison plots always include step and time.",
    )
    parser.add_argument(
        "--y",
        choices=["best_score", "current_score", "tns_ss", "wns_ss", "tns_ff", "wns_ff", "area"],
        default="best_score",
        help="Y axis metric",
    )
    parser.add_argument("--out-dir", type=Path, help="Output directory, default: <run-dir>/plots")
    args = parser.parse_args()

    progress_files = find_progress_files(args.run_dir)
    if not progress_files:
        print(
            f"No progress.tsv files found under {args.run_dir}. "
            "Run with CADD0040_PROGRESS_TRACE=1 to generate numeric event trace data.",
            file=sys.stderr,
        )
        return 0

    rows: list[dict[str, object]] = []
    for path in progress_files:
        rows.extend(read_progress(path))
    if not rows:
        print(f"Progress files were found but contained no rows under {args.run_dir}.", file=sys.stderr)
        return 0

    out_dir = args.out_dir if args.out_dir is not None else args.run_dir / "plots"
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        import matplotlib  # noqa: F401

        plot_by_testcase(rows, out_dir, args.y, "step")
        plot_by_testcase(rows, out_dir, args.y, "time")
        plot_by_run(rows, out_dir, args.y, args.x)
    except ImportError:
        print("matplotlib not found; using standard-library PNG fallback plots.", file=sys.stderr)
        plot_by_testcase_pillow(rows, out_dir, args.y, "step")
        plot_by_testcase_pillow(rows, out_dir, args.y, "time")
        plot_by_run_pillow(rows, out_dir, args.y, args.x)
    print(f"Wrote optimizer progress plots to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
