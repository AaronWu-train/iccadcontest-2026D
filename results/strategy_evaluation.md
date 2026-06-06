# Strategy Evaluation

All runs below use the saved batch-script outputs in `results/` with
`CADD0040_SA_SECONDS=10` and five testcase scores averaged equally.

## Best Overall Strategy

The best single strategy by average score is **greedy iteration 4**:

| Testcase | Final Score |
|---|---:|
| testcase0 | 0.589894 |
| testcase1 | 1.10147 |
| testcase2 | 1.15627 |
| testcase3 | 0.974637 |
| testcase4 | 0.490999 |
| **Average** | **0.862654** |

Result file: `results/greedy_iteration4_deeper_resize.txt`

## Average Score Ranking

| Rank | Result File | Strategy | Average |
|---:|---|---|---:|
| 1 | `greedy_iteration4_deeper_resize.txt` | greedy | 0.862654 |
| 2 | `greedy_iteration5_deeper_phases.txt` | greedy | 0.857459 |
| 3 | `greedy_iteration3_multiphase.txt` | greedy | 0.844734 |
| 4 | `iteration5_remove_heavy_mix.txt` | anneal / SA | 0.823739 |
| 5 | `greedy_iteration2_resize_polish.txt` | greedy | 0.816386 |
| 6 | `iteration2_restore_metrics_fix.txt` | anneal / SA | 0.810967 |
| 7 | `iteration3_greedy_removal.txt` | anneal / SA | 0.809859 |
| 8 | `iteration4_area_cleanup_only.txt` | anneal / SA | 0.801004 |
| 9 | `milp_iteration1_endpoint_ip.txt` | MILP-inspired | 0.774924 |
| 10 | `milp_iteration3_feasible_removal.txt` | MILP-inspired | 0.774924 |
| 11 | `milp_iteration5_feasible_resize.txt` | MILP-inspired | 0.774924 |
| 12 | `milp_iteration2_removal_candidates.txt` | MILP-inspired | 0.760591 |
| 13 | `greedy_iteration1_basic.txt` | greedy | 0.718025 |
| 14 | `iteration1_remove_undo_fix.txt` | anneal / SA | 0.603123 |
| 15 | `iteration0_baseline.txt` | anneal / SA | 0.602770 |
| 16 | `milp_iteration4_resize_variables.txt` | MILP-inspired | 0.600393 |

## Notes

- MILP-inspired iteration 1/3/5 produced the best observed `testcase4` score: `0.577229`.
- Greedy iteration 4 has the best average and is the best default single-strategy choice from these experiments.
- Per-testcase strategy selection can outperform any single strategy, but this evaluation ranks strategies by equal average over all five testcases.
