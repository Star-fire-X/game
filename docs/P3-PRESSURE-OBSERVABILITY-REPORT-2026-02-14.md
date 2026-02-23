# P3 压测观测补齐与对比报告（2026-02-14）

## 1. 测试目标

- 补齐 P3 观测项：
  - tick 延迟分位：`p95`、`p99`
  - `effect/guild` 热路径分配计数
- 输出 baseline（legacy 路径）与 current（P3 优化路径）对比结果。

## 2. 压测方法

- 执行命令：
  - `./build-wsl/bin/legend2_tests --gtest_filter='P3PressureObservabilityTest.*'`
- 用例：
  - `P3PressureObservabilityTest.LegacyVsCurrentHotPathMetrics`
- 压测工作负载（单轮）：
  - tick 样本数：`1200`
  - effect 工作集：`1000` 实体，每实体 `12` 个效果
  - guild 查询：每 tick `2048` 次成员判定
- 场景定义：
  - `legacy`：线性扫描 + 每次临时 `vector` 聚合（模拟 P3 前热点路径）
  - `current`：`EffectListComponent` 缓存 API + `GuildComponent` 成员索引（P3 路径）

## 3. 对比结果

| 指标 | Legacy | Current | 变化 |
| --- | ---: | ---: | ---: |
| tick p95 (us) | 4548.05 | 290.10 | -93.62% |
| tick p99 (us) | 5100.32 | 477.08 | -90.65% |
| effect alloc/tick | 3000 | 0 | -100.00% |
| guild alloc/tick | 0 | 0 | 持平 |

## 4. 结论

- tick 分位显著下降：`p95/p99` 均明显优于 legacy。
- effect 热路径分配从每 tick `3000` 次降到 `0`，达到 P3 降分配目标。
- guild 热路径在该工作负载下保持 `0` 分配（legacy 与 current 均为 0）；current 的价值主要体现在成员判定复杂度从线性扫描转为索引查询。

## 5. 备注

- 本报告数据来自 ECS 热路径合成压测（非全链路线上流量回放）。
- 结果已固化为可复现实验，后续可在同一测试用例下持续跟踪回归。
