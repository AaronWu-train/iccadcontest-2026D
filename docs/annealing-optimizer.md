# Annealing Optimizer 架構說明（給 Agent / 維護者）

本文件記錄 ICCAD 2026 Problem D 模擬退火 optimizer 的設計、模組分工與操作方式。

## 問題與評分

- **允許操作**：在 clock tree 上 **插入 buffer**、**resize 既有 buffer**、**移除自插入的 buffer**（`NEW_BUF_X`）。
- **固定不變**：data path delay（`SS_delay.rpt` / `FF_delay.rpt`）、既有元件名稱與相對順序。
- **評分公式**（`src/evaluation.cpp`，α=β=0.3，γ=0.4）：

  ```
  Score = α[(1-TNS_ss/TNS_ss_ori)+(1-WNS_ss/WNS_ss_ori)]
        + β[(1-TNS_ff/TNS_ff_ori)+(1-WNS_ff/WNS_ff_ori)]
        + γ(1-Area/Area_ori)
  ```

- **Corner**：SS → setup check；FF → hold check。
- **時限**：單一 testcase 10 分鐘（含 I/O）。

## 目錄結構

`SkewModel` 位於 `src/optimization/` 共用層；SA 相關策略仍集中在 `src/optimization/sa/`：

```
src/optimization/
├── optimizer.hpp / factory.hpp / factory.cpp
├── skew_model.hpp / .cpp
└── sa/
    ├── sa_common.hpp / .cpp
    ├── annealing_optimizer.hpp / .cpp   # 單輪 SA（舊版）
    └── iterated_sa_optimizer.hpp / .cpp # 多輪 SA+Greedy（預設）
```

## 模組分工（重要）

| 模組 | 檔案 | 職責 | SA 中是否可變 |
|------|------|------|---------------|
| `DataPathGraph` | `src/datapath_graph.hpp` | 輸入：FF→FF path、固定 data delay、Tclk/setup/hold | 否（唯讀） |
| `ClockTree` | `src/clock_tree.hpp` | 可修改的 clock tree；`insert_buffer` / `resize_buffer` | 最後才寫回 |
| `evaluate()` | `src/evaluation.cpp` | 完整時序評分（ground truth） | — |
| `SkewModel` | `src/optimization/skew_model.*` | optimizer 共用增量計時沙盒 | 是（記憶體內試算） |
| `sa_common` | `src/optimization/sa/sa_common.*` | 共用 move / materialize / best-state helpers | — |
| `AnnealingOptimizer` | `src/optimization/sa/annealing_optimizer.*` | 單輪：warmup + SA + final polish | — |
| `IteratedSaOptimizer` | `src/optimization/sa/iterated_sa_optimizer.*` | 多輪：warmup + (SA ↔ greedy batch) × N + final polish | — |

**`SkewModel` 不是取代 `DataPathGraph`**。它啟動時讀取 `DataPathGraph` + `ClockTree`，在 SA 迴圈內用增量 `apply_delta` 快速試算；結束後才把 best state **materialize** 回 `ClockTree`。

## 資料流

### AnnealingOptimizer（`--optimizer anneal`）

```
AnnealingOptimizer::run
  → SkewModel(clock_tree, data_path_graph, buffer_library)
  → greedy warmup（256 次）
  → SA 迴圈（insert / remove / resize，Metropolis 接受）
  → final greedy polish（64 次）
  → snapshot best state → restore → materialize 到 ClockTree
```

### IteratedSaOptimizer（`--optimizer isa`，預設）

```
IteratedSaOptimizer::run
  → SkewModel(...)
  → greedy warmup（512 次）
  → for round in 1..5:
      → SA phase（剩餘時間均分）
      → greedy batch（最多 48 步，從 best_state 出發）
  → final greedy polish（64 次）
  → materialize 到 ClockTree
```

## SkewModel 重點

- **內部表示**：節點陣列 + 每條 tree edge 上的 `inserted_cell_indices` 串列 + buffer cell 表。
- **增量更新**：`apply_delta(subtree_root, Δss, Δff)` 只更新該子樹 FF 與 incident data path 的 slack；TNS 用 running sum，WNS 用 `multiset`。
- **三種 move**：`Insert`（edge 上 fanout=1 buffer）、`Remove`（移除自插入 buffer）、`Resize`（換 cell，fanout 不變）。
- **已知 bug 修正**：`affected_path_epoch_` 大小必須是 **path 數**（`launch_idx_.size()`），不是 node 數；否則大 testcase 會 crash。

## Optimizer 註冊

| 別名 | Class | 說明 |
|------|-------|------|
| `isa` / `sa2` | `IteratedSaOptimizer` | **預設**，多輪 SA+Greedy |
| `anneal` / `sa` | `AnnealingOptimizer` | 單輪 SA（舊版，保留） |

## DebugProgress

演算法輸出規範見 [`AGENTS.md`](../AGENTS.md) § DebugProgress。

## 參數

- 預設 SA 時間：**540 秒**（`kAnnealingTimeBudget`）。
- 環境變數 **`CADD0040_SA_SECONDS`** 可覆寫（測試用）。
- 預設 optimizer：**`isa`**（factory 別名 `sa2`）。
- RNG seed 固定（可重現）。

## 執行方式

```sh
make release
./build-release/cadd0040 ./testcases/testcase0 ./testcases/testcase0/modified_clk_tree.structure
./build-release/cadd0040 --optimizer isa ./testcases/testcase0 ./output.structure
./build-release/cadd0040 --optimizer anneal ./testcases/testcase0 ./output.structure

# 批次跑全部 testcase
./scripts/run_all_testcases.sh
CADD0040_SA_SECONDS=30 ./scripts/run_all_testcases.sh
```

## 測試

- `tests/test_annealing.cpp`：SkewModel 與 `evaluate()` 一致、兩個 optimizer 分數不劣於 baseline。
- 單元測試會 `setenv("CADD0040_SA_SECONDS", "2")` 縮短 SA 時間。

## 已知限制與改進方向

1. **Greedy warmup** 在大 testcase 上可能偏慢（O(paths × edges × cells)）；可限制 sample 數或只試最小 area cell。
2. **Materialize** 順序：先 resize 原始 buffer，再依 parent→child 順序 `insert_buffer`；插入鏈由 `insert_buffer(parent, downstream, ...)` 逐層建立。
3. **Restore score vs Final score**：model 內分數與 materialize 後 `evaluate()` 可能略有差異；以 `evaluate()` 為準。
4. `tests/test_evaluation.cpp` 中 `score` 預期值 2.1 的測試與目前 α/β/γ 常數可能不一致（既有問題，非本 optimizer 引入）。

## 檔案清單

```
src/optimization/skew_model.hpp
src/optimization/skew_model.cpp
src/optimization/sa/sa_common.hpp
src/optimization/sa/sa_common.cpp
src/optimization/sa/annealing_optimizer.hpp
src/optimization/sa/annealing_optimizer.cpp
src/optimization/sa/iterated_sa_optimizer.hpp
src/optimization/sa/iterated_sa_optimizer.cpp
src/optimization/factory.cpp      # 註冊 isa / sa2 / anneal / sa
src/optimization/factory.hpp      # kDefaultOptimizerName = "isa"
scripts/run_all_testcases.sh
tests/test_annealing.cpp
```
