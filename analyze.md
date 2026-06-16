# Optimizer Analysis / 最佳化演算法分析

本文根據目前程式碼、`docs/` 內的演算法文件，以及 `results/` 內 20260616_131538
這次 Slurm 實驗結果整理。中文版本在前，英文版本在後。

---

# 中文版本

## 1. 分析範圍與資料來源

本分析主要比較 A1-A8 八個主實驗 optimizer：

| ID | Alias | 類型 |
|---|---|---|
| A1 | `greedy-violation-path` | Best-improvement greedy |
| A2 | `sa` | Single-phase simulated annealing |
| A3 | `isa` | Iterated SA + greedy batch |
| A4 | `greedy-critical-endpoint` | Greedy, critical endpoint candidates |
| A5 | `greedy-upstream-window` | Greedy, upstream window candidates |
| A6 | `greedy-repair-recover` | Two-stage greedy objective |
| A7 | `greedy-randomized-rcl` | Randomized greedy RCL |
| A8 | `tabu` | Tabu search |

`visual` 是 trace / 簡報視覺化工具，不是主比較演算法。`milp` 仍可執行，但目前
`docs/optimization-experiment-parameters.md` 明確說它不在 A1-A8 預設矩陣內，因此本文只
簡短補充，不放入主排名。

主要來源：

- `docs/optimization-algorithms.md`
- `docs/optimization-architecture.md`
- `docs/optimization-complexity.md`
- `docs/optimization-experiment-parameters.md`
- `results/report.md`
- `results/results.tsv`
- `results/by_optimizer.tsv`
- `results/best_by_testcase.tsv`
- `src/optimization/*`
- `src/evaluation.cpp`

本次結果設定：

- 8 optimizers x 5 testcases = 40 jobs
- 每個 optimizer 預設時間預算：570 秒
- 40 OK, 0 failed
- 初始 score 都是 0，表中比較的是 final score

目前 score 權重在 `src/evaluation.cpp`：

```text
Score = 0.5 * SS improvement
      + 0.25 * FF improvement
      + 0.25 * Area improvement
```

其中 SS/FF improvement 各自由 TNS/WNS normalize 後相加，area improvement 也是相對
baseline normalize。分數越高越好。

## 2. 共同架構：現在 optimizer 實際怎麼跑

目前架構已經不是舊版 `SkewModel` 為主，而是：

```text
ClockTree      = mutable clock topology
DataPathGraph  = read-only FF-to-FF timing input
TimingState    = incremental timing and score cache
Optimizer      = search policy
```

主 optimizer 都在一個 working `ClockTree` 上套用 reversible `ClockTreeEdit`：

- insert buffer on active edge
- resize active buffer
- remove inserted buffer

原始 contest nodes 不能刪除；只有 optimizer 自己插入的 buffer 能移除。

每個 trial move 的流程大致是：

```text
ClockTree apply edit
TimingState apply edit
compute score
accept or undo
```

`TimingState` 會增量更新 affected subtree 和相關 timing paths，避免每個 candidate 都跑
完整 `evaluate()`。完整 `evaluate()` 仍是 ground truth，但主要用於 baseline/final
validation，不適合放在 hot loop。

這個架構讓各演算法的差異集中在：

- candidate 怎麼產生
- 接受或拒絕 move 的規則
- 是否允許暫時變差
- 是否有 restart / tabu memory / objective staging
- 是否有 area cleanup 或 resize polish

## 3. 總體結果

### 3.1 平均 final score 排名

| Rank | Optimizer | Average final score | Total time |
|---:|---|---:|---:|
| 1 | `tabu` | 1.17529 | 2860s |
| 2 | `greedy-repair-recover` | 1.15719 | 2857s |
| 3 | `greedy-upstream-window` | 1.12174 | 2857s |
| 4 | `isa` | 1.10615 | 2858s |
| 5 | `greedy-randomized-rcl` | 1.04336 | 2026s |
| 6 | `greedy-violation-path` | 1.02022 | 2858s |
| 7 | `sa` | 0.996876 | 2858s |
| 8 | `greedy-critical-endpoint` | 0.880788 | 2858s |

如果每個 testcase 都挑該 testcase 的最佳 optimizer，oracle best average 是：

```text
Average best-by-testcase score = 1.232323
Sum best-by-testcase score     = 6.161615
```

因此即使平均第一的 `tabu`，仍低於 per-testcase oracle：

```text
tabu average gap to oracle = 1.232323 - 1.175291 = 0.057032
```

這代表目前沒有單一演算法能完全支配所有 testcase。

### 3.2 每個 testcase 的最佳演算法

| Testcase | Best optimizer | Best score |
|---|---|---:|
| testcase0 | `isa` | 0.935219 |
| testcase1 | `tabu` | 1.44477 |
| testcase2 | `greedy-violation-path` | 1.52937 |
| testcase3 | `greedy-repair-recover` | 1.36979 |
| testcase4 | `greedy-upstream-window` | 0.882466 |

五個 testcase 分別由五個不同 optimizer 拿下最佳，這是這份結果最重要的訊號。

它代表問題 instance 的 landscape 差異很大：

- 有些 testcase 適合 local greedy 快速收斂。
- 有些 testcase 需要 upstream window 提供更多 placement freedom。
- 有些 testcase 需要先修 timing、再回收 area。
- 有些 testcase 需要 tabu / SA 類 search 允許暫時變差以跳出 local optimum。

### 3.3 平均落後 per-testcase best 的差距

這裡 gap 定義為：

```text
gap = 該 testcase 最佳 final score - 該 optimizer final score
```

越小越好。

| Optimizer | Avg score | Avg gap to testcase best | Max gap | Wins |
|---|---:|---:|---:|---:|
| `tabu` | 1.175291 | 0.057032 | 0.098670 | 1 |
| `greedy-repair-recover` | 1.157195 | 0.075128 | 0.216765 | 1 |
| `greedy-upstream-window` | 1.121742 | 0.110581 | 0.218890 | 1 |
| `isa` | 1.106150 | 0.126173 | 0.232580 | 1 |
| `greedy-randomized-rcl` | 1.043364 | 0.188959 | 0.287870 | 0 |
| `greedy-violation-path` | 1.020225 | 0.212098 | 0.353060 | 1 |
| `sa` | 0.996876 | 0.235447 | 0.334200 | 0 |
| `greedy-critical-endpoint` | 0.880788 | 0.351535 | 0.649432 | 0 |

觀察：

- `tabu` 最穩，平均 gap 最小，而且 max gap 也小於 0.1。
- `greedy-repair-recover` 平均第二，但 testcase4 掉比較多，最大 gap 0.216765。
- `greedy-upstream-window` 在 testcase4 最強，但在 testcase3 落後較多。
- `isa` 贏 testcase0，但在 testcase1/3/4 不如更針對性的 greedy/tabu 策略。
- `greedy-violation-path` 能贏 testcase2，但泛化性弱，testcase0/3/4 gap 大。
- `greedy-critical-endpoint` 在 testcase2 表現接近強者，但 testcase4 明顯失敗。

## 4. 各演算法實際做法、優劣與結果解讀

## A1 `greedy-violation-path`

### 實際做法

這是 best-improvement greedy。每一步會：

1. 找目前最嚴重的 violating paths。
2. 對 SS violation，嘗試在 capture FF incoming edge 插 buffer。
3. 對 FF violation，嘗試在 launch FF incoming edge 插 buffer。
4. 加入 inserted-buffer removal candidates。
5. 後段做 resize polish。
6. 只接受 score delta > 0 的最佳 move。

它不接受變差 move，所以是非常典型的 local greedy。

### 優點

- 搜尋方向很直接：哪條 path 違反，就修該 path endpoint。
- candidate pool 小，邏輯簡單，可解釋性高。
- 在某些 testcase 上非常強，尤其 testcase2 直接拿第一，score 1.52937。

### 缺點

- 太貼近單條 violating path endpoint，placement freedom 不夠。
- 不接受變差 move，容易卡在 local optimum。
- 對 area / global side effect 的處理偏被動，主要靠 removal/resize polish 補救。

### 結果解讀

平均 1.020225，排名第 6，但拿下 testcase2 第一。

這代表它不是泛用最強，但當 testcase 的最佳修法剛好集中在 worst violating path endpoint
附近時，它可以非常有效。testcase0、testcase3、testcase4 分別落後最佳 0.326506、
0.353060、0.313366，顯示它在需要更廣 placement 或 staged cleanup 的 testcase 上不足。

## A2 `sa`

### 實際做法

`sa` 是 single-phase simulated annealing：

