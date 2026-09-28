---
id: modules.visual-storyboard
kind: reference
status: current
scope: visual
source_of_truth:
  - src/visual/ReferenceLibrary.h
  - src/visual/VideoTypes.h
  - src/visual/VideoProject.h
  - src/visual/VideoTaskRunner.h
  - src/visual/SceneToImageBuilder.h
  - src/visual/NovelShotBridge.h
  - src/novel/NovelVisualStages.h
  - src/novel/NovelAssetPipeline.h
last_verified: 2026-09-25
---

# 视觉资产与分镜

## 责任

`src/visual` 负责从小说实体/场景到可提交生成物的模型和任务执行；`src/novel/NovelVisual*`、`NovelAssetPipeline` 负责小说侧视觉状态和生产阶段。命名空间大多为 `shine::video`，不要按目录名误判。

## 参考图库

`visual::ReferenceLibrary` 以项目根为边界：

```text
assets/refs/       原始/导入图片
assets/refs/refs.json 或模块 manifest
```

公开操作包括 `Load`、`Save`、`Import`、`Bind`、`SetMarkers`、`Remove`、`Resolve`。相对路径通过项目根解析；图片元数据包括尺寸、方向、实体/资产绑定和标记。

## 视觉资产链

`NovelAssetPipeline` 的层顺序：

```text
Front → Turnaround → BaseBody → Wardrobe
```

状态可消费性由生产状态决定，不要求先达到 Canon。`RunAssetPipeline` 返回 `Ready`、`Suspended`、`Degraded` 或 `Failed`，并保留结构化降级说明。默认允许缺依赖时降级；严格模式下缺依赖直接失败/挂起，不能假装成功。

## 叙事分镜

`NovelVisualStages` 把视觉化链拆为：

- V1 `SCENE_BREAKDOWN`：场景七要素 + 镜骨架；
- V2 `DIRECTOR_INTENT`；
- V3 `PERFORMANCE`；
- V4 `SPATIAL`；
- V5 `CAMERA`；
- V6 `TIMELINE`；
- V7 `AUDIO`；
- V8 连续性检查（`NovelContinuity`）；
- V9 叙事分镜（`NovelStoryboard`）；
- V10 提示词；
- V11 出图/出片。

V2–V7 使用统一的阶段执行器；阶段产物写入 `work/ch<NNN>/vNN_*.json`，链式 hash 用于复用。`use_agent_tools` 默认是 `false`，只有显式开启才走 Agent 工具循环。

## 叙事 → 生成桥

`visual::ToGenShot` 是纯函数：

- 输入是已解析的只读 DTO，不直接查询小说库；
- 输出是 `VideoProject` 和 K20/K21 检查/降级账；
- seed 由 `StableShotSeed(sceneTitle, shotId, ord)` 稳定派生；
- 不自动出图，提交由 `VideoTaskRunner` 负责。

## 视频/分镜图模型

`VideoProject` 保存工程级模型段、采样参数和 `Shot` 列表。`Shot` 记录：提示词、首帧/参考图、角色资产、链式标记、尺寸/帧数/采样、seed 和多次 job 账。

硬约束集中在 `VideoTypes.h`：

- H3 尺寸对齐到 32；
- 合法帧数满足 `n % 17 == 5`；
- 参考图最多 9 张；
- 默认 FPS 为 24；
- `Sanitize` 负责纠正并记录前后值。

分镜图 `SceneToImageBuilder` 的尺寸对齐粒度为 64；缺少参考图/ControlNet 时允许走明确记录的降级路径。

## 任务执行

`VideoTaskRunner` 的状态机：

```text
Idle → Resolving → Uploading → Compiling → Submitting
     → Running → ReadingHistory → SavingMedia → Done | Failed
```

- 同一时刻运行一个任务，其他任务按优先级排队：资产 → 分镜图 → 视频；
- 状态由 UI 线程修改；worker 解析、读文件、上传、编译、下载；
- `execution_interrupted` 表示已中断，不等同失败；
- 提交前必须用本机 `/object_info` 校验，object info 未就绪时拒绝提交；
- 降级通过 `GenerationDegradation` 和输出账目保留，不能只返回一个 bool。
