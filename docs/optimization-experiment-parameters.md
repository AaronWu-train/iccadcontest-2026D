# Optimization Experiment Parameters

This document defines the default experiment matrix and lightweight logging rules.

## Fairness Rules

- Main experiments use the same edit operations: insert buffer, remove inserted buffer, resize buffer.
- Algorithm differences are limited to candidate policy, search framework, objective schedule, and local-optimum escape.
- All A1-A8 optimizers use the same default wall-clock budget: `570s`.
- `CADD0040_SA_SECONDS` overrides the time budget for every optimizer when no config file is used.
- `--config <file>` loads an INI experiment file. Config values override environment variables.
  The config `optimizer` key overrides CLI `--optimizer`.
- `CADD0040_CHECKPOINT_STEPS` writes the best-so-far tree to the requested output path. Default: `4096`; set `0` to disable.
- Progress trace and visual trace are off by default.
- Full Slurm runs should keep `CADD0040_DEBUG_PROGRESS=0`, `CADD0040_PROGRESS_TRACE=0`, and `CADD0040_VISUAL_TRACE=0`.

## A1-A8 Matrix

| ID | Alias | Name | Framework | Main Difference |
|----|-------|------|-----------|-----------------|
| A1 | `greedy-violation-path` | Greedy-ViolationPath | Greedy | Worst violated path endpoint candidate |
| A2 | `sa` | SA | SA | Single SA phase with Metropolis accept |
| A3 | `isa` | ISA | ISA | Multi-round SA plus greedy batch |
| A4 | `greedy-critical-endpoint` | Greedy-CriticalEndpoint | Greedy | Candidate edges from top critical endpoints |
| A5 | `greedy-upstream-window` | Greedy-UpstreamWindow | Greedy | Candidate edges from upstream endpoint window |
| A6 | `greedy-repair-recover` | Greedy-RepairRecover | Greedy | Timing repair stage, then area recovery stage |
| A7 | `greedy-randomized-rcl` | Greedy-RandomizedRCL | Randomized Greedy | Top-k positive move sampling plus restart |
| A8 | `tabu` | Tabu | Tabu Search | Best non-tabu move with aspiration; worse moves allowed |

`milp` remains runnable, but it is not part of the A1-A8 default experiment matrix. Old aliases are
not registered.

## Default Parameters

| Optimizer | Time | Main parameters |
|-----------|------|-----------------|
| A1 Greedy-ViolationPath | `570s` | `max_steps=4096`, `max_resize_polish_steps=96`, `max_polish_phases=64`, `violation_sample_limit=32`, `removal_candidate_limit=512` |
| A2 SA | `570s` | `greedy_warmup=256`, `final_greedy_polish=32`, `initial_temperature=0.08`, `cooling_factor=0.01`, `restart_stale=2500` |
| A3 ISA | `570s` | `greedy_warmup=256`, `rounds=16`, `round_greedy=16`, `final_greedy_polish=32`, `restart_stale=2500` |
| A4 Greedy-CriticalEndpoint | `570s` | `critical_endpoint_limit=32`, `removal_candidate_limit=512`, resize polish same as A1 |
| A5 Greedy-UpstreamWindow | `570s` | `violation_sample_limit=32`, `upstream_window_depth=4`, `removal_candidate_limit=512`, resize polish same as A1 |
| A6 Greedy-RepairRecover | `570s` | `timing_steps=4096`, `area_steps=4096`, `upstream_window_depth=4`, `removal_candidate_limit=1024` |
| A7 Greedy-RandomizedRCL | `570s` | `restart_count=16`, `steps_per_restart=512`, `top_k=8`, `seed=2026` |
| A8 Tabu | `570s` | `max_steps=8192`, `tabu_tenure=128`, `candidate_limit=4096`, `upstream_window_depth=4` |

## What To Record

Default Slurm output is lightweight:

```text
logs/
outputs/
summary.txt
results.tsv
by_optimizer.tsv
best_by_testcase.tsv
progress_index.tsv
```

For report tables, use:

- `results.tsv`: optimizer, testcase, initial score, final score, runtime, status.
- `by_optimizer.tsv`: average final score and total runtime per optimizer.
- `best_by_testcase.tsv`: best optimizer per testcase.

For curves, enable progress trace only on selected runs:

```sh
CADD0040_PROGRESS_TRACE=1 CADD0040_PROGRESS_STEPS=256 ./scripts/slurm_run_all_optimizers.sh --local
```

Progress trace file:

```text
progress/<optimizer>/<testcase>/progress.tsv
```

Columns:

```text
optimizer testcase step elapsed_sec phase round event current_score best_score delta_score
tns_ss wns_ss tns_ff wns_ff area accepted_moves rejected_moves candidate_policy
```

Recording rules:

- Candidate-level trials are not recorded.
- Every `CADD0040_PROGRESS_STEPS` logical steps are recorded.
- Phase start/end, best update, restart, and final are always recorded.
- Set `CADD0040_PROGRESS_STEPS=1` only for small visualization runs.

For clock-tree animation frames, enable visual trace only on a few testcases:

```sh
CADD0040_VISUAL_TRACE=1 CADD0040_VISUAL_TRACE_STEPS=256 OPTIMIZERS="greedy-violation-path" \
    ./scripts/slurm_run_all_optimizers.sh --local
```

Visual output:

```text
traces/<optimizer>/<testcase>/frames.json
```

## Plotting

Generate step-score and time-score plots from a run directory:

```sh
python3 scripts/plot_optimizer_progress.py \
  --run-dir slurm_runs/20260616_120000 \
  --y best_score \
  --out-dir slurm_runs/20260616_120000/plots
```

Outputs:

```text
plots/by_testcase/<testcase>_best_score_vs_step.png
plots/by_testcase/<testcase>_best_score_vs_time.png
plots/by_run/<optimizer>__<testcase>_phases.png
```

If no `progress.tsv` exists, the script prints a clear message and exits successfully.

## Experiment Config File

Use `--config <file>` for reproducible parameter sweeps without recompiling. Format: INI
`key = value` with optional per-optimizer sections named by alias.

```ini
optimizer = isa
seed = 1234
time_budget_seconds = 60

[isa]
rounds = 8
greedy_round_iterations = 32
initial_temperature = 0.08

[greedy-randomized-rcl]
restart_count = 16
steps_per_restart = 512
top_k = 8
```

Precedence when `--config` is present:

1. Struct defaults from `optimizer_config.hpp`
2. `CADD0040_SA_SECONDS` (if set)
3. Config file global keys (`optimizer`, `seed`, `time_budget_seconds`)
4. Config file optimizer section keys

The config `optimizer` key overrides CLI `--optimizer`.

## Recommended Commands

Smoke all A1-A8 locally:

```sh
make release
CADD0040_SA_SECONDS=10 ./scripts/slurm_run_all_optimizers.sh --local
```

Main Slurm run:

```sh
make release
SLURM_TIME=00:11:00 ./scripts/slurm_run_all_optimizers.sh
```

Aggregate after Slurm jobs finish:

```sh
OUTPUT_DIR=<printed-output-dir> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```
