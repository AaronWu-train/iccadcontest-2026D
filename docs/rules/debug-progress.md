# DebugProgress 演算法輸出規範

> Codex 可讀副本，與 `.cursor/rules/debug-progress.mdc` 內容一致。  
> 適用範圍：`src/optimization/**/*`、`src/solver.cpp`

所有 optimizer 與 solver 的**演算法 telemetry** 必須經 `DebugProgress`，不可直接 `std::cerr`。

## 正確寫法

從 `OptimizerContext` 取得 reporter（`Solver` 則用 `DebugProgress::from_environment()`）：

```cpp
DebugProgress& debug = context.debug_progress;

debug.log([&](std::ostream& os) {
    os << "MyOptimizer: baseline score = " << score << '\n';
});

debug.report_if_due(elapsed, best_metrics, baseline_metrics, current_score);
```

## 禁止寫法

```cpp
// ❌ 演算法 progress / summary 不可直接寫 stderr
std::cerr << "MyOptimizer: best score = " << best_score << '\n';
```

## API 分工

| API | 用途 |
|-----|------|
| `debug.log(...)` | phase 切換、baseline / warmup / final summary、batch 完成訊息 |
| `debug.report_if_due(...)` | 長迴圈內週期性輸出（stderr prefix：`Progress`） |

## 啟用條件

- **Release**（`NDEBUG`）：永遠靜默。
- **Debug**：僅在 `CADD0040_DEBUG_PROGRESS=1` 時輸出；間隔由 `CADD0040_DEBUG_PROGRESS_INTERVAL` 控制（預設 30s）。

## 允許直接 `std::cerr` 的例外

- `main.cpp`、`Solver::run()` 的 exception 錯誤訊息。
- `src/debug_progress.cpp` 內部實作。

低階函式庫失敗（例如 `ClockTree::insert_buffer`）不要新增演算法 telemetry；遵循既有 call site 慣例。

## 參考實作

- `src/optimization/sa/annealing_optimizer.cpp`
- `src/optimization/sa/iterated_sa_optimizer.cpp`
- `src/optimization/greedy/greedy_optimizer.cpp`
- `src/optimization/milp/milp_optimizer.cpp`
- `src/solver.cpp`