1. 先跑 256-step greedy warmup。
2. 進入一段 SA phase。
3. candidate 包含 guided insert、random insert、remove、resize。
4. score 變好一定接受。
5. score 變差依 `exp(delta / temperature)` 機率接受。
6. stale 或落後 best 太多時 restart from best。
7. 最後跑 final greedy polish。

### 優點

- 能接受變差 move，因此理論上能跳出 greedy local optimum。
- guided insert 保留 timing violation 導向，不是完全亂走。
- 實作共用 `sa_search`，有 restart 和 final polish，穩定性比純 random 好。

### 缺點

- 單一 SA phase 的搜尋軌跡容易受早期路徑影響。
- random candidate 相比 best-improvement greedy，單步效率較低。
- 在 570s budget 下，平均仍低於 `isa`、`tabu` 和多數強 greedy variants。

### 結果解讀

平均 0.996876，排名第 7，沒有任何 testcase 第一。

它在 testcase0 有 0.831891，還算接近 `isa`，但在 testcase1/2/3/4 都落後明顯。這表示
單輪 SA 的 exploration 雖然存在，但沒有被有效地轉換成穩定高分。和 `isa` 比較：

```text
isa average 1.106150
sa  average 0.996876
difference 0.109274
```

所以多輪 SA + greedy batch 的結構明顯比單輪 SA 好。

## A3 `isa`

### 實際做法

`isa` 是 iterated SA：

1. 先跑 256-step greedy warmup。
2. 把時間切成 16 rounds。
3. 每 round 跑 SA phase。
4. 每 round 後 restore best，再跑 greedy batch。
5. 最後跑 final greedy polish。

接受規則在 SA phase 中和 `sa` 相同，greedy batch 則只接受正 delta。

### 優點

- 比 `sa` 更能把 exploration 和 exploitation 分開。
- round-based SA 降低單一路徑走壞的風險。
- greedy batch 可以把 SA 找到的區域做局部清理。
- testcase0 拿第一，代表在某些 landscape 中 multi-round stochastic search 很有用。

### 缺點

- 平均只排名第 4，不如 `tabu` 和兩個強 greedy variants。
- 仍然依賴 random move 品質；如果 candidate 空間沒有打中關鍵 edit，時間會被消耗。
- 在 testcase3 落後 `greedy-repair-recover` 0.23258，表示它沒有很好處理 timing-vs-area
  objective staging。

### 結果解讀

平均 1.106150，排名第 4。比 `sa` 明顯好，但不是最穩。

它在 testcase0 是最佳，score 0.935219；testcase2 也有 1.42872，屬於前段。但在
testcase1/3/4 分別落後 0.165920、0.232580、0.131715，顯示 ISA 的 generic exploration
不一定比 problem-specific candidate policy 好。

## A4 `greedy-critical-endpoint`

### 實際做法

這是 greedy framework 的另一種 candidate policy：

1. 把負 slack path 的 violation 累積到 FF endpoint 上。
2. 選 top critical endpoints。
3. 在這些 FF incoming edge 嘗試 insert。
4. 加 removal 和 resize polish。
5. 只接受最佳正 delta move。

### 優點

- 能把多條 path 的壓力集中到同一 endpoint，避免只看單條 worst path。
- 在 testcase2 表現不錯，score 1.46541，距最佳只差 0.06396。
- candidate selection 比完全 upstream window 更小，理論上有較低 trial cost。

### 缺點

- endpoint criticality 不一定等於最佳插入位置。
- 如果真正關鍵位置在 endpoint 上游，這個方法會看不到。
- 對 testcase4 嚴重失敗，score 只有 0.233034，最大 gap 0.649432。

### 結果解讀

平均 0.880788，排名最後。

它的失敗不代表 criticality 沒價值，而是目前 candidate placement 太窄：只挑 endpoint
incoming edge，缺少上游窗口或 objective staging。testcase2 表現接近強者，表示 endpoint
aggregation 在某些 topology 上有效；但 testcase4 顯示它很容易錯過真正需要調整的 clock
tree region。

## A5 `greedy-upstream-window`

### 實際做法

這也是 best-improvement greedy，但 candidate 比 A1 更寬：

1. 找 violating paths。
2. setup violation 從 capture FF 往 root 走。
3. hold violation 從 launch FF 往 root 走。
4. 每個 endpoint 取 upstream window，預設 depth = 4。
5. 在 window 內 edge 嘗試 fanout-1 buffer insertion。
6. 加 removal 和 resize polish。

### 優點

- 比 A1 有更多 placement choices。
- 仍然 bounded，不會掃整棵 tree。
- testcase4 拿第一，score 0.882466。
- 平均 1.121742，排名第 3，是穩定前段方法。

### 缺點

- candidate pool 比 A1 大，單步成本更高。
- 仍然只接受正 delta，會卡 local optimum。
- 在 testcase3 落後最佳 0.21889，表示只有上游窗口仍不如 staged repair/recover。

### 結果解讀

A5 是 A1 的自然強化版，平均顯著優於 A1：

```text
greedy-upstream-window average = 1.121742
greedy-violation-path average  = 1.020225
difference                     = 0.101517
```

但它沒有完全取代 A1，因為 testcase2 反而是 A1 最佳。這代表更大的 candidate window 有時會
因 area/side-effect 或 candidate order 造成不同 local optimum。

## A6 `greedy-repair-recover`

### 實際做法

這個演算法最重要的是 two-stage objective schedule：

Stage 1 timing repair：

- 用 violation-path + upstream-window insert candidates。
- 目標偏向 timing repair objective，不急著最小化 area。

Stage 2 area recovery：

- 嘗試 remove inserted buffers 和 resize。
- 只有在 timing 不變差超過 tolerance，且 score 不下降時才接受 area-saving move。

### 優點

- 把「先修 timing」和「再回收 area」分開，符合 clock tree optimization 的實務直覺。
- 避免主 score 的 area penalty 太早阻止 timing repair。
- testcase3 拿第一，testcase1/2 也非常接近第一。
- 平均 1.157195，排名第 2。

### 缺點

- 如果 testcase 需要在 timing 和 area 之間反覆折衷，而不是單純先修後回收，兩階段策略可能不夠。
- testcase4 落後最佳 0.216765，表示某些 case 需要 upstream placement 或 randomized/tabu escape。
- Stage 1 暫時忽略 area，可能產生需要 Stage 2 清理的過多插入。

### 結果解讀

A6 是目前最值得作為 deterministic baseline 的方法。它只贏 testcase3，但平均第二，且在
testcase0/1/2 都很接近最佳：

```text
testcase0 gap = 0.048056
testcase1 gap = 0.038560
testcase2 gap = 0.072260
testcase3 gap = 0
```

弱點集中在 testcase4。若要改進 A6，最直接方向是把 A5 的 upstream-window cleanup 或 tabu-style
escape 加入 area recovery / late repair。

## A7 `greedy-randomized-rcl`

### 實際做法

RCL = restricted candidate list。

每個 restart 中：

1. 產生 violation-path insert + removal candidates。
2. 評估正 delta candidates。
3. 排序後取 top-k，預設 k=8。
4. 從 top-k 均勻隨機選一個。
5. 多次 restart，追蹤 global best。
6. 最後做 resize polish。

### 優點

- 比純 greedy 更有多樣性，不一定每次都走最大 delta move。
- 多 restart 可以探索不同 local optimum。
- 在 testcase4 排第二，距最佳只差 0.033874。
- 總 runtime 只有 2026s，明顯低於其他 570s x 5 幾乎跑滿的 optimizer。

### 缺點

- Candidate pool 主要仍是 violation-path，placement diversity 沒有 A5/A8 多。
- 只從正 delta top-k 選，不像 SA/tabu 可接受負 delta。
- testcase1/2/3 落後較大，gap 約 0.278 左右。

### 結果解讀

平均 1.043364，排名第 5。它不是最高分，但效率很突出：

- testcase1 runtime 295s
- testcase2 runtime 302s
- testcase3 runtime 349s

這表示它常常提前沒有正 delta candidate 或 restarts 結束，沒有把 570s 全部用完。若在時間很有限的
場景，A7 可能是 cost-effective 選項；若追最高分，candidate pool 需要加入 upstream/critical
或允許有限負 delta。

## A8 `tabu`

### 實際做法

Tabu search 每步建立 mixed candidate pool：

- violation-path insert
- critical-endpoint insert
- upstream-window insert
- inserted-buffer removal
- resize

然後選最高分的 non-tabu candidate。Tabu move 只有在 aspiration 條件下，也就是能刷新 global best
時才允許。接受後該 move key 進入 tabu memory，預設 tenure = 128。

