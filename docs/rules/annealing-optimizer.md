# Annealing Optimizer 慣例

> Codex 可讀副本，與 `.cursor/rules/annealing-optimizer.mdc` 內容一致。  
> 適用範圍：`src/optimization/**/*`  
> 完整架構說明見 `docs/annealing-optimizer.md`。

## Class hierarchy

- `AnnealingOptimizer : public Optimizer` — 單輪 SA（`src/optimization/sa/`）
- `IteratedSaOptimizer : public Optimizer` — 多輪 SA+Greedy（預設）
- `factory.cpp` 註冊：
  - `"isa"` / `"sa2"` → `IteratedSaOptimizer`（預設）
  - `"anneal"` / `"sa"` → `AnnealingOptimizer`

## 模組角色（勿混淆）

| 模組 | 職責 | SA 中是否可變 |
|------|------|---------------|
| `DataPathGraph` | 唯讀輸入（path、固定 data delay、clock period） | 否 |
| `SkewModel` | SA 內部增量計時沙盒 | 是（記憶體內試算） |
| `ClockTree` | 最後才透過 `materialize()` 寫回 | 結束時才改 |
| `evaluate()` | 完整 ground-truth 評分 | 每步太慢，不用於 SA 內迴圈 |

## SkewModel 不變量

- `affected_path_epoch_.size()` 必須等於 **path 數**（`launch_idx_.size()`），不是 node 數。
- 插入的 buffer 使用 fanout=1 delay table entry；resize 須通過 `cell_supports_fanout`。
- Move 須可透過 `undo_move()` 還原（Metropolis rejection）。

## 調參

- `CADD0040_SA_SECONDS` 可覆寫預設 540s SA budget。
- 批次測試：`./scripts/run_all_testcases.sh`

## Debug 輸出

所有 optimizer 演算法 telemetry 必須使用 `context.debug_progress`（`DebugProgress`），禁止直接 `std::cerr`。詳見 `docs/rules/debug-progress.md`。
