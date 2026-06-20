#!/usr/bin/env python3
"""Package the final course submission archive."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
ZIP_NAME = "B13901011_B13901078_B13901088_B13901104.zip"
DEFAULT_OUTPUT_DIR = ROOT / "dist" / "submission"
STAGING_DIR_NAME = "staging"

SUBMISSION_README = ROOT / "submission" / "A_README.md"
SUPPLEMENTAL_DIR = ROOT / "submission" / "E_Supplemental_Materials"
PRESENTATION_PDF = ROOT / "report" / "presentation.pdf"
REPORT_PDF = ROOT / "report" / "main.pdf"

SOURCE_ITEMS = [
    "README.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "CMakeLists.txt",
    "Makefile",
    ".clang-format",
    ".pre-commit-config.yaml",
    ".dockerignore",
    ".gitignore",
    "src",
    "scripts",
    "tests",
    "testcases",
    "docker",
]

SOURCE_EXCLUDED_DIRS = {
    ".git",
    ".cache",
    ".understand-anything",
    ".vscode",
    "__pycache__",
    "build",
    "build-release",
    "dist",
    "slurm_runs",
    "config_runs",
}

SOURCE_EXCLUDED_FILES = {
    ".DS_Store",
    "AGENTS.md",
    "modified_clk_tree.structure",
}

SUPPLEMENTAL_EXCLUDED_FILES = {
    ".DS_Store",
    ".gitkeep",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the final submission zip with A/E from submission/ and B/C/D generated."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"Output directory, default: {DEFAULT_OUTPUT_DIR}",
    )
    parser.add_argument(
        "--keep-staging",
        action="store_true",
        help="Keep the generated staging directory for inspection.",
    )
    return parser.parse_args()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"error: required file not found: {path}")


def require_dir(path: Path) -> None:
    if not path.is_dir():
        raise SystemExit(f"error: required directory not found: {path}")


def reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def should_skip_source(path: Path) -> bool:
    if path.name in SOURCE_EXCLUDED_FILES:
        return True
    if path.suffix == ".zip":
        return True
    if path.suffix == ".log":
        return True
    return False


def copy_source_item(source: Path, destination: Path) -> None:
    if source.is_file():
        if should_skip_source(source):
            return
        ensure_parent(destination)
        shutil.copy2(source, destination)
        return

    if not source.is_dir():
        return

    for child in source.iterdir():
        if child.is_dir():
            if child.name in SOURCE_EXCLUDED_DIRS:
                continue
            copy_source_item(child, destination / child.name)
        elif not should_skip_source(child):
            ensure_parent(destination / child.name)
            shutil.copy2(child, destination / child.name)


def copy_supplemental(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for child in source.iterdir():
        if child.name in SUPPLEMENTAL_EXCLUDED_FILES:
            continue
        target = destination / child.name
        if child.is_dir():
            copy_supplemental(child, target)
        else:
            ensure_parent(target)
            shutil.copy2(child, target)


def add_zip_entry(zip_file: ZipFile, path: Path, arcname: Path) -> None:
    if path.is_dir():
        zip_file.write(path, f"{arcname.as_posix().rstrip('/')}/")
        children = sorted(path.iterdir(), key=lambda item: item.name)
        for child in children:
            add_zip_entry(zip_file, child, arcname / child.name)
    else:
        zip_file.write(path, arcname.as_posix())


def create_zip(staging_dir: Path, zip_path: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()
    with ZipFile(zip_path, "w", ZIP_DEFLATED, compresslevel=9) as zip_file:
        for child in sorted(staging_dir.iterdir(), key=lambda item: item.name):
            add_zip_entry(zip_file, child, Path(child.name))


def format_size(size_bytes: int) -> str:
    size_mib = size_bytes / 1024 / 1024
    return f"{size_mib:.2f} MiB"


def build_staging(staging_dir: Path) -> None:
    reset_dir(staging_dir)

    shutil.copy2(SUBMISSION_README, staging_dir / "A_README.md")

    slides_dir = staging_dir / "B_Presentation_Slides"
    slides_dir.mkdir()
    shutil.copy2(PRESENTATION_PDF, slides_dir / "presentation.pdf")

    report_dir = staging_dir / "C_Project_Report"
    report_dir.mkdir()
    shutil.copy2(REPORT_PDF, report_dir / "report.pdf")

    source_root = staging_dir / "D_Source_Code_and_Testcases"
    source_root.mkdir()
    for item in SOURCE_ITEMS:
        source = ROOT / item
        if not source.exists():
            print(f"warning: source item not found, skipping: {item}", file=sys.stderr)
            continue
        copy_source_item(source, source_root / item)

    copy_supplemental(SUPPLEMENTAL_DIR, staging_dir / "E_Supplemental_Materials")


def main() -> int:
    args = parse_args()
    output_dir = args.output_dir.resolve()
    staging_dir = output_dir / STAGING_DIR_NAME
    zip_path = output_dir / ZIP_NAME

    require_file(SUBMISSION_README)
    require_dir(SUPPLEMENTAL_DIR)
    require_file(PRESENTATION_PDF)
    require_file(REPORT_PDF)

    output_dir.mkdir(parents=True, exist_ok=True)
    build_staging(staging_dir)
    create_zip(staging_dir, zip_path)

    print(f"Submission zip: {zip_path}")
    print(f"Zip size      : {format_size(zip_path.stat().st_size)}")
    print(
        "Zip size is reported for manual checking; it is not enforced by this script."
    )
    if args.keep_staging:
        print(f"Staging kept  : {staging_dir}")
    else:
        shutil.rmtree(staging_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