它可以接受比 current state 差的 move，但最後輸出 global best。

### 優點

- Candidate pool 最完整，結合 A1/A4/A5 的 placement 來源。
- Tabu memory 避免立即 cycling。
- 能接受 worse move，比 greedy 更能逃離 local optimum。
- 平均 final score 第一，平均 gap 和 max gap 都最小。
- testcase1 拿第一，其他 testcase 都在前段。

### 缺點

- 實作成本與 candidate 評估成本最高。
- 幾乎吃滿 570s budget。
- 需要調 tabu tenure、candidate limit、resize node limit；參數不佳時可能浪費時間在不好的 neighborhood。
- 沒有在 testcase0/2/3/4 拿第一，代表 generic robust search 未必贏過專門策略。

### 結果解讀

Tabu 是目前最穩的單一 optimizer：

```text
Average score = 1.175291
Average gap   = 0.057032
Max gap       = 0.098670
Wins          = 1 / 5
```

雖然只贏 testcase1，但它每個 testcase 都沒有大爆炸。這和 A4/A1 形成對比：A1/A4 有機會單點很強，
但也可能在某些 testcase 明顯失敗。

如果只能提交一個 optimizer，`tabu` 是目前最合理的選擇。如果允許 per-testcase selection，
則應使用 `best_by_testcase.tsv` 的 ensemble。

## Legacy `milp`

目前 `milp` 是 MILP-inspired heuristic，不是真正 MILP solver。它根據 worst violations 建立候選窗口，
再做 best positive local edit。由於它不在 A1-A8 570s 結果矩陣內，本文不與其他八個演算法量化比較。

從設計上看，它比較像早期 violation-window greedy。它的優點是 candidate focus 清楚；缺點是沒有真正
integer programming 的 global optimality，也沒有 SA/tabu 的 escape mechanism。

## 5. 結果差距的整體解釋

### 5.1 為什麼沒有單一演算法全贏？

Clock tree optimization 的 move 有強烈 side effect：

- 插 buffer 會改善某些 setup/hold，但可能傷另一個 corner。
- 插 buffer 會增加 area。
- resize 可能改善 timing，但也可能放大 fanout-related delay/area tradeoff。
- remove inserted buffer 能回收 area，但可能破壞 timing repair。

不同 testcase 的最佳策略不同：

- testcase0：`isa` 最佳，表示 multi-round stochastic exploration 有價值。
- testcase1：`tabu` 最佳，表示需要 local optimum escape 和 candidate memory。
- testcase2：`greedy-violation-path` 最佳，表示 worst endpoint local repair 剛好非常有效。
- testcase3：`greedy-repair-recover` 最佳，表示 timing-first then area-recovery 的 objective schedule 很重要。
- testcase4：`greedy-upstream-window` 最佳，表示插入位置需要從 endpoint 往上游擴展。

### 5.2 Greedy family 的差距

Greedy variants 的平均分數：

```text
greedy-repair-recover     1.157195
greedy-upstream-window    1.121742
greedy-randomized-rcl     1.043364
greedy-violation-path     1.020225
greedy-critical-endpoint  0.880788
```

結論：

- 單純換 endpoint ranking 不夠，A4 最弱。
- 擴大 candidate placement window 很有效，A5 明顯提升 A1。
- Objective scheduling 很有效，A6 平均最佳 greedy。
- Randomized RCL 提供時間效率與一些 diversity，但 candidate pool 還不夠廣。

### 5.3 SA family 的差距

```text
isa average = 1.106150
sa average  = 0.996876
```

ISA 明顯優於 SA，主要原因是：

- SA 單輪探索容易走到不好的區域。
- ISA 把時間切成 16 rounds，並在每輪後做 greedy batch。
- 多輪 restore best + greedy cleanup 比單一長 SA 更穩。

不過 ISA 仍低於 `tabu` 和 `greedy-repair-recover`，表示 stochastic move 本身還需要更好的 candidate
generation 或 objective-aware repair/recovery。

### 5.4 Robustness vs peak performance

可以把 optimizer 分成兩類：

High peak but unstable:

- `greedy-violation-path`: 贏 testcase2，但 testcase0/3/4 差。
- `greedy-upstream-window`: 贏 testcase4，但 testcase3 差。
- `isa`: 贏 testcase0，但 testcase3 不夠強。

Robust:

- `tabu`: 平均第一，max gap 最小。
- `greedy-repair-recover`: 平均第二，多數 testcase 接近最佳。

Fast but not top:

- `greedy-randomized-rcl`: 總時間較短，但平均分數不在最前。

## 6. 補充分析向度

### 6.1 如果目標是單一提交

建議優先選：

```text
tabu
```

理由：

- 平均最高。
- 每個 testcase 都接近最佳。
- 沒有明顯崩盤 testcase。

備選：

```text
greedy-repair-recover
```

理由是 deterministic、平均第二，尤其 testcase1/2/3 很強。

### 6.2 如果允許 per-testcase 選擇

使用：

```text
testcase0 -> isa
testcase1 -> tabu
testcase2 -> greedy-violation-path
testcase3 -> greedy-repair-recover
testcase4 -> greedy-upstream-window
```

這組平均 1.232323，比單一最佳 `tabu` 的 1.175291 高 0.057032。

### 6.3 下一步改進方向

1. Tabu + repair/recover hybrid

   `tabu` robust，`greedy-repair-recover` 在 testcase3 很強。可以考慮 tabu search 中加入 timing-repair
   objective 或 area-recovery phase。

2. Upstream-window 加入 A6 area recovery

   A5 贏 testcase4，但 A6 平均更好。把 upstream-window candidate 更完整地納入 A6 的兩階段流程，可能改善
   A6 在 testcase4 的弱點。

3. ISA candidate policy 升級

   目前 SA family 的 random/guided moves 不如 tabu mixed candidate pool。可以讓 SA/ISA 的 guided insert
   也採用 violation + critical endpoint + upstream window 混合池，再 random sample。

4. RCL 加 upstream/critical candidate

   A7 目前速度不錯，但 candidate pool 太像 A1。加入 A5/A8 的候選來源，可能保留效率又提升品質。

5. Per-testcase meta selector

   目前五個 testcase 五個 winner。若不能手動指定，可以建立低成本特徵，例如：

   - path count
   - initial SS/FF violation ratio
   - tree depth / fanout distribution
   - baseline area
   - worst slack concentration on endpoints

   用這些特徵選 optimizer 或調參。

## 7. 結論

目前最佳單一 optimizer 是 `tabu`，因為它平均分數最高、最穩，且沒有大幅落後的 testcase。

但目前最重要的結論不是「tabu 永遠最好」，而是：

```text
不同 testcase 需要不同 search bias。
```

`greedy-violation-path`、`isa`、`greedy-repair-recover`、`greedy-upstream-window`、`tabu`
各自都在某個 testcase 拿第一。這代表未來提升分數的方向不是只微調單一演算法，而是把幾個成功因素合併：

- A5 的 upstream placement freedom
- A6 的 timing-first / area-recovery objective schedule
- A8 的 tabu memory 和 worse-move escape
- A3 的 multi-round stochastic exploration

以目前數據看，最有價值的下一步是做 `tabu + repair/recover + upstream-window` 的 hybrid，或做
per-testcase optimizer selector。

---

# English Version

## 1. Scope and Data Sources

This analysis compares the A1-A8 main experiment optimizers:

| ID | Alias | Type |
|---|---|---|
| A1 | `greedy-violation-path` | Best-improvement greedy |
| A2 | `sa` | Single-phase simulated annealing |
| A3 | `isa` | Iterated SA + greedy batch |
| A4 | `greedy-critical-endpoint` | Greedy with critical endpoint candidates |
| A5 | `greedy-upstream-window` | Greedy with upstream-window candidates |
| A6 | `greedy-repair-recover` | Two-stage greedy objective |
| A7 | `greedy-randomized-rcl` | Randomized greedy RCL |
| A8 | `tabu` | Tabu search |

`visual` is a trace and presentation tool, not a main comparison optimizer. `milp` is still
runnable, but the current experiment documentation explicitly keeps it outside the A1-A8 default
matrix, so it is not included in the quantitative ranking.

Sources used:

- `docs/optimization-algorithms.md`
- `docs/optimization-architecture.md`
- `docs/optimization-complexity.md`
- `docs/optimization-experiment-parameters.md`
- `results/report.md`
- `results/results.tsv`
- `results/by_optimizer.tsv`
- `results/best_by_testcase.tsv`
- `src/optimization/*`
- `src/evaluation.cpp`

Run setup:

