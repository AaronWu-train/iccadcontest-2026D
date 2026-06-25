# Report Plots

`scripts/plot_optimizer_report.py` generates report-quality A1-A8 comparison
figures plus machine-checkable derived tables. It is intended for formal report
work where every plotted value should be traceable to an input file.

## Generate a run with traces

Aggregate summary figures only require `results.tsv`, but anytime plots need
numeric progress traces:

```sh
CADD0040_PROGRESS_TRACE=1 CADD0040_PROGRESS_STEPS=256 \
  ./scripts/slurm_run_all_optimizers.sh --local
```

For full Slurm runs, use the same environment variables without `--local`.
Trace files are written under:

```text
progress/<optimizer>/seed_<seed>/<testcase>/progress.tsv
```

## Generate report plots

```sh
python3 scripts/plot_optimizer_report.py \
  --run-dir slurm_runs/<run_id> \
  --out-dir slurm_runs/<run_id>/report_plots \
  --baseline isa \
  --gap-thresholds 0.01,0.03,0.05 \
  --time-bins 80 \
  --selected-testcases auto
```

Run the built-in synthetic verification test with:

```sh
python3 scripts/plot_optimizer_report.py --self-test
```

## Outputs

The report directory contains:

- `figures/`: PNG and PDF versions of every generated figure.
- `tables/`: derived TSV tables used by the figures.
- `DERIVED_METRICS.md`: exact definitions and formulas.
- `REPORT_FIGURE_CAPTIONS.md`: figure-by-figure captions, inputs, formulas,
  interpretation notes, and caveats.
- `REPORT_SUMMARY.md`: detected run coverage, warnings, and cautious conclusion
  candidates.
- `RUN_AUDIT.json`: command arguments, Python/platform/package versions, input
  SHA256 hashes, detected columns, canonical column mappings, optimizer order,
  selected testcases, and warnings.

## Derived tables

- `run_manifest.tsv`: every input file, row count, SHA256, parse status, and
  warnings.
- `final_runs_normalized.tsv`: one row per OK optimizer/testcase/seed run with
  final score, best-known score for the testcase, normalized gap, feasibility,
  area, runtime, and timing metrics when available.
- `optimizer_summary.tsv`: per-optimizer feasible rate, best count, normalized
  gap statistics, feasible-area median, and progress timing medians when traces
  exist.
- `per_testcase_gap_matrix.tsv`: testcase x optimizer median normalized gap.
- `win_tie_loss_vs_<baseline>.tsv`: per-testcase median comparison against the
  baseline optimizer.
- `time_to_feasible.tsv`: first feasible timestamp per traced run, when timing
  metrics exist.
- `time_to_gap_threshold.tsv`: first timestamp reaching each normalized-gap
  threshold, when progress traces exist.
- `progress_binned_median_iqr.tsv`: elapsed-time binned median/q25/q75 best
  score and contributing seed count.
- `progress_all_testcases_average.tsv`: elapsed-time binned all-testcase
  average normalized best-score gap. Each testcase contributes its median traced
  seed gap before the cross-testcase average is computed.
- `timing_all_testcases_average.tsv`: elapsed-time binned all-testcase average
  of raw `tns_ss`, `wns_ss`, `tns_ff`, and `wns_ff` timing metrics.
- `normalized_objective_progress.tsv`: elapsed-time binned all-testcase average
  normalized optimization progress for setup slack, hold slack, and area.

Average progress tables and figures use common-horizon truncation: for the
optimizers shown in a figure, trace rows after the shortest available optimizer
elapsed time are excluded before binning. This keeps compared curves equal
length.

Group comparison figures `fig16`-`fig31` also cap the plotted trace data at
`elapsed_sec <= 550`.

## Figures

- `fig1_feasible_rate_by_optimizer`: bar chart of feasible run rate.
- `fig2_median_normalized_gap_by_optimizer`: bar chart of median normalized
  score gap.
- `fig3_best_count_by_optimizer`: bar chart of testcase best-known counts.
- `fig4_normalized_gap_heatmap`: testcase x optimizer normalized gap heatmap.
- `fig5_win_tie_loss_vs_baseline`: stacked horizontal win/tie/loss bars against
  the selected baseline.
- `fig6_cactus_time_to_feasible`: cumulative traced runs reaching feasibility
  over elapsed time.
- `fig7_cactus_time_to_gap_<threshold>`: cumulative traced runs reaching each
  near-best threshold.
- `fig8_selected_testcase_best_score_progress_<testcase>`: selected testcase
  median best-score progress with IQR bands.
- `fig9_selected_testcase_timing_progress_<testcase>`: selected testcase timing
  metric progress with IQR bands.
- `fig10_area_after_feasible_<testcase>`: area progress only after each run
  first reaches feasibility.
- `fig11_all_testcases_average_best_score_progress`: all-testcase average
  best-score progress, plotted as normalized gap so testcase score scales are
  comparable.
- `fig12_all_testcases_average_timing_progress`: all-testcase average timing
  progress with four subplots for `tns_ss`, `wns_ss`, `tns_ff`, and `wns_ff`.
- `fig13_normalized_setup_slack_optimization_average`: normalized setup slack
  optimization averaged across testcases.
- `fig14_normalized_hold_slack_optimization_average`: normalized hold slack
  optimization averaged across testcases.
- `fig15_normalized_area_optimization_average`: normalized area optimization
  averaged across testcases.
- `fig16`-`fig19`: greedy-family comparison for best score, setup slack, hold
  slack, and area.
- `fig20`-`fig23`: random-neighborhood comparison for best score, setup slack,
  hold slack, and area.
- `fig24`-`fig27`: union-pool comparison for best score, setup slack, hold
  slack, and area.
- `fig28`-`fig31`: selected best-four comparison for best score, setup slack,
  hold slack, and area.

## Aggregate vs progress questions

Aggregate summary figures answer final-outcome questions: which optimizer had
the best final score distribution, how often each optimizer was feasible, and
which optimizer won per testcase under the completed run directory.

Progress trace figures answer anytime-behavior questions: how quickly optimizers
improve, reach feasibility, or approach the best-known score. If traces were
enabled for only a subset of runs, progress figures are generated only from that
subset and the coverage warning is written to `REPORT_SUMMARY.md` and
`RUN_AUDIT.json`.
