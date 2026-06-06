# Agent Guide (ICCAD 2026 Problem D)

本文件供 **Codex** 自動載入。Cursor 使用 `.cursor/rules/*.mdc`；兩邊內容以 `docs/rules/` 為共同來源。

## 規則文件對照

| 主題 | Codex / 人類可讀 | Cursor rule |
|------|------------------|-------------|
| DebugProgress 輸出 | `docs/rules/debug-progress.md` | `.cursor/rules/debug-progress.mdc` |
| SA optimizer 慣例 | `docs/rules/annealing-optimizer.md` | `.cursor/rules/annealing-optimizer.mdc` |
| SA 完整架構 | `docs/annealing-optimizer.md` | — |
| 編輯 optimizer 時 | `src/optimization/AGENTS.md`（就近覆寫） | — |

修改規則時請**同步更新** `docs/rules/` 與 `.cursor/rules/` 兩份。

## 演算法輸出：一律使用 DebugProgress

**所有 optimizer / solver 的演算法 telemetry**（phase 切換、baseline、warmup、summary、週期性 best score）必須經 `DebugProgress`，**禁止**在 optimizer 內直接 `std::cerr`。

```cpp
DebugProgress& debug = context.debug_progress;

debug.log([&](std::ostream& os) {
    os << "MyOptimizer: baseline score = " << score << '\n';
});

debug.report_if_due(elapsed, best_metrics, baseline_metrics, current_score);
```

- `Solver` 以 `DebugProgress::from_environment()` 建立，再傳入 `OptimizerContext`。
- **Release build**（`NDEBUG`）永遠靜默。
- **Debug build** 需 `CADD0040_DEBUG_PROGRESS=1` 才輸出；間隔用 `CADD0040_DEBUG_PROGRESS_INTERVAL`（預設 30s）。
- `report_if_due` 輸出 prefix 為 `Progress`；`log` 用於 phase / summary 等一次性訊息。

### 允許直接 stderr 的例外

- `main.cpp`、`Solver::run()` 的 exception 錯誤訊息。
- `src/debug_progress.cpp` 內部實作。

新增 optimizer checkpoint 時，請對照既有實作：`annealing_optimizer.cpp`、`greedy_optimizer.cpp`、`milp_optimizer.cpp`。

## 目錄與模組

```
src/optimization/
├── optimizer.hpp          # OptimizerContext { baseline_metrics, debug_progress }
├── factory.cpp            # 註冊 optimizer 別名
├── sa/                    # Simulated annealing
├── greedy/                # Greedy optimizer
└── milp/                  # MILP-inspired optimizer
```

- `DataPathGraph`：唯讀輸入，optimizer 執行中不可改。
- `ClockTree`：optimizer 結束後才 materialize 寫回。
- `SkewModel`：SA / greedy / MILP 共用的增量試算沙盒（見 `sa/skew_model.*`）。

## 開發與驗證

```sh
make build && make test
./scripts/run_all_testcases.sh
CADD0040_DEBUG_PROGRESS=1 ./build/cadd0040 <testcase_dir> <output>
```

- 新 C++ 檔加入 `CMakeLists.txt` 的 `cadd0040_core`。
- 新 optimizer 在 `factory.cpp` 註冊，並遵守 DebugProgress 輸出規範。
- Commit 前會跑 `clang-format`（pre-commit）。