- 8 optimizers x 5 testcases = 40 jobs
- Default wall-clock budget: 570 seconds per run
- 40 OK, 0 failed
- Initial score is 0 for all rows; final score is compared

The current scoring weights in `src/evaluation.cpp` are:

```text
Score = 0.5 * SS improvement
      + 0.25 * FF improvement
      + 0.25 * Area improvement
```

Higher is better.

## 2. Shared Architecture

The current architecture is:

```text
ClockTree      = mutable clock topology
DataPathGraph  = read-only FF-to-FF timing input
TimingState    = incremental timing and score cache
Optimizer      = search policy
```

Main optimizers mutate a working `ClockTree` through reversible `ClockTreeEdit`s:

- insert buffer on active edge
- resize active buffer
- remove inserted buffer

Original contest nodes cannot be removed. Only optimizer-inserted buffers can be removed.

A typical trial move is:

```text
apply edit to ClockTree
apply edit to TimingState
compute score
accept or undo
```

`TimingState` incrementally updates the affected subtree and affected timing paths. Full
`evaluate()` remains the ground truth, but it is too expensive for every candidate trial.

Therefore, optimizer differences are mostly:

- candidate generation policy
- acceptance rule
- whether worse moves are allowed
- whether restarts, tabu memory, or objective staging exist
- whether area cleanup or resize polish is performed

## 3. Overall Results

### 3.1 Average Final Score Ranking

| Rank | Optimizer | Average final score | Total time |
|---:|---|---:|---:|
| 1 | `tabu` | 1.17529 | 2860s |
| 2 | `greedy-repair-recover` | 1.15719 | 2857s |
| 3 | `greedy-upstream-window` | 1.12174 | 2857s |
| 4 | `isa` | 1.10615 | 2858s |
| 5 | `greedy-randomized-rcl` | 1.04336 | 2026s |
| 6 | `greedy-violation-path` | 1.02022 | 2858s |
| 7 | `sa` | 0.996876 | 2858s |
| 8 | `greedy-critical-endpoint` | 0.880788 | 2858s |

If we choose the best optimizer per testcase, the oracle ensemble score is:

```text
Average best-by-testcase score = 1.232323
Sum best-by-testcase score     = 6.161615
```

Even the best single optimizer, `tabu`, is below that oracle:

```text
tabu average gap to oracle = 1.232323 - 1.175291 = 0.057032
```

This means no single optimizer dominates every testcase.

### 3.2 Best Optimizer Per Testcase

| Testcase | Best optimizer | Best score |
|---|---|---:|
| testcase0 | `isa` | 0.935219 |
| testcase1 | `tabu` | 1.44477 |
| testcase2 | `greedy-violation-path` | 1.52937 |
| testcase3 | `greedy-repair-recover` | 1.36979 |
| testcase4 | `greedy-upstream-window` | 0.882466 |

The five testcases are won by five different optimizers. This is the most important signal in the
results.

The search landscape varies across instances:

- Some cases favor direct local greedy repair.
- Some need more upstream placement freedom.
- Some benefit from timing repair followed by area recovery.
- Some require tabu/SA-style escape from local optima.

### 3.3 Average Gap to Per-Testcase Best

Gap is defined as:

```text
gap = best final score on the testcase - optimizer final score
```

Smaller is better.

| Optimizer | Avg score | Avg gap to testcase best | Max gap | Wins |
|---|---:|---:|---:|---:|
| `tabu` | 1.175291 | 0.057032 | 0.098670 | 1 |
| `greedy-repair-recover` | 1.157195 | 0.075128 | 0.216765 | 1 |
| `greedy-upstream-window` | 1.121742 | 0.110581 | 0.218890 | 1 |
| `isa` | 1.106150 | 0.126173 | 0.232580 | 1 |
| `greedy-randomized-rcl` | 1.043364 | 0.188959 | 0.287870 | 0 |
| `greedy-violation-path` | 1.020225 | 0.212098 | 0.353060 | 1 |
| `sa` | 0.996876 | 0.235447 | 0.334200 | 0 |
| `greedy-critical-endpoint` | 0.880788 | 0.351535 | 0.649432 | 0 |

Observations:

- `tabu` is the most robust: smallest average gap and max gap below 0.1.
- `greedy-repair-recover` is second overall, but weak on testcase4.
- `greedy-upstream-window` wins testcase4, but is weaker on testcase3.
- `isa` wins testcase0, but is weaker than specialized greedy/tabu methods on testcase1/3/4.
- `greedy-violation-path` wins testcase2 but generalizes poorly.
- `greedy-critical-endpoint` is close on testcase2 but fails badly on testcase4.

## 4. Algorithm-by-Algorithm Analysis

## A1 `greedy-violation-path`

### What it does

This is best-improvement greedy:

1. Sort currently violating paths by severity.
2. For SS violations, try insertion on the capture FF incoming edge.
3. For FF violations, try insertion on the launch FF incoming edge.
4. Add inserted-buffer removal candidates.
5. Run resize polish.
6. Accept only the best positive score delta.

It does not accept worsening moves.

### Strengths

- Very direct search bias: repair the endpoint of the bad path.
- Small candidate pool and high interpretability.
- Extremely strong when the best edit is near the worst violated endpoint.
- Wins testcase2 with score 1.52937.

### Weaknesses

- Placement freedom is too narrow.
- It easily gets stuck in local optima.
- Area and global side effects are handled mostly through later cleanup.

### Result interpretation

Average score is 1.020225, rank 6, but it wins testcase2. It is a high-peak but unstable method.
Large gaps on testcase0/3/4 show that endpoint-only repair is insufficient when the best insertion
point lies upstream or when area/timing tradeoffs require staged optimization.

## A2 `sa`

### What it does

Single-phase simulated annealing:

1. Run 256 greedy warmup steps.
2. Run one SA phase.
3. Candidate moves include guided insert, random insert, remove, and resize.
4. Accept improving moves.
5. Accept worsening moves with probability `exp(delta / temperature)`.
6. Restart from best when stale or too far below best.
7. Run final greedy polish.

### Strengths

- Can escape greedy local optima.
- Guided insert keeps the search somewhat timing-aware.
- Restart and polish improve stability compared with pure random search.

### Weaknesses

- A single long SA trajectory can still drift into weak regions.
- Random trial moves have lower per-step efficiency than best-improvement greedy.
- Under the 570s budget, it is weaker than ISA, tabu, and several greedy variants.

### Result interpretation

Average score is 0.996876, rank 7, with no testcase wins. It is notably worse than ISA:

```text
isa average = 1.106150
sa  average = 0.996876
difference  = 0.109274
```

The conclusion is that plain single-phase SA is not enough; the multi-round structure and greedy
batches in ISA matter.

## A3 `isa`

### What it does

Iterated SA:

1. Run 256 greedy warmup steps.
2. Split the time budget into 16 rounds.
3. Run one SA phase per round.
4. Restore best and run a greedy batch after each round.
5. Run final greedy polish.

SA phases use the same Metropolis acceptance rule as A2. Greedy batches accept only positive
deltas.

### Strengths

- Better balance between exploration and exploitation than single SA.
- Round-based search reduces dependence on one trajectory.
- Greedy batches clean up obvious local improvements.
- Wins testcase0.

### Weaknesses

- Average rank is only 4, below tabu and the strongest greedy methods.
- Still depends on random move quality.
- Performs poorly on testcase3 relative to repair/recover, suggesting it lacks objective staging.

### Result interpretation

Average score is 1.106150, rank 4. It is clearly stronger than SA but not the most robust method.
It wins testcase0, but has sizable gaps on testcase1/3/4. Generic stochastic exploration does not
always beat problem-specific candidate policies.

## A4 `greedy-critical-endpoint`

### What it does

This is the greedy framework with a different candidate policy:

1. Accumulate negative slack severity onto FF endpoints.
2. Select top critical endpoints.
3. Try insertion on each selected FF incoming edge.
4. Add removal and resize polish.
5. Accept the best positive delta.

### Strengths

- Aggregates multiple path violations onto shared endpoints.
- Good on testcase2, only 0.06396 below the best.
- Smaller candidate set than upstream-window search.

### Weaknesses

- Endpoint criticality does not guarantee the best insertion location.
- It misses upstream placement opportunities.
- It fails badly on testcase4.

### Result interpretation

Average score is 0.880788, last place. The idea is not useless, but the placement model is too
narrow. It can work when endpoint aggregation matches the topology, as on testcase2, but it is
fragile.

## A5 `greedy-upstream-window`

### What it does

Best-improvement greedy with a wider candidate window:

