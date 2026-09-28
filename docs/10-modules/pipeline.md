---
id: modules.pipeline
kind: reference
status: current
scope: pipeline
source_of_truth:
  - src/pipeline/StageMachine.h
  - src/pipeline/StageMachine.cpp
  - src/pipeline/Runner.h
  - src/pipeline/Runner.cpp
  - src/pipeline/Budget.h
  - src/pipeline/Ledger.h
  - src/pipeline/Checkpoint.h
  - src/pipeline/StopPolicy.h
last_verified: 2026-09-25
---

# 流水线与阶段机

## 两层编排

项目中有两种相关但不同的编排：

1. `src/pipeline`：通用阶段定义、预算、账本、检查点和 `Runner`；
2. `src/novel/NovelRunLoop`：小说章节/连续运行的真实业务循环，复用阶段概念但有自己的预算、停止和报告契约。

不要把通用 `Runner` 的简单 JSON 产物误认为小说生成的完整阶段实现。

## 阶段表

`StageMachine.cpp` 当前定义 29 个阶段：

- 文本链 `T1`–`T17`：章节初始化、前情、世界状态、角色状态、关系、场景、冲突、伏笔、事件序、正文提示、正文、评审、修复、提取、校验、提交、备忘；
- 视觉链 `V0`–`V11`：资产、场景切分、导演意图、表演、空间、镜头、时间轴、声音、连续性、叙事分镜、提示词、出图/出片。

进入规则是顺序相邻：首阶段之前必须是“未进入”，之后必须满足上一阶段退出条件。`TransitionError` 给出可显示的中文原因。

## Runner 行为

`Runner::Configure` 注入项目根、运行模式、阶段执行器和 hash provider。`RunNext`：

1. 检查阶段顺序；
2. 以 `StageCode` 和 hash 判断是否可复用；
3. 消费预算；
4. 执行阶段；
5. 写 `work/<StageCode>.json`；
6. 记录账本并推进阶段。

`RunAll` 聚合执行/复用/停止结果。`SaveCheckpoint` 写 `work/checkpoint.json`，`Resume` 读取下一阶段。该通用 Runner 使用 `std::function` 作为已公开接口；新增热路径回调不要继续扩大这一依赖。

## 预算、账本和停止

- `Budget` 记录调用/资源预算及原因；
- `Ledger` 记录阶段、hash 和输出；
- `StopPolicy` 为通用停止策略；
- `Checkpoint` 只描述可恢复的阶段位置，不替代业务状态快照。

## UI 装配

`src/pages/pipeline/PipelineWorkspace` 组合 Gantt、Ledger、StopReport 视图并调用 `Runner`。UI 只展示状态和发出运行命令；真实阶段执行器由装配层注入。

## 不变量

- 阶段跳转不能跳过前置阶段。
- 已完成且 hash 未变化的阶段可复用；hash 变化必须重新执行。
- 预算不足应停止并给出原因，不应继续调用外部服务。
- 检查点写入失败不能被报告成成功恢复。
