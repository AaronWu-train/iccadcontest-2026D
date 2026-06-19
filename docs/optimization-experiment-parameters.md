# Optimization Experiment Parameters

This document defines the default experiment matrix and lightweight logging rules.

## Fairness Rules

- Main experiments use the same edit operations: insert buffer, remove inserted buffer, resize buffer.
- Algorithm differences are expressed as `CandidatePolicy` plus `AcceptPolicy`.
- All A1-A13 optimizers use the same default wall-clock budget: `570s`.
- Bounded candidate-set optimizers use the same default candidate cap: `candidate_limit=4096`.
- Shared UnionPool family limits default to `violation_sample_limit=32`,
  `critical_endpoint_limit=32`, `upstream_window_depth=4`, `removal_candidate_limit=512`, and
  `resize_node_limit=1024` where the policy uses that action family.
- `CADD0040_SA_SECONDS` overrides the time budget for every optimizer when no config file is used.
- `--config <file>` loads an INI experiment file. Config values override environment variables.
  The config `optimizer` key overrides CLI `--optimizer`.
- Numeric progress traces are optional in direct solver runs and always written by Slurm optimizer runs.
- Slurm optimizer runs do not produce visualization frames; use the dedicated visualization workflow.

## A1-A13 Matrix

| ID | Descriptive Alias | CandidatePolicy | AcceptPolicy |
|----|-------------------|-----------------|--------------|
| `A1` | `greedy-random` | `RandomActionSpace` | `BestScore` |
| `A2` | `greedy-violation-path` | `ViolationPath` | `BestScore` |
| `A3` | `greedy-upstream-window` | `UpstreamWindow` | `BestScore` |
| `A4` | `greedy-critical-endpoint` | `CriticalEndpoint` | `BestScore` |
| `A5` | `greedy-union-pool` | `UnionPool` | `BestScore` |
| `A6` | `two-step-union-pool` | `UnionPool` | `TwoStepSlackThenScore` |
| `A7` | `sa-sampled-union-pool` | `SampledUnionPool` | `Metropolis` |
| `A8` | `isa-sampled-union-pool` | `SampledUnionPool` | `IteratedMetropolis` |
| `A9` | `tabu-union-pool` | `UnionPool` | `TabuBestNonTabu` |
| `A10` | `two-step-random` | `RandomActionSpace` | `TwoStepSlackThenScore` |
| `A11` | `sa-random` | `RandomActionSpace` | `Metropolis` |
| `A12` | `isa-random` | `RandomActionSpace` | `IteratedMetropolis` |
| `A13` | `tabu-random` | `RandomActionSpace` | `TabuBestNonTabu` |

Numeric aliases are uppercase only. Config sections should use descriptive aliases. The short
aliases `two-step-optimize`, `sa`, `isa`, and `tabu` remain registered for A6-A9 compatibility, but
new experiments should prefer the canonical aliases above. `milp` remains runnable, but it is not
part of the A1-A13 default experiment matrix. Old aliases are not registered.

## Default Parameters

| Optimizer | Time | Main parameters |
|-----------|------|-----------------|
| A1-A5 Greedy | `570s` | `max_steps=4096`, `candidate_limit=4096`, `max_resize_polish_steps=96`, `max_polish_phases=64` |
| A6/A10 TwoStepOptimize | `570s` | `timing_steps=2048`, `score_steps=2048`, total accepted-step cap `4096`, `candidate_limit=4096` |
| A7/A11 SA | `570s` | `greedy_warmup=256`, `final_greedy_polish=32`, `initial_temperature=0.08`, `cooling_factor=0.01`, `restart_stale=2500` |
| A8/A12 ISA | `570s` | `greedy_warmup=256`, `rounds=16`, `round_greedy=16`, `final_greedy_polish=32`, `restart_stale=2500` |
| A9/A13 Tabu | `570s` | `max_steps=4096`, `tabu_tenure=128`, `candidate_limit=4096` |

Action-budget comparison:

- A1-A6, A9, A10, and A13 use a `4096` accepted-step or iteration cap, plus the common `570s` wall-clock cap.
- A7/A8/A11/A12 SA phases are time-driven and sample one proposal per SA iteration.
  Their bounded greedy warmup/polish batches use `256` and `32` steps by default.

## What To Record

Default Slurm output is lightweight:

```text
logs/
outputs/
progress/
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
| Human debug status | `--debug` | stderr only | Watch a local debug run |
| Numeric progress trace | Slurm always-on; direct solver uses `--progress-dir` | `progress.tsv` | Plot score/time curves |
| Visualization frames | `CADD0040_VISUAL_TRACE=1` | `frames.json` | Direct solver visualization runs only |

`DebugProgress` is not a file trace. It writes human-readable status to stderr
only in debug builds, and only when enabled. Keep it off for full Slurm runs.

Initial/final metric summaries are stdout build-type output: debug builds print them, while
release builds suppress them.

Slurm optimizer runs always write numeric progress every 256 logical steps.

Numeric progress trace file:

```text
progress/<optimizer>/<testcase>/progress.tsv
```

Columns:

```text
optimizer testcase step elapsed_sec phase round event current_score best_score delta_score
tns_ss wns_ss tns_ff wns_ff area accepted_moves rejected_moves candidate_policy accept_policy
```

Numeric progress trace recording rules:

- Candidate-level trials are not recorded.
- Every 256 logical steps are recorded by Slurm optimizer runs.
- Phase start/end, best update, restart, and final are always recorded.
- Direct solver visualization experiments can use a smaller interval when needed.

## Plotting

Generate report plots and derived tables from a run directory:

```sh
python3 scripts/plot_optimizer_report.py \
  --run-dir slurm_runs/20260616_120000 \
  --out-dir slurm_runs/20260616_120000/report_plots
```

Outputs:

```text
report_plots/figures/*.png
report_plots/figures/*.pdf
report_plots/tables/*.tsv
report_plots/RUN_AUDIT.json
```

If no `progress.tsv` exists, progress-derived figures are skipped with a warning.

## Experiment Config File

Use `--config <file>` for reproducible parameter sweeps without recompiling. Format: INI
`key = value` with optional per-optimizer sections named by alias.

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

[two-step-union-pool]
timing_steps = 2048
score_steps = 2048

[two-step-random]
random_candidate_limit = 512
```

Precedence when `--config` is present:

1. Struct defaults from `optimizer_config.hpp`
2. `CADD0040_SA_SECONDS` (if set)
3. Config file global keys (`optimizer`, `seed`, `time_budget_seconds`)
4. CLI `--seed` and `--seconds`
5. Config file optimizer section keys

The config `optimizer` key overrides CLI `--optimizer`.

## Recommended Commands

Smoke all A1-A13 locally:

```sh
make build
CADD0040_SA_SECONDS=10 ./scripts/slurm_run_all_optimizers.sh --local
```

Main Slurm run:

```sh
make build
SLURM_TIME=00:11:00 ./scripts/slurm_run_all_optimizers.sh
```

Aggregate after Slurm jobs finish:

```sh
OUTPUT_DIR=<printed-output-dir> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```