1. Sort violating paths.
2. For setup violations, walk upstream from the capture FF.
3. For hold violations, walk upstream from the launch FF.
4. Try insertions on edges within a bounded upstream window, depth 4 by default.
5. Add removal and resize polish.

### Strengths

- More placement freedom than A1.
- Still bounded; it does not scan the whole tree.
- Wins testcase4.
- Average rank 3.

### Weaknesses

- Larger candidate pool and higher per-step cost than A1.
- Still greedy-only, so it can get stuck.
- Weaker than repair/recover on testcase3.

### Result interpretation

A5 is a strong upgrade over A1 on average:

```text
greedy-upstream-window average = 1.121742
greedy-violation-path average  = 1.020225
difference                     = 0.101517
```

However, A1 still wins testcase2, so a larger candidate window is not universally better.

## A6 `greedy-repair-recover`

### What it does

Two-stage objective schedule:

Stage 1: timing repair

- Use violation-path and upstream-window insert candidates.
- Optimize a timing-focused objective.
- Do not prioritize area too early.

Stage 2: area recovery

- Try removing inserted buffers and resizing buffers.
- Accept area-saving moves only if timing does not worsen beyond tolerance and score does not drop.

### Strengths

- Matches the practical intuition: repair timing first, then recover area.
- Avoids area penalty blocking necessary timing repair too early.
- Wins testcase3.
- Strong on testcase0/1/2 as well.
- Average rank 2.

### Weaknesses

- Two-stage flow may be too rigid when timing and area must be traded repeatedly.
- Weak on testcase4.
- Stage 1 can create many insertions that Stage 2 must clean up.

### Result interpretation

This is the best deterministic baseline. It wins testcase3 and is close on testcase0/1/2. Its main
weakness is testcase4, where the gap is 0.216765. A natural next step is combining this objective
schedule with stronger upstream or tabu-style exploration.

## A7 `greedy-randomized-rcl`

### What it does

RCL means restricted candidate list:

1. Build violation-path insert and removal candidates.
2. Score positive-delta candidates.
3. Keep top-k, with k=8 by default.
4. Randomly select one candidate from top-k.
5. Repeat across multiple restarts.
6. Run final resize polish.

### Strengths

- More diverse than deterministic greedy.
- Multiple restarts explore different local optima.
- Second best on testcase4, only 0.033874 behind the winner.
- Uses much less total runtime than most other optimizers.

### Weaknesses

- Candidate pool is still close to A1.
- It only chooses among positive moves; unlike SA/tabu, it cannot take negative steps.
- Large gaps on testcase1/2/3.

### Result interpretation

Average score is 1.043364, rank 5. It is not a top scorer, but it is cost-effective. It often
finishes before using the full 570s budget. For higher score, it likely needs upstream/critical
candidates or limited worse-move acceptance.

## A8 `tabu`

### What it does

Tabu search builds a mixed candidate pool:

- violation-path insert
- critical-endpoint insert
- upstream-window insert
- inserted-buffer removal
- resize

It chooses the best non-tabu candidate. A tabu move can be used only by aspiration when it improves
the global best. Accepted moves enter tabu memory with tenure 128. The optimizer may accept a move
that worsens the current state, but final output is the global best state.

### Strengths

- Most complete candidate pool.
- Tabu memory reduces cycling.
- Can escape local optima by accepting worse moves.
- Highest average final score.
- Smallest average gap and max gap.
- Wins testcase1 and stays near best on all others.

### Weaknesses

- Highest implementation and candidate-evaluation cost.
- Uses nearly the full 570s budget.
- Parameter sensitive: tenure, candidate limit, and resize node limit matter.
- Does not win testcase0/2/3/4, so specialized methods can still beat it.

### Result interpretation

Tabu is the most robust single optimizer:

```text
Average score = 1.175291
Average gap   = 0.057032
Max gap       = 0.098670
Wins          = 1 / 5
```

If only one optimizer can be submitted, `tabu` is currently the safest choice. If per-testcase
selection is allowed, the oracle ensemble is better.

## Legacy `milp`

The current `milp` is a MILP-inspired heuristic, not a true MILP solver. It builds a candidate
window around worst violations and applies the best positive local edit. Since it is outside the
A1-A8 570s matrix, it is not ranked quantitatively here.

Conceptually, it is closer to an early violation-window greedy method: focused and explainable, but
without true mathematical-programming optimality or SA/tabu-style escape.

## 5. Explaining the Result Gaps

### 5.1 Why no single optimizer wins all cases

Clock-tree edits have strong side effects:

- Inserting a buffer may improve one setup/hold path but hurt another corner.
- Inserting a buffer increases area.
- Resizing changes timing and area in fanout-dependent ways.
- Removing inserted buffers recovers area but can undo timing repair.

The best bias differs by testcase:

- testcase0: `isa` wins, so multi-round stochastic exploration helps.
- testcase1: `tabu` wins, so local-optimum escape and memory help.
- testcase2: `greedy-violation-path` wins, so direct endpoint repair is sufficient.
- testcase3: `greedy-repair-recover` wins, so objective staging matters.
- testcase4: `greedy-upstream-window` wins, so upstream placement freedom matters.

### 5.2 Greedy-family differences

Average scores:

```text
greedy-repair-recover     1.157195
greedy-upstream-window    1.121742
greedy-randomized-rcl     1.043364
greedy-violation-path     1.020225
greedy-critical-endpoint  0.880788
```

Conclusions:

- Endpoint ranking alone is not enough; A4 is weakest.
- Expanding placement to an upstream window is useful; A5 improves significantly over A1.
- Objective scheduling is very useful; A6 is the best greedy method.
- Randomized RCL improves diversity and runtime, but its candidate pool is still too narrow.

### 5.3 SA-family differences

```text
isa average = 1.106150
sa average  = 0.996876
```

ISA is clearly better because:

- one long SA phase is fragile;
- ISA splits the budget into 16 rounds;
- each round is followed by greedy cleanup;
- restore-best behavior prevents long drift.

However, ISA is still below tabu and repair/recover, which suggests SA candidate generation needs
stronger problem-specific structure.

### 5.4 Robustness vs peak performance

High peak but unstable:

- `greedy-violation-path`: wins testcase2, weak on testcase0/3/4.
- `greedy-upstream-window`: wins testcase4, weak on testcase3.
- `isa`: wins testcase0, weaker on testcase3.

Robust:

- `tabu`: highest average, smallest max gap.
- `greedy-repair-recover`: second average and close on most testcases.

Fast but not top:

- `greedy-randomized-rcl`: shorter total runtime, but not top score.

## 6. Additional Angles

### 6.1 If only one optimizer can be submitted

Prefer:

```text
tabu
```

Reasons:

- highest average score;
- smallest average and maximum gap;
- no catastrophic testcase.

Backup:

```text
greedy-repair-recover
```

It is deterministic, second overall, and very strong on testcase1/2/3.

### 6.2 If per-testcase selection is allowed

Use:

```text
testcase0 -> isa
testcase1 -> tabu
testcase2 -> greedy-violation-path
testcase3 -> greedy-repair-recover
testcase4 -> greedy-upstream-window
```

This gives average 1.232323, which is 0.057032 above the best single optimizer, `tabu`.

### 6.3 Next improvement directions

1. Tabu + repair/recover hybrid

   `tabu` is robust, while `greedy-repair-recover` is very strong on testcase3. A hybrid could add
   timing-repair objective staging or area-recovery phases to tabu.

2. Add upstream-window logic to A6 area recovery

   A5 wins testcase4 while A6 is better on average. Combining A5 placement freedom with A6 staging
   may reduce A6's testcase4 weakness.

3. Upgrade ISA candidate policy

   SA/ISA currently use random and guided moves. They may benefit from the mixed candidate pool used
   by tabu: violation path + critical endpoint + upstream window.

4. Add upstream/critical candidates to RCL

   A7 is efficient but candidate-limited. Adding A5/A8 candidate sources may improve score without
   losing all runtime advantage.

5. Build a per-testcase meta selector

   Useful low-cost features:

   - path count
   - initial SS/FF violation ratio
   - tree depth and fanout distribution
   - baseline area
   - whether violations concentrate on a few endpoints

## 7. Conclusion

The best single optimizer today is `tabu`: it has the highest average score and the best
robustness profile.

The larger conclusion is:

```text
Different testcases need different search biases.
```

`greedy-violation-path`, `isa`, `greedy-repair-recover`, `greedy-upstream-window`, and `tabu` each
win one testcase. The next improvement should probably combine the successful ingredients:

- upstream placement freedom from A5;
- timing-first / area-recovery objective scheduling from A6;
- tabu memory and worse-move escape from A8;
- multi-round stochastic exploration from A3.

