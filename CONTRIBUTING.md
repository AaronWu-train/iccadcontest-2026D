# Contributing

This document collects local development, verification, and final-submission
workflow notes for this repository.

## Build And Test

Build the debug executable in `build/`:

```sh
make build
```

Build the optimized executable in `build-release/`:

```sh
make release
```

Build debug and run Catch2 through CTest:

```sh
make test
```

Run one optimizer across all bundled testcase directories:

```sh
make run
```

Build the Rocky Linux 8 release binary:

```sh
make rocky8
```

The Rocky Linux 8 binary is written to `dist/rocky8/cadd0040`.

## Formatting

Install the formatting hook once per checkout if pre-commit is available:

```sh
pre-commit install
```

Format C++ source and tests:

```sh
make format
```

## Adding Code

New `.cpp` files must be added to the `cadd0040_core` target in
[`CMakeLists.txt`](CMakeLists.txt).

New tests should be added to [`tests/CMakeLists.txt`](tests/CMakeLists.txt).

Optimizer telemetry must use `DebugProgress`; do not write optimizer status
directly to `std::cerr`.

## Final Submission Package

The editable submission inputs are:

```text
submission/A_README.md
submission/E_Supplemental_Materials/
```

`submission/A_README.md` is copied directly into the ZIP as `A_README.md`.
Files placed under `submission/E_Supplemental_Materials/` are copied into the
ZIP supplemental-materials folder.

Build the final course submission ZIP:

```sh
make submission
```

The archive is written to:

```text
dist/submission/B13901011_B13901078_B13901088_B13901104.zip
```

The package script auto-generates these folders at packaging time:

```text
B_Presentation_Slides/
C_Project_Report/
D_Source_Code_and_Testcases/
```

It reports the final ZIP size for manual checking but does not enforce a size
limit.

Keep the generated staging directory for inspection:

```sh
python3 scripts/package_submission.py --keep-staging
```

## Git Workflow

- Use git flow for branches.
- Rebase only your own feature branches; do not rebase shared branches.
- Run `make test` before opening a pull request.
- Use short imperative commit messages, and include the reason for the change.
