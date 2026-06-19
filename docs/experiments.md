# Experiments

This document describes how to run experiments, where outputs are written, and
how to turn run directories into report artifacts.

## Direct Solver Runs

Run one testcase:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure \
  --optimizer tabu-random \
  --seconds 60
```

Enable debug status:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  /tmp/modified_clk_tree.structure \
  --optimizer tabu-random \
  --debug
```

Write numeric progress:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  /tmp/modified_clk_tree.structure \
  --optimizer tabu-random \
  --progress-dir /tmp/cadd0040-progress \
  --progress-steps 256
```

Direct progress output is:

```text
<progress-dir>/progress.tsv
```

## Run All Testcases

`scripts/run_all_testcases.sh` runs one optimizer across every
`testcases/testcase*/` directory and prints a score table.

```sh
./scripts/run_all_testcases.sh
```

Useful variants:

```sh
./scripts/run_all_testcases.sh --optimizer tabu-random
./scripts/run_all_testcases.sh --seconds 60
./scripts/run_all_testcases.sh --debug
./scripts/run_all_testcases.sh --build-dir build-release
```

This script is for quick local checks. It does not create the full Slurm run
directory layout.

## Slurm Optimizer Matrix

`scripts/slurm_run_all_optimizers.sh` runs every selected optimizer against every
`testcases/testcase*/` directory. Default mode submits a Slurm array job. Use
`--local` on a machine without Slurm.

```sh
./scripts/slurm_run_all_optimizers.sh
./scripts/slurm_run_all_optimizers.sh --wait
./scripts/slurm_run_all_optimizers.sh --local
```

Seed controls are CLI arguments, not environment variables:

```sh
./scripts/slurm_run_all_optimizers.sh --seed 5000 --seed-runs 10
```

Optimizer selection is still an environment setting:

```sh
OPTIMIZERS="A1 A6 A10 A13" ./scripts/slurm_run_all_optimizers.sh --local
```

Useful Slurm settings:

```sh
SLURM_TIME=00:11:00 ./scripts/slurm_run_all_optimizers.sh
SLURM_PARTITION=short ./scripts/slurm_run_all_optimizers.sh
SLURM_ACCOUNT=<account> ./scripts/slurm_run_all_optimizers.sh
```

Aggregate an existing run:

```sh
OUTPUT_DIR=slurm_runs/<run-id> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```

## Slurm Output Layout

After aggregation, a Slurm run directory contains:

```text
logs/
outputs/
progress/
results.tsv
by_optimizer.tsv
best_by_testcase.tsv
progress_index.tsv
summary.txt
```

`logs/<optimizer>/seed_<seed>/<testcase>.log` stores stdout/stderr from the
solver process.

`outputs/<optimizer>/seed_<seed>/<testcase>/modified_clk_tree.structure` stores
the generated clock-tree output.

`progress/<optimizer>/seed_<seed>/<testcase>/progress.tsv` stores numeric
optimizer progress. Slurm optimizer runs always create this file and currently
sample every 256 logical steps plus forced events such as phase start/end,
best-update, restart, and final.

`results.tsv` is the row-level run summary. It has one row per
optimizer/testcase/seed run, including initial score, final score, elapsed time,
exit code, and status.

`by_optimizer.tsv` aggregates final scores and total runtime per optimizer.

`best_by_testcase.tsv` records the best final score, optimizer, and seed for
each testcase.

`progress_index.tsv` points to the expected progress TSV for each row in
`results.tsv`.

`summary.txt` is the human-readable aggregate report printed at the end.

Slurm optimizer runs do not produce visualization frames.

## Report Generation

Generate report plots, derived tables, and audit metadata:

```sh
python3 scripts/plot_optimizer_report.py \
  --run-dir slurm_runs/<run-id> \
  --out-dir slurm_runs/<run-id>/report_plots
```

Main output groups:

```text
report_plots/figures/*.png
report_plots/figures/*.pdf
report_plots/tables/*.tsv
report_plots/DERIVED_METRICS.md
report_plots/REPORT_FIGURE_CAPTIONS.md
report_plots/REPORT_SUMMARY.md
report_plots/RUN_AUDIT.json
```

The report tool requires `results.tsv`. Progress-derived figures are skipped
with warnings if matching `progress.tsv` files are missing.

## Config Files

`cadd0040 --config <file>` accepts INI-style `key = value` files. Global keys
include:

- `optimizer`
- `seed`
- `time_budget_seconds`

Per-optimizer sections use optimizer aliases:

```ini
optimizer = isa-sampled-union-pool
seed = 1234
time_budget_seconds = 60

[isa-sampled-union-pool]
rounds = 8
greedy_round_iterations = 32
initial_temperature = 0.08

[greedy-union-pool]
max_steps = 2048
candidate_limit = 4096
```

Precedence:

1. defaults from `src/optimization/optimizer_config.hpp`;
2. `CADD0040_SA_SECONDS`, if set;
3. config global keys;
4. CLI `--seed` and `--seconds`;
5. matching optimizer section keys.

The config `optimizer` key overrides CLI `--optimizer`.

There is no config-sweep runner in the current script set. Use direct
`cadd0040 --config <file>` runs or add a dedicated runner if config sweeps
become part of the normal workflow again.