Based on the current data, the most valuable next steps are either a
`tabu + repair/recover + upstream-window` hybrid or a per-testcase optimizer selector.

---

# 追加分析：Setup / Hold / Area 三向度與混用方案

本節是根據同一批 `results/progress/<optimizer>/<testcase>/progress.tsv` 的 final metrics
再拆分出三個 score component。拆分方式和 `src/evaluation.cpp` 一致：

```text
setup_component = improvement(tns_ss) + improvement(wns_ss)
hold_component  = improvement(tns_ff) + improvement(wns_ff)
area_component  = improvement(area)

final_score = 0.5 * setup_component
            + 0.25 * hold_component
            + 0.25 * area_component
```

注意：

- setup_component 對應 SS corner setup timing。
- hold_component 對應 FF corner hold timing。
- area_component 若為負值，代表 final area 比 baseline 大。
- setup / hold component 最高可接近 2，因為 TNS 和 WNS 各有一個 normalize improvement。

## 中文：三向度平均表

| Optimizer | Avg score | Setup / SS | Hold / FF | Area component | Avg final area ratio |
|---|---:|---:|---:|---:|---:|
| `tabu` | 1.175291 | 1.454729 | 1.793373 | -0.001667 | 1.001667 |
| `greedy-repair-recover` | 1.157196 | 1.557660 | 1.665355 | -0.151893 | 1.151893 |
| `greedy-upstream-window` | 1.121741 | 1.367227 | 1.808853 | -0.056342 | 1.056342 |
| `isa` | 1.106148 | 1.478663 | 1.800768 | -0.333501 | 1.333501 |
| `greedy-randomized-rcl` | 1.043363 | 1.219835 | 1.790834 | -0.057051 | 1.057051 |
| `greedy-violation-path` | 1.020223 | 1.322925 | 1.438062 | -0.003021 | 1.003021 |
| `sa` | 0.996875 | 1.366993 | 1.807473 | -0.553957 | 1.553957 |
| `greedy-critical-endpoint` | 0.880787 | 1.127255 | 1.305426 | -0.036788 | 1.036788 |

## 中文：三向度觀察

### 1. `tabu` 的強項是 balance，不是單一向度最高

`tabu` 的 setup component 不是最高，hold component 也不是最高，但 area component 幾乎為 0：

```text
setup = 1.454729
hold  = 1.793373
area  = -0.001667
```

這代表 `tabu` 的主要優勢是「timing 改善夠好，同時幾乎不付 area 代價」。它平均分數第一，
原因不是暴力把 timing 修到最好，而是避免了 SA/ISA 那種大量插 buffer 導致 area penalty 的問題。

### 2. `greedy-repair-recover` 是 setup 最強，但 area 成本較高

`greedy-repair-recover` 的 setup component 平均最高：

```text
setup = 1.557660
```

這符合它的設計：Stage 1 先修 timing，不急著考慮 area。但它的 area component 是：

```text
area = -0.151893
avg final area ratio = 1.151893
```

也就是平均 final area 約增加 15.19%。它仍然排名第二，表示 setup repair 的收益大於 area penalty。
不過 testcase4 失利也和這個結構有關：若該 testcase 需要很精準的 placement 或 area control，
two-stage repair 可能過度插入或修錯區域。

### 3. `greedy-upstream-window` 是 hold 最強之一，area 控制也好

`greedy-upstream-window` 的 hold component 最高：

```text
hold = 1.808853
area = -0.056342
```

它比 A1 多了 upstream placement freedom，因此能更有效調整 clock arrival / skew，尤其對 hold
有幫助；同時 area ratio 只增加約 5.63%，比 SA/ISA 保守很多。

這解釋它為什麼能贏 testcase4：testcase4 的最佳策略似乎需要沿 endpoint 上游找插入位置，而不是只在
endpoint incoming edge 上動手。

### 4. `isa` 和 `sa` timing 很強，但 area penalty 太重

`isa`：

```text
setup = 1.478663
hold  = 1.800768
area  = -0.333501
avg final area ratio = 1.333501
```

`sa`：

```text
setup = 1.366993
hold  = 1.807473
area  = -0.553957
avg final area ratio = 1.553957
```

兩者 hold 都很強，但 area penalty 明顯。尤其 `sa` 平均 final area 增加約 55.40%，這幾乎直接解釋
它為什麼排名第 7：timing improvement 被 area penalty 吃掉。

`isa` 比 `sa` 好，除了 multi-round + greedy batch，也因為 area penalty 小很多：

```text
isa area component = -0.333501
sa  area component = -0.553957
```

但 `isa` 仍比 `tabu` 多很多 area 成本，所以平均分數被壓低。

### 5. `greedy-violation-path` 幾乎不增加 area，但 hold/setup 上限不足

`greedy-violation-path` 的 area control 很好：

```text
area = -0.003021
avg final area ratio = 1.003021
```

但 timing component 明顯弱於前段 optimizer：

```text
setup = 1.322925
hold  = 1.438062
```

這代表 A1 不是因為 area 爆掉而輸，而是 candidate placement 太窄，沒有找到足夠好的 timing repair。
它在 testcase2 能贏，是因為 testcase2 的有效修法剛好和 worst violating endpoint 很吻合。

### 6. `greedy-critical-endpoint` 的問題是 setup/hold 都不夠

`greedy-critical-endpoint` area control 不差：

```text
area = -0.036788
```

但 setup / hold 都偏低：

```text
setup = 1.127255
hold  = 1.305426
```

所以它不是輸在面積，而是 candidate policy 沒有產生足夠有效的 timing edits。endpoint criticality
可以排序壓力大的 FF，但如果最佳插入點在上游或需要多步組合，A4 很容易看不到。

## 中文：三向度下的演算法定位

| 定位 | 代表 optimizer | 解讀 |
|---|---|---|
| 最均衡 | `tabu` | timing 好，area 幾乎不變，平均最穩 |
| 最強 setup repair | `greedy-repair-recover` | setup component 最高，但 area 增加較多 |
| 最強 hold / upstream skew control | `greedy-upstream-window`, `sa`, `isa` | hold 高，但 SA/ISA area penalty 大 |
| 最保守 area | `greedy-violation-path`, `tabu` | area 幾乎不增加，但 A1 timing 上限不足 |
| 快速但不夠深 | `greedy-randomized-rcl` | area 控制好、hold 強，但 setup 不夠 |
| Candidate policy 不足 | `greedy-critical-endpoint` | area 沒爆，但 setup/hold 都弱 |

---

# 中文：可行混用方案 CLI-style shortlist

以下是我認為可行的混用方案。這些是設計提案，尚未實作。

```text
$ proposed-hybrids

1. a6-tabu-repair-recover
   Idea:
     Use A6 two-stage objective schedule, but replace each stage's pure greedy best-improvement
     with tabu search.
   Combine:
     A6 timing-first / area-recovery objective
     A8 tabu memory and worse-move escape
     A5 upstream-window candidates in timing repair
   Expected optimization:
     Better than A6 on local optima.
     Better than tabu on testcase3-style timing repair.
     Lower area risk than SA/ISA.
   Expected weakness:
     More expensive candidate evaluation.
     More parameters: tabu tenure, timing stage length, area recovery tolerance.
     If tabu accepts too many bad timing moves in Stage 1, area recovery may not fix it.

2. policy-averaged-greedy-seed-then-tabu
   Idea:
     Do not literally average ClockTree outputs, because topology is discrete.
     Instead, average the policy signals from A1/A4/A5:
       A1 violation-path candidates
       A4 critical-endpoint candidates
       A5 upstream-window candidates
     Score candidates by per-policy rank or normalized delta, then seed tabu with the best
     consensus moves.
   Combine:
     A1 direct endpoint repair
     A4 endpoint criticality aggregation
     A5 upstream placement freedom
     A8 tabu final search
   Expected optimization:
     More robust candidate generation than any single greedy policy.
     Should reduce A4's testcase4 failure and A1's testcase0/3/4 failure.
     Gives tabu a better starting tree, reducing time spent on obvious repairs.
   Expected weakness:
     "Average" can blur strong single-policy signals; testcase2 may prefer pure A1.
     Candidate scoring normalization is non-trivial.
     Extra warmup cost before tabu starts.

3. upstream-rcl-repair-polish
   Idea:
     Upgrade A7 RCL by adding A5 upstream-window and A4 critical-endpoint candidates, then finish
     with A6-style area recovery polish.
   Combine:
     A7 top-k randomized selection and restarts
     A5 upstream-window placement
     A4 critical endpoint candidates
     A6 area recovery polish
   Expected optimization:
     Keeps RCL's runtime advantage while improving setup repair.
     More diverse than deterministic greedy.
     Likely improves testcase1/2/3 where current RCL gap is large.
   Expected weakness:
     Larger candidate pool may remove RCL's speed advantage.
     Still only selects positive-delta moves unless explicitly extended.
     Randomness can produce variance; needs fixed seed or multi-run selection.

4. isa-with-repair-recover-guidance
   Idea:
     Keep ISA's multi-round SA, but change guided moves and polish to use A6 repair/recover stages.
     Early rounds favor timing-repair objective; late rounds favor score and area recovery.
   Combine:
     A3 multi-round SA
     A6 staged objective
     A5 upstream-window guided candidates
   Expected optimization:
     Reduces ISA's current area penalty.
     Keeps ability to accept worse moves and escape local optima.
     May improve testcase0 while reducing testcase3/testcase4 gap.
   Expected weakness:
     More complicated temperature/objective schedule.
     If area recovery starts too early, it may block useful timing exploration.
     Runtime may still be high, similar to current ISA.

5. meta-selector-plus-short-polish
   Idea:
     Run cheap early probes or read initial testcase features, choose one main optimizer, then run
     a short common area-recovery polish.
   Combine:
     Per-testcase winner behavior from current results
     A6-style area recovery
   Expected optimization:
     Can approach best-by-testcase oracle without fully running all optimizers.
     Useful because five testcases currently have five different winners.
   Expected weakness:
     Requires feature engineering and validation.
     Wrong selector choice can be worse than just running tabu.
     Less elegant than a single unified optimizer.
```

