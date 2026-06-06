# Optimization 模組（Codex 就近指引）

在此目錄新增或修改 optimizer 時，請遵守以下規則（與根目錄 `AGENTS.md` 合併後，本檔優先）。

## DebugProgress（必守）

演算法 telemetry 一律經 `DebugProgress`，禁止 `std::cerr`：

```cpp
DebugProgress& debug = context.debug_progress;
debug.log([&](std::ostream& os) { os << "...\n"; });
debug.report_if_due(elapsed, best_metrics, baseline_metrics, current_score);
```

完整說明：`docs/rules/debug-progress.md`

## SA / SkewModel

- `DataPathGraph` 唯讀；`ClockTree` 僅在 `materialize()` 時寫回。
- `affected_path_epoch_.size()` == path 數，不是 node 數。
- 新 optimizer 在 `factory.cpp` 註冊；參考 `annealing_optimizer.cpp`、`greedy_optimizer.cpp`、`milp_optimizer.cpp`。

完整說明：`docs/rules/annealing-optimizer.md`、`docs/annealing-optimizer.md`
