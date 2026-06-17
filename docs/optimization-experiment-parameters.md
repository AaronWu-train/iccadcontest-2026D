# Optimization Experiment Parameters

This document defines the default experiment matrix and lightweight logging rules.

## Fairness Rules

- Main experiments use the same edit operations: insert buffer, remove inserted buffer, resize buffer.
- Algorithm differences are expressed as `CandidatePolicy` plus `AcceptPolicy`.
- All A1-A9 optimizers use the same default wall-clock budget: `570s`.
- Bounded candidate-set optimizers use the same default candidate cap: `candidate_limit=4096`.
- Shared UnionPool family limits default to `violation_sample_limit=32`,
  `critical_endpoint_limit=32`, `upstream_window_depth=4`, `removal_candidate_limit=512`, and
  `resize_node_limit=1024` where the policy uses that action family.
- `CADD0040_SA_SECONDS` overrides the time budget for every optimizer when no config file is used.
- `--config <file>` loads an INI experiment file. Config values override environment variables.
  The config `optimizer` key overrides CLI `--optimizer`.
- `CADD0040_CHECKPOINT_STEPS` writes the best-so-far tree to the requested output path. Default: `4096`; set `0` to disable.
- Numeric event traces and visual frame traces are off by default.
- Full Slurm runs should keep `CADD0040_DEBUG_PROGRESS=0`, `CADD0040_PROGRESS_TRACE=0`, and `CADD0040_VISUAL_TRACE=0`.

## A1-A9 Matrix

| ID | Descriptive Alias | CandidatePolicy | AcceptPolicy |
|----|-------------------|-----------------|--------------|
| `A1` | `greedy-random` | `RandomActionSpace` | `BestScore` |
| `A2` | `greedy-violation-path` | `ViolationPath` | `BestScore` |
| `A3` | `greedy-upstream-window` | `UpstreamWindow` | `BestScore` |
| `A4` | `greedy-critical-endpoint` | `CriticalEndpoint` | `BestScore` |
| `A5` | `greedy-union-pool` | `UnionPool` | `BestScore` |
| `A6` | `two-step-optimize` | `UnionPool` | `TwoStepSlackThenScore` |
| `A7` | `sa` | `SampledUnionPool` | `Metropolis` |
| `A8` | `isa` | `SampledUnionPool` | `IteratedMetropolis` |
| `A9` | `tabu` | `UnionPool` | `TabuBestNonTabu` |

Numeric aliases are uppercase only. Config sections should use descriptive aliases.
`milp` remains runnable, but it is not part of the A1-A9 default experiment matrix. Old aliases are
not registered.

## Default Parameters

| Optimizer | Time | Main parameters |
|-----------|------|-----------------|
| A1-A5 Greedy | `570s` | `max_steps=4096`, `candidate_limit=4096`, `max_resize_polish_steps=96`, `max_polish_phases=64` |
| A6 TwoStepOptimize | `570s` | `timing_steps=2048`, `score_steps=2048`, total accepted-step cap `4096`, `candidate_limit=4096` |
| A7 SA | `570s` | `greedy_warmup=256`, `final_greedy_polish=32`, `initial_temperature=0.08`, `cooling_factor=0.01`, `restart_stale=2500` |
| A8 ISA | `570s` | `greedy_warmup=256`, `rounds=16`, `round_greedy=16`, `final_greedy_polish=32`, `restart_stale=2500` |
| A9 Tabu | `570s` | `max_steps=4096`, `tabu_tenure=128`, `candidate_limit=4096` |

Action-budget comparison:

- A1-A6 and A9 use a `4096` accepted-step or iteration cap, plus the common `570s` wall-clock cap.
- A7/A8 SA phases are time-driven and sample one `SampledUnionPool` proposal per SA iteration.
  Their bounded greedy warmup/polish batches use `256` and `32` steps by default.

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

Telemetry terms:

| Purpose | Switch | Output | Use |
|---------|--------|--------|-----|
| Human debug status | `CADD0040_DEBUG_PROGRESS=1` | stderr only | Watch a local debug run |
| Numeric event trace | `CADD0040_PROGRESS_TRACE=1` | `progress.tsv` | Plot score/time curves |
| Visual frame trace | `CADD0040_VISUAL_TRACE=1` | `frames.json` | Animate or inspect clock-tree moves |

`DebugProgress` is not a file trace. It writes human-readable status to stderr
only in debug builds, and only when enabled. Keep it off for full Slurm runs.

For curves, enable the numeric event trace only on selected runs:

```sh
CADD0040_PROGRESS_TRACE=1 CADD0040_PROGRESS_STEPS=256 ./scripts/slurm_run_all_optimizers.sh --local
```

Numeric event trace file:

```text
progress/<optimizer>/<testcase>/progress.tsv
```

Columns:

```text
optimizer testcase step elapsed_sec phase round event current_score best_score delta_score
tns_ss wns_ss tns_ff wns_ff area accepted_moves rejected_moves candidate_policy accept_policy
```

Numeric event trace recording rules:

- Candidate-level trials are not recorded.
- Every `CADD0040_PROGRESS_STEPS` logical steps are recorded.
- Phase start/end, best update, restart, and final are always recorded.
- Set `CADD0040_PROGRESS_STEPS=1` only for small visualization runs.

For clock-tree animation frames, enable the visual frame trace only on a few
testcases:

```sh
CADD0040_VISUAL_TRACE=1 CADD0040_VISUAL_TRACE_STEPS=256 OPTIMIZERS="greedy-violation-path" \
    ./scripts/slurm_run_all_optimizers.sh --local
```

Visual frame output:

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

[greedy-union-pool]
max_steps = 2048
candidate_limit = 4096

[two-step-optimize]
timing_steps = 2048
score_steps = 2048
```

Precedence when `--config` is present:

1. Struct defaults from `optimizer_config.hpp`
2. `CADD0040_SA_SECONDS` (if set)
3. Config file global keys (`optimizer`, `seed`, `time_budget_seconds`)
4. Config file optimizer section keys

The config `optimizer` key overrides CLI `--optimizer`.

## Recommended Commands

Smoke all A1-A9 locally:

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