## 中文：我最推薦先實作哪三個

### 第一優先：`a6-tabu-repair-recover`

理由：

- A6 setup 最強。
- Tabu 平均最穩且 area 最好。
- 兩者互補最明顯。

可能流程：

```text
baseline
Stage 1 timing repair with tabu:
  candidate pool = A1 violation path + A5 upstream window + A4 critical endpoint
  objective = timing_repair_objective
  tabu memory prevents cycling
Stage 2 area recovery with tabu:
  candidates = remove inserted + resize
  objective = area reduction subject to timing tolerance
final restore best score tree
```

### 第二優先：`policy-averaged-greedy-seed-then-tabu`

理由：

- 目前 A1/A4/A5 各自代表不同 candidate bias。
- Tabu 已經有 mixed pool，但還沒有明確做「policy consensus」。
- 可以讓 warmup seed 更好，避免 tabu 前期浪費時間。

這裡的「平均產生解」不應該是平均 clock tree topology，而應該是平均候選策略訊號，例如：

```text
candidate_score =
    0.4 * normalized_delta_from_A1_policy
  + 0.2 * normalized_endpoint_criticality_from_A4_policy
  + 0.4 * normalized_delta_from_A5_policy
```

或用 rank aggregation：

```text
candidate_rank = average(rank_in_A1, rank_in_A4, rank_in_A5)
```

### 第三優先：`isa-with-repair-recover-guidance`

理由：

- ISA 已經贏 testcase0，不能直接丟掉。
- 但 ISA area penalty 過大。
- 加入 A6 的 staged objective，可能保留 stochastic exploration 同時降低 area 爆掉。

---

# Addendum: Setup / Hold / Area Components and Hybrid Designs

This section decomposes the final score into the same components used by `src/evaluation.cpp`:

```text
setup_component = improvement(tns_ss) + improvement(wns_ss)
hold_component  = improvement(tns_ff) + improvement(wns_ff)
area_component  = improvement(area)

final_score = 0.5 * setup_component
            + 0.25 * hold_component
            + 0.25 * area_component
```

Negative area component means final area is larger than baseline.

## English: Average Component Table

| Optimizer | Avg score | Setup / SS | Hold / FF | Area component | Avg final area ratio |
|---|---:|---:|---:|---:|---:|
| `tabu` | 1.175291 | 1.454729 | 1.793373 | -0.001667 | 1.001667 |
| `greedy-repair-recover` | 1.157196 | 1.557660 | 1.665355 | -0.151893 | 1.151893 |
| `greedy-upstream-window` | 1.121741 | 1.367227 | 1.808853 | -0.056342 | 1.056342 |
| `isa` | 1.106148 | 1.478663 | 1.800768 | -0.333501 | 1.333501 |
| `greedy-randomized-rcl` | 1.043363 | 1.219835 | 1.790834 | -0.057051 | 1.057051 |
| `greedy-violation-path` | 1.020223 | 1.322925 | 1.438062 | -0.003021 | 1.003021 |
| `sa` | 0.996875 | 1.366993 | 1.807473 | -0.553957 | 1.553957 |
| `greedy-critical-endpoint` | 0.880787 | 1.127255 | 1.305426 | -0.036788 | 1.036788 |

## English: Component-Level Observations

1. `tabu` wins by balance.

   It does not have the highest setup or hold component, but its area component is almost zero.
   It improves timing while keeping area nearly unchanged.

2. `greedy-repair-recover` is the strongest setup repair method.

   It has the highest setup component, which matches its timing-first stage. Its weakness is area:
   average area grows by about 15.19%.

3. `greedy-upstream-window` is very strong for hold and keeps area moderate.

   It has the highest hold component and only about 5.63% average area growth. This explains its
   testcase4 win.

4. `isa` and `sa` have strong timing but large area penalties.

   `sa` grows area by about 55.40% on average, while `isa` grows area by about 33.35%. Their timing
   improvements are good, but the area term suppresses final score.

5. `greedy-violation-path` preserves area but lacks timing reach.

   It almost does not increase area, but setup and hold components are lower than the leading
   methods.

6. `greedy-critical-endpoint` loses on timing, not area.

   Its area component is acceptable, but both setup and hold components are weak. The candidate
   policy is too narrow.

## English: CLI-style Hybrid Shortlist

```text
$ proposed-hybrids

1. a6-tabu-repair-recover
   Idea:
     Use A6's two-stage objective schedule, but run tabu search inside each stage.
   Expected improvement:
     Combines A6's strong setup repair with tabu's robustness and area control.
   Weakness:
     Higher candidate cost and more parameters.

2. policy-averaged-greedy-seed-then-tabu
   Idea:
     Do not average tree structures directly. Instead, average or aggregate candidate signals from
     A1, A4, and A5, then use the consensus moves to seed tabu.
   Expected improvement:
     Better warmup tree and more robust candidate generation.
   Weakness:
     Policy averaging can dilute a strong single-policy signal, especially on testcase2.

3. upstream-rcl-repair-polish
   Idea:
     Add A5 upstream-window and A4 critical-endpoint candidates to A7 RCL, then finish with A6-style
     area recovery.
   Expected improvement:
     Keeps RCL speed while improving setup reach and area cleanup.
   Weakness:
     Larger candidate pool may reduce the speed advantage.

4. isa-with-repair-recover-guidance
   Idea:
     Keep ISA's multi-round SA, but use A6-like timing repair objective early and area recovery
     late.
   Expected improvement:
     Reduces ISA's area penalty while preserving stochastic exploration.
   Weakness:
     More complex temperature and objective schedule.

5. meta-selector-plus-short-polish
   Idea:
     Use testcase features or short probes to choose a main optimizer, then run common area recovery.
   Expected improvement:
     Approaches the best-by-testcase oracle without running every optimizer fully.
   Weakness:
     Requires reliable feature engineering; wrong selection can underperform tabu.
```

Recommended implementation order:

1. `a6-tabu-repair-recover`
2. `policy-averaged-greedy-seed-then-tabu`
3. `isa-with-repair-recover-guidance`

# 追加實作結果：a6-tabu-repair-recover 本機測試

## 中文：實作內容

我實作了新的 CLI optimizer：

```text
--optimizer a6-tabu-repair-recover
```

這個版本的設計是以 A6 的 `timing repair -> area recovery` 兩階段為主體，並在候選選擇時加入 tabu memory：

- timing repair：沿用 A6 的 violating path + upstream window 候選，預設只接受 timing repair objective 變好的 move。
- area recovery：沿用 A6 的 remove/resize 候選，保留 timing 不變差且總分不倒退的限制。
- tabu memory：最近使用過的 move key 會暫時列入 tabu；若候選能超過歷史 best score，允許 aspiration。
- best-state restore：中途即使接受探索 move，最後仍回復到整段搜尋中分數最高的 clock tree。

我一開始測過較激進版本，也就是 timing 階段允許非改善 move，並且混入 critical-endpoint 候選。該版本平均只有 `0.984899`，明顯輸給 A6 和 tabu。原因是 timing tabu 會吃滿 570 秒，area recovery 幾乎沒有機會執行。後來把預設調成較保守的 v2：不預設加入 critical-endpoint 候選，且 timing repair 只接受 repair objective 改善的 move。

