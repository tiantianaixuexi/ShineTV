---
id: contracts.visual-generation
kind: contract
status: current
scope: visual-generation
source_of_truth:
  - src/visual/VideoTypes.h
  - src/visual/VideoProject.h
  - src/visual/SceneToImageBuilder.h
  - src/visual/VideoTaskRunner.h
  - src/visual/GenerationLedger.h
  - src/novel/NovelAssetPipeline.h
last_verified: 2026-09-25
---

# 视觉生成契约

## 分镜图与视频的共同输入

`Shot` 至少包含：标题、提示词、模式、首帧或参考图、角色资产、链式标记、尺寸/帧数/采样参数、seed 和 job 账。是否可提交由 `Shot::IsSubmittable()` 判断；工程级模型/采样配置由 `VideoProject` 提供。

## 硬约束

| 项目 | 约束 | 唯一来源 |
|---|---|---|
| H3 宽高 | 32 的倍数 | `VideoTypes.h` / `VideoProject::Sanitize` |
| H3 帧数 | `n % 17 == 5` | `VideoTypes.h` |
| H3 参考图 | 最多 9 张 | `VideoTypes.h` |
| 分镜图宽高 | 64 的倍数 | `SceneToImageBuilder.h` |
| 默认帧率 | 24 | `VideoTypes.h` |
| 默认分镜 | 832×480、107 帧 | `VideoTypes.h` |

对齐修正在提交前完成，并在 `GenerationDegradation`/warning 中留下前后值；不要在 UI、编译器和校验器各写一套常量。

## 视觉资产状态

`NovelAssetPipeline` 的生产链是 `Front → Turnaround → BaseBody → Wardrobe`。下游可消费状态和可继续派生状态分别由 `IsAssetConsumable`、`IsAssetDerivable` 判定；Canon 状态与生产状态正交。

## 降级契约

允许降级时必须同时提供：

1. 人读 warning；
2. 类型化 `GenerationDegradation`；
3. 任务状态中的降级记录；
4. 必要时写入 `degradations.jsonl` 或章级报告。

缺 checkpoint、无参考图、无 ControlNet、尺寸纠正等情况不能静默变成“成功且无变化”。严格模式由调用方选择，不能由默认值掩盖。

## 多任务账

一个 `Shot` 可以有多条 `ShotJobRecord`：

```text
Asset → SceneImage → Video
```

每条记录保存 `jobId`、类型、状态、文件、错误和时间。状态查询应聚合全部 job，不能用 `jobs.back()` 假设最后一次就是当前完成状态。

## 线程契约

`VideoTaskRunner` 的 `Start`/`Tick`/状态修改在 UI 线程；worker 负责解析、读取、上传、编译和下载；完成回调在 UI 线程。任务中断清空待跑队列，但不把已上传素材伪装成回滚。
