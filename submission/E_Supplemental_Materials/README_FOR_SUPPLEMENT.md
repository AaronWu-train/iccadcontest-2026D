# 20260618_091340 Experimental Data

This folder contains supplementary experimental data for the ICCAD 2026 Problem
D project.  It is provided to demonstrate project depth through raw performance
datasets, detailed optimizer logs, final clock-tree outputs, and numeric
progress traces.

## Download Raw Experimental Results

The raw experimental results are stored externally because of their size.
Please download them from the [Drive Link](https://drive.google.com/file/d/1aEEa5809AMByTVic1d0iDEWjBdPaHaad/view?usp=sharing)
and extract the archive before reading or reproducing the detailed experimental
results.

## Experiment Scope

- Testcases: `testcase0` through `testcase4`
- Seeds: `2026`, `2027`, `2028`, `2029`, `2030`
- Optimizers: 13 optimizer variants
- Total runs: `13 optimizers * 5 testcases * 5 seeds = 325`
- Time budget: `570s` per run

All rows in `results.tsv` have `STATUS = OK`.

## What to Read First

| File | Description |
|---|---|
| `summary.txt` | Human-readable full summary of the experiment. |
| `results.tsv` | One row per optimizer/testcase/seed run. This is the main raw score dataset. |
| `by_optimizer.tsv` | Per-optimizer average final score and total runtime. |
| `best_by_testcase.tsv` | Best observed optimizer and seed for each testcase. |
| `progress_index.tsv` | Index of progress trace files for each run. |

Recommended review order:

1. Start with `summary.txt`.
2. Use `by_optimizer.tsv` for the overall ranking.
3. Use `best_by_testcase.tsv` to see testcase-specific winners.
4. Use `results.tsv` for per-seed raw scores.
5. Open `logs/`, `outputs/`, and `progress/` for single-run details.

## Directory Structure

```text
logs/<optimizer>/seed_<seed>/testcaseN.log
outputs/<optimizer>/seed_<seed>/testcaseN/modified_clk_tree.structure
progress/<optimizer>/seed_<seed>/testcaseN/progress.tsv
```

Example single-run files:

```text
logs/tabu-random/seed_2027/testcase2.log
outputs/tabu-random/seed_2027/testcase2/modified_clk_tree.structure
progress/tabu-random/seed_2027/testcase2/progress.tsv
```

## Per-Run Data

`logs/` contains final solver logs.  Each log records initial metrics, final
metrics, and final score.  The timing metrics are:

- `tns_ss`: total negative setup slack in the SS corner
- `wns_ss`: worst negative setup slack in the SS corner
- `tns_ff`: total negative hold slack in the FF corner
- `wns_ff`: worst negative hold slack in the FF corner
- `area`: clock-tree buffer area

`outputs/` contains the final modified clock-tree structure emitted by each run.
These files show the resulting buffer topology and flip-flop sinks.

`progress/` contains numeric optimization traces.  Each `progress.tsv` records
step-by-step score, timing, area, phase, event type, candidate policy, and accept
policy.  These files are useful for analyzing convergence and timing-area
trade-offs over time.

## Optimizer Variants

| Optimizer | Candidate policy | Accept/search policy |
|---|---|---|
| `greedy-random` | RandomActionSpace | BestScore greedy |
| `greedy-violation-path` | ViolationPath | BestScore greedy |
| `greedy-upstream-window` | UpstreamWindow | BestScore greedy |
| `greedy-critical-endpoint` | CriticalEndpoint | BestScore greedy |
| `greedy-union-pool` | UnionPool | BestScore greedy |
| `two-step-union-pool` | UnionPool | TwoStepSlackThenScore |
| `sa-sampled-union-pool` | SampledUnionPool | Metropolis SA |
| `isa-sampled-union-pool` | SampledUnionPool | IteratedMetropolis |
| `tabu-union-pool` | UnionPool | TabuBestNonTabu |
| `two-step-random` | RandomActionSpace | TwoStepSlackThenScore |
| `sa-random` | RandomActionSpace | Metropolis SA |
| `isa-random` | RandomActionSpace | IteratedMetropolis |
| `tabu-random` | RandomActionSpace | TabuBestNonTabu |

## Notes

- `progress_index.tsv` stores absolute paths from the original run machine.
  When reading locally, use the corresponding files under this extracted folder.
- The original run also recorded a `FRAMES_JSON` column, but this package only
  includes numeric progress traces, not visual frame JSONs.
- Related supplementary materials in the repository include
  `report/presentation.pdf`, `report/main.pdf`, `docs/experiments.md`, and
  `docs/report-plots.md`.