## 中文：完整測試

完整單元測試結果：

```text
make test
45/45 passed
```

`make format` 嘗試執行過，但本機沒有 `clang-format`：

```text
/bin/sh: clang-format: command not found
```

## 中文：v2 本機 benchmark 結果

執行設定：

```text
optimizer  = a6-tabu-repair-recover
seed       = 2026
budget     = CADD0040_SA_SECONDS=570
trace      = CADD0040_PROGRESS_TRACE=1
run dir    = slurm_runs/a6_tabu_repair_recover_v2_parallel_20260616_001
```

五個 testcase 本機平行執行結果：

| Testcase | New final score |
|---|---:|
| testcase0 | 0.668446 |
| testcase1 | 1.390700 |
| testcase2 | 1.422830 |
| testcase3 | 1.322860 |
| testcase4 | 0.608139 |
| Average | 1.082595 |

和既有結果比較：

| Testcase | New | A6 greedy-repair-recover | Delta vs A6 | Tabu | Delta vs Tabu | Old best | Delta vs Old best |
|---|---:|---:|---:|---:|---:|---:|---:|
| testcase0 | 0.668446 | 0.887163 | -0.218717 | 0.884014 | -0.215568 | 0.935219 | -0.266773 |
| testcase1 | 1.390700 | 1.406210 | -0.015510 | 1.444770 | -0.054070 | 1.444770 | -0.054070 |
| testcase2 | 1.422830 | 1.457110 | -0.034280 | 1.469630 | -0.046800 | 1.529370 | -0.106540 |
| testcase3 | 1.322860 | 1.369790 | -0.046930 | 1.271120 | +0.051740 | 1.369790 | -0.046930 |
| testcase4 | 0.608139 | 0.665701 | -0.057562 | 0.806923 | -0.198784 | 0.882466 | -0.274327 |
| Average | 1.082595 | 1.157195 | -0.074600 | 1.175291 | -0.092696 | 1.232323 | -0.149728 |

結論：目前 `a6-tabu-repair-recover` 沒有整體進步。它只在 testcase3 贏過 tabu，但平均仍低於 A6、tabu、以及 old best-by-testcase。

## 中文：三向度分析

v2 的三個未加權 component：

| Testcase | Score | Setup component | Hold component | Area component | Area ratio |
|---|---:|---:|---:|---:|---:|
| testcase0 | 0.668446 | 0.770792 | 1.153644 | -0.021443 | 1.021443 |
| testcase1 | 1.390700 | 1.984499 | 2.000000 | -0.406186 | 1.406186 |
| testcase2 | 1.422830 | 1.985546 | 2.000000 | -0.279755 | 1.279755 |
| testcase3 | 1.322860 | 1.717460 | 2.000000 | -0.143496 | 1.143496 |
| testcase4 | 0.608139 | 0.646210 | 1.145806 | -0.005664 | 1.005664 |
| Average | 1.082595 | 1.420901 | 1.659890 | -0.171309 | 1.171309 |

和先前平均 component 比較：

| Optimizer | Avg score | Setup | Hold | Area |
|---|---:|---:|---:|---:|
| a6-tabu-repair-recover v2 | 1.082595 | 1.420901 | 1.659890 | -0.171309 |
| greedy-repair-recover A6 | 1.157196 | 1.557660 | 1.665355 | -0.151893 |
| tabu A8 | 1.175291 | 1.454729 | 1.793373 | -0.001667 |

觀察：

1. setup 沒有達到 A6 水準。

   A6 的平均 setup component 是 `1.557660`，新 hybrid 是 `1.420901`。代表目前 tabu wrapper 沒有保留 A6 最強的 timing repair 效率。

2. hold 也沒有達到 tabu 水準。

   Tabu 的 hold component 是 `1.793373`，新 hybrid 是 `1.659890`。這表示目前不是單純「A6 setup + tabu hold」的有效疊加。

3. area 比 tabu 差很多，也比 A6 略差。

   新 hybrid area component 是 `-0.171309`，A6 是 `-0.151893`，tabu 幾乎不增加 area (`-0.001667`)。主要原因是多數 testcase 沒有足夠時間進入 area recovery，或 area recovery 步數不足。

4. progress trace 顯示 phase allocation 是主要問題。

   v2 中 testcase0、testcase1、testcase3、testcase4 最後仍在 `timing_repair_tabu`，只有 testcase2 明確進入 `area_recovery_tabu`。因此這個 hybrid 的瓶頸不是 area recovery policy 本身，而是 timing repair 階段仍然太耗時。

## 中文：後續改良方向

目前不建議把 `a6-tabu-repair-recover` 當預設 optimizer。它比較像一個實驗性 hybrid，證明「直接把 tabu memory 插入 A6」不會自然得到 A6 + tabu 的優點。

更合理的下一版方向：

1. 加入 phase time split。

   例如 timing repair 最多用 60%-70% budget，保留固定時間給 area recovery 或 score-based tabu polish。這可以避免 testcase0/testcase4 完全卡在 timing repair。

2. tabu 只放在 polish 階段，而不是包住 A6 主修復階段。

   A6 的 timing repair 很強，應該先讓它用 deterministic greedy 快速修 timing；tabu 比較適合接在後段處理 local optimum。

3. 使用 score-based tabu polish，而不是 timing-objective tabu。

   現在 timing repair objective 仍偏向修 timing，沒有直接處理 area。若後段改用總分 score objective，可能比較接近 A8 的優勢。

4. 避免本機平行 benchmark 對 step count 的影響。

   這次五個 testcase 是平行執行，總 wall time 符合 570 秒 budget，但每個 process 可用 CPU 可能低於既有 results 的單 run 條件。若要做嚴格比較，應再跑一次 sequential 或 Slurm one-core-per-task。

# Implementation Result: a6-tabu-repair-recover Local Test

## English Summary

I implemented the new CLI optimizer:

```text
--optimizer a6-tabu-repair-recover
```

The optimizer uses A6's two-stage repair/recover structure and adds tabu memory to candidate
selection. The final state is always restored to the best score seen during the run.

The first aggressive version allowed non-improving timing moves and mixed in critical-endpoint
candidates by default. That version averaged only `0.984899`, because timing tabu consumed the full
570-second budget and left little room for area recovery. I then changed the default to a more
conservative v2: no critical-endpoint candidates by default, and timing repair only accepts moves
that improve the repair objective.

Unit tests:

```text
make test
45/45 passed
```

`make format` could not run because `clang-format` is not installed locally.

## English Benchmark Result

Run directory:

```text
slurm_runs/a6_tabu_repair_recover_v2_parallel_20260616_001
```

The v2 average final score is `1.082595`, which is below A6 (`1.157195`) and tabu (`1.175291`).
It beats tabu only on testcase3, but does not improve the overall average.

| Testcase | New | A6 | Delta vs A6 | Tabu | Delta vs Tabu | Old best | Delta vs Old best |
|---|---:|---:|---:|---:|---:|---:|---:|
| testcase0 | 0.668446 | 0.887163 | -0.218717 | 0.884014 | -0.215568 | 0.935219 | -0.266773 |
| testcase1 | 1.390700 | 1.406210 | -0.015510 | 1.444770 | -0.054070 | 1.444770 | -0.054070 |
| testcase2 | 1.422830 | 1.457110 | -0.034280 | 1.469630 | -0.046800 | 1.529370 | -0.106540 |
| testcase3 | 1.322860 | 1.369790 | -0.046930 | 1.271120 | +0.051740 | 1.369790 | -0.046930 |
| testcase4 | 0.608139 | 0.665701 | -0.057562 | 0.806923 | -0.198784 | 0.882466 | -0.274327 |
| Average | 1.082595 | 1.157195 | -0.074600 | 1.175291 | -0.092696 | 1.232323 | -0.149728 |

Component comparison:

| Optimizer | Avg score | Setup | Hold | Area |
|---|---:|---:|---:|---:|
| a6-tabu-repair-recover v2 | 1.082595 | 1.420901 | 1.659890 | -0.171309 |
| greedy-repair-recover A6 | 1.157196 | 1.557660 | 1.665355 | -0.151893 |
| tabu A8 | 1.175291 | 1.454729 | 1.793373 | -0.001667 |

Conclusion: the implemented hybrid is valid and testable, but it is not an improvement yet. The
main issue is phase allocation: most runs still spend the full budget in timing repair, so the
hybrid does not recover enough area and does not capture tabu's hold/area advantages.
