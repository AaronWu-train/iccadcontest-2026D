# Final Report

LaTeX source for final report, packaged around the NeurIPS 2025 template.

## Requirements

- TeX Live, MacTeX, or another full LaTeX distribution (2023 or newer recommended)
- `latexmk` (bundled with most TeX distributions) for convenient compilation

## Build Instructions

Use `latexmk` for a single-pass PDF build:

```bash
latexmk -pdf main.tex
```

While writing, you can enable automatic recompilation on changes:

```bash
latexmk -pdf -pvc main.tex
```

Generated files such as `main.aux`, `main.log`, and the final `main.pdf` will be placed in the repository root. To clean temporary artifacts, run:

```bash
latexmk -c
```

## Project Layout

- `main.tex` – primary entry point for the report; edit this file to update content.
- `main.pdf` – latest compiled PDF output.
- `neurips_2025.tex` / `neurips_2025.pdf` – official template instructions; keep for reference only.
- `neurips_2025.sty` – NeurIPS 2025 style file. **Do not modify.**
