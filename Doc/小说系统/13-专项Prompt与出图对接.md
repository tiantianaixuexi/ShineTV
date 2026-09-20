# 13 · 专项 Prompt 与出图对接

> 本卷定义四类专项能力（图像 / 视频 / 音频 / 分镜）的提示规范与按需调用规则，并给出与现有 ComfyUI 能力的对接点。**必须如实写明：ComfyUI 已通过现有会话与分镜图编译器完整接通；小说侧出图后端仍是 stub；「小说分镜 → 生成分镜 → 出图」缺桥。**

## 0. 本卷范围与不做什么

**范围**

- Prompt 路由：什么时候调用哪类专项能力。
- 四类专项的输入/输出规范（图像 / 视频 / 音频 / 分镜）。
- 「Prompt 是派生产物」的落地：版本、状态哈希、失效。
- 与 ComfyUI 的对接点、两个 Comfy 入口的统一。
- 生成侧校验与降级。

**不做**

- 不定义分镜结构 → `12`。
- 不定义映射规则 → `11` §2.3。
- 不定义生成参数契约 → `02` §2.8。
- 不定义 Prompt 文本全文 → `05` §2.1 的 `prompts/`。

## 1. 现有落点

### 1.1 小说侧提示层（`src/novel/NovelVisual.cpp`）

- `prompt_layers` 表（`NovelDb.cpp:118`）；`SetLayer`（`:277-296`，先 DELETE 同 `owner_kind/owner_id/layer` 再 INSERT）、`QueryLayer`（`:298-311`，`ORDER BY version DESC LIMIT 1`）。
- `Assemble`（`:324-467`）**九层**：`base` → `stage` → `scene` → `action` → `camera` → `composition` → `lighting` → `style` → `quality`（`:456-464`），另有 `negative` 单独输出（`:465`）。
- `SetVisualCanon`（`:510-521`）写 `visual_canon_logs`。

### 1.2 小说侧出图（`src/novel/NovelImageGen.cpp` / `NovelImageStore.cpp`）

| 后端 | 行号 | 行为 |
|---|---|---|
| `MockBackend` | `:210-232` | 写 1×1 PNG 占位 |
| `OpenAiImagesBackend` | `:235-343` | `POST /images/generations`（`:253`），支持 b64 与 URL（`:334 net::Download`） |
| **`ComfyStubBackend`** | `:346-355` | **恒返回 `Err("unsupported", "ComfyUI 出图后端将在 P9.2 接入…")`** |
| 工厂 | `:359-368` | `imageBackend`：`openai_images`/`openai` → OpenAI；`comfy` → **stub**；其余 → mock（默认 `mock`，`Settings.h:80`） |

- 任务：`NovelImageStore.cpp:276-438 RunImageJob`（`RUNNING` → `PROPOSED`/`FAILED`）。
- 启发式 checklist（`:36-77`）：`prompt_nonempty` / `file_on_disk` / `size_hint`(≥64) / `cast_hint` / `negative_hint` / `status`。
- 状态机（`:247-252`）：`CANON/NON_CANON/DISCARDED/PROPOSED`，联动 `SetVisualCanon`（`:269-272`）。

### 1.3 ComfyUI 接入（**已完整接通**）

| 能力 | 落点 |
|---|---|
| 会话/队列/健康 | `ComfySession.cpp:60 Instance`、`:539-581 PollHealth`、`:390-436 CheckWsSilence` |
| 端点 | `ComfyClient.cpp:604 /prompt`、`:513 /history/{id}`、`:554 /queue`、`:572 /object_info`、`:636 /interrupt`、`:649 /api/free`、`:662 /api/system_stats`、`:678 /view` |
| 分镜图（SD1.5） | `SceneToImageBuilder.cpp:124`，宽高对齐 **64** |
| 视频（H3） | `H3WorkflowBuilder.cpp:93 BuildH3Workflow` |
| 提交与轮询 | `VideoTaskRunner.cpp:129 Start`、`:365-414 PollQueue`、`:546-558 Tick` |
| 对 Comfy 校验 | `VideoTaskRunner.cpp:115-123` → `ApiGraphValidator.cpp:73` |
| 工作流三来源 | `ComfyWorkflows.cpp:265 / :284 / :301`（`/` → `%2F`，`:23-39`；404 = 空不算错，`:303-305`） |
| UI | `ShotTableView.cpp:999` 生成 / `:1046` 出分镜图 |
| 侧栏面板 | `src/app/panels/comfy/DrawComfySidePanel.cpp` |

### 1.4 两个 Comfy 入口（**必须统一**）

| 入口 | 配置 | UI | 现状 |
|---|---|---|---|
| ① 通用 | `Settings.h:13 comfyBaseUrl` | `DrawSettingsWindow.cpp:72-89`「服务地址」+「保存并连接」→ `ComfySession::SetBaseUrl` | 可用 |
| ② 小说出图 | `Settings.h:80 imageBackend` | `DrawSettingsWindow.cpp:318-334`「出图后端」下拉（`mock`/`openai_images`/`comfy`） | 选 `comfy` → **stub** |

**问题**：两个入口各自为政，用户以为「选了 comfy 就能出图」，实际走的是 stub。这是 G13 的核心。

### 1.5 相关模块

- `MentionResolver.cpp:265 Resolve`：解析 `@image:` / `@char:` / `{{Mixed N}}` → 改写提示词 + `orderedImages`；去重键 `ImageDedupKey`（`:256`）。
- `CharacterAsset.cpp:42/66/85`：角色资产（参考图 + `identityPrompt` + `lockSeed`）的读写。
- `VideoTaskRunner.cpp:175` `POST /upload/image`（改名带时间戳，`:186`）。

## 2. 目标设计

### 2.1 路由规则（按需调用，禁止一次性全部调用）

| 需要什么 | 调用 | 输入 | 触发条件 |
|---|---|---|---|
| 角色/场景/道具概念图 | Image Prompt | 实体 + 视觉状态 | 首次需要该实体的视觉基准 |
| 分镜图（关键帧） | Image Prompt | `NarrativeShot.start_state` | `V9` 之后每镜必调 |
| 视频 | Video Prompt | `Shot` + 起止状态 + 表演 + 镜头 + 时间轴 | 该镜需要视频而非静帧 |
| 音频 | Audio Prompt | 场/镜 + 时间轴 + 对白 + 表演 + 环境 | 该镜有对白/SFX/BGM 需求 |
| 分镜 | Storyboard | Scene + 表演 + 空间 + 镜头 + 时间轴 | `V9`（上游齐备后） |
| 检查 | Review（视觉 / 连续性） | 期望状态 vs 实际 | `V8`（**生成评审已移除**） |

**按需规则**（对应旧文档 `03` 的「最小 Context 原则」）：

| # | 规则 |
|---|---|
| R1 | 总控只维护「有哪些能力」，不持有专项 Prompt |
| R2 | 每镜只调用**实际需要**的专项：无声镜不调 Audio；非视频镜不调 Video |
| R3 | 不发送整章：只发当前镜的 `NarrativeShot` + 相关视觉状态 + 风格约束 |
| R4 | 已存在的 `PromptArtifact` 若 `input_state_hash` 一致 → **直接复用**，不重新生成（`04` §2.5 H4） |

### 2.2 共同输入（所有专项共用）

```
Target            （shot / scene / entity / item）
CharacterVisualState  （按章解析的 visual_states，11 §2.3.2）
SceneVisualState      （scene_visuals）
ItemVisualState       （持有的道具 + 其 visual_assets）
CameraState           （02 §2.7 的 Camera）
Composition / Lighting / Style （camera_defs / composition_defs / lighting_defs / visual_styles）
ContinuityConstraints （上一镜的 end_state；本镜不得违反）
ReferenceImages       （visual_assets.sheet_rel_path，≤9）
StyleHardRules        （world_rule 翻译出的视觉硬约束）
```

**禁止**：把整章正文、全部实体、全部历史放进专项输入。

### 2.3 Image Prompt 规范

**职责**（旧文档 `04` 原文保留）：只负责「根据当前视觉状态生成图像生成 Prompt」。

**不负责**：剧情规划、小说正文、世界状态修改、镜头剧情设计、长期记忆管理。

**Prompt 结构（九段，与 `Assemble` 的九层对应）**：

```
Subject + Action/Pose + Environment + Composition + Camera + Lighting
        + Visual Style + Material/Texture + Atmosphere
        + Continuity Constraints
```

**三类目标各自的重点**：

| 目标 | 重点 |
|---|---|
| Character | `Identity / Face / Hair / Body / Clothing / Accessories / Pose / Expression / Lighting / Camera / Style` |
| Scene | `Architecture / Environment / Time / Weather / Lighting / Objects / Spatial Layout / Camera / Atmosphere` |
| Keyframe | **优先保证**：人物一致、服装一致、道具一致、场景一致、空间一致、镜头一致 |

**禁止**（旧文档 `04` 原文保留）：不得自行创造新角色 / 新服装 / 新道具 / 新剧情 / 新地点，除非任务明确要求。

**输出**（`02` §2.10）：`Image Prompt` + `Negative Prompt` + `Reference Requirements` + `Generation Parameters`。

### 2.4 Audio Prompt 规范

| 层 | 内容 |
|---|---|
| Dialogue | `Character / Text / Emotion / Voice Style / Speed / Volume / Pause / Breath` |
| SFX | 按事件：`Footsteps / Door / Weapon / Clothing / Breathing / Impact / Explosion / Rain / Glass / Fire` |
| Ambient | 按地点：`Forest / City / Tavern / Room / Battlefield / Rain / Wind / Crowd` |
| BGM | 按情绪：`Calm / Suspense / Tension / Fear / Action / Climax / Resolution` |
| Timeline | `00.0s Ambient Start / 01.2s Footstep / 02.0s Dialogue / …`（0.1s 精度，与 `Beat` 对齐） |

**输出**：`Voice Prompt` + `SFX Prompt` + `Ambient Prompt` + `BGM Prompt` + `Mixing Requirements`。

**校验**：音频时间轴的事件时间必须落在某个 `Beat` 区间内（对应 `06` §2.3 K24 的扩展）。

### 2.5 Video Prompt 规范

**核心原则**（旧文档 `05` 原文保留）：

```
Start → Motion → Transformation → End
```

而不是只描述静态画面。

| 段 | 内容 |
|---|---|
| Camera | `Shot Size / Lens / Camera Position / Camera Movement / Speed / Focus / DOF / Framing` |
| Performance | `Expression / Eye Movement / Body Movement / Hand Movement / Walking / Reaction / Timing` |
| Continuity | **必须继承**：`Character / Clothing / Position / Props / Lighting / Environment / Camera Direction` |

**输出**：`Video Prompt` + `Negative/Avoid Prompt` + `Motion Constraints` + `Generation Parameters`。

**硬约束**（旧文档 `05` 原文保留）：不要改变剧情结果、人物身份、服装、道具或空间。

### 2.6 生成对接与两个入口的统一

| # | 决定 |
|---|---|
| U1 | **统一到 `ComfySession`**：所有出图/出视频都必须经 `ComfySession`（`Settings.h:13 comfyBaseUrl`），**废除** `NovelImageGen` 的独立后端抽象（`imageBackend` 只在「用非 Comfy 后端做对照实验」时保留） |
| U2 | 小说侧出图改为：`Assemble` 九层 → `PromptArtifact` → `GenShot`（`11` §2.5）→ `SceneToImageBuilder::BuildSceneToImageWorkflow` → `VideoTaskRunner::Start` → `ComfySession` |
| U3 | `Settings.imageBackend` 的 `comfy` 分支必须**改接真实实现**，或从 UI 下拉中移除（**不允许保留「选了会 unsupported」的选项**） |
| U4 | 出图与出视频共用同一套上传（`POST /upload/image`）、同一套状态轮询（`PollQueue`）、同一套校验（`CheckAgainstComfyUI`） |
| U5 | 所有提交前必须跑 `validate`（`06` §2.3 K19）；`/object_info` 未就绪时**不得静默跳过**，应报「待校验」并阻止提交（修正 `VideoTaskRunner.cpp:115-123` 的现状） |
| U6 | **出图前必须等资产 `status` 就绪或显式降级**：作为 `referenceImages` 的资产需 `status ∈ {SHEET_READY, WARDROBE_READY, READY}`（`11` §2.6.1 S1）；不满足则按 `11` §2.7 选 C（挂起）或 B（降级并记账），**不得静默用空参考图** |
| U7 | **降级必须记账且可见**：`SceneToImageBuilder` 的 `degraded` 标志（缺 checkpoint / 无颜色图走 `EmptyLatentImage` 且 `denoise` 强制 1.0 / 缺 ControlNet，`SceneToImageBuilder.cpp:130/163/173`）与 `Sanitize` 的静默纠正（`VideoProject.cpp:33-107`），都必须写入章级报告（对应 `06` K28） |
| U8 | **一镜多产物已能承载**：`video::Shot.jobs`（`ShotJobRecord[]`）逐次记账，同一镜的正脸/四视图/服装任务**不得互相覆盖状态**（S5 已实现，对应 `06` K29 与 `11` §2.7 W4；队列优先级见 W5） |

**对接分派**：

| 任务 | 编译器 | 对齐 | 降级 |
|---|---|---|---|
| 分镜图（SD1.5） | `SceneToImageBuilder` | 宽高 **64** | 缺 checkpoint → 中文错误不产 JSON；无颜色图 → 纯文生图（`denoise=1.0`）+ 告警；缺 ControlNet → 无 ControlNet + 告警 |
| 视频（H3） | `H3WorkflowBuilder` | 宽高 **32**、帧数 `n % 17 == 5` | 见 `H3WorkflowBuilder.h:7-17` 的节点链 |

### 2.7 Prompt 的版本与失效

| # | 规则 |
|---|---|
| PV1 | 每次生成 `PromptArtifact` 必须带 `input_state_hash`（`04` §2.5） |
| PV2 | 哈希不一致 → **不得复用**（不变式 I9）；必须重新生成并 `version + 1` |
| PV3 | 同 `target_id` 的多个版本全部保留（用于对比与回滚） |
| PV4 | `prompt_layers` 的 `version` 与 `PromptArtifact.version` 必须同步（`QueryLayer` 取 `version DESC LIMIT 1`） |
| PV5 | 生成结果与 Prompt 的关联通过 `PromptArtifact.generation_ref`（`02` §2.10）建立，双向可查 |
| PV6 | **Prompt 不得作为世界状态的一部分被长期依赖**：删掉 Prompt 不影响世界状态的可重建性 |
| PV7 | **组装规则版本参与哈希**（S25）：`ComputeInputStateHash` 的 `chain=visual` 分支含 `prompt_rule_version`（`04` §2.5）。**拼装规则变了就 +1** → 旧产物全部失效重算。不加这条会出现"修了拼装 bug、但旧 prompt 永远被复用"（S25 实测踩到） |

## 3. 差距（逐条：现状 → 缺口 → 影响）

| # | 现状 | 缺口 | 影响 | 严重度 |
|---|---|---|---|---|
| 13-1 | `NovelImageGen` 的 `comfy` 后端是 stub（`:346-355`） | 小说侧出图走不到 Comfy | 出图不可用（G13） | **A** |
| 13-2 | 两个 Comfy 入口（`comfyBaseUrl` / `imageBackend`） | 概念混淆，选 comfy 无效 | 用户误判 | **A** |
| 13-3 | 无 `PromptArtifact` 落库 | 无版本、无哈希、无法复用 | 每次重算，无法追溯 | **A** |
| 13-4 | `Assemble` 九层无契约约束 | 层内容自由 | 输出漂移 | **A** |
| 13-5 | 无「按需调用」规则 | 可能一次性生成全部 | 成本与噪声 | **A** |
| 13-6 | 无 Audio Prompt 落地（无音频生成后端） | 音频层只有规范无实现 | 影视化链不完整 | **A** |
| 13-7 | `ApiGraphValidator` 在 object_info 未就绪时跳过（`VideoTaskRunner.cpp:115-123`） | 假通过 | 提交无效图 | **A** |
| 13-8 | 检验只在提交前，`Sanitize` 静默纠正 | 纠正不可见 | 与 `06` §3 06-8 同 | **B** |
| 13-9 | 无 `ContinuityConstraints` 的显式传递 | 专项 Prompt 无法拿到上一镜状态 | 视觉连续性无保障 | **A** |
| 13-10 | `MentionResolver` 的 `@char:` 依赖 `CharacterAsset` 目录 | 与 `visual_assets` 两套角色资产 | 资产定义分裂 | **A** |
| 13-11 | 原 `V12 GENERATION_REVIEW` 已按需移除 | 出图后无质量门禁 | 由提交前的 K19–K21 机器校验兜底；质量取舍交人工 | **B** |
| 13-12 | `prompt_layers.version` 与 `PromptArtifact.version` 无关联约定 | 版本可能错位 | 取到旧层 | **B** |
| 13-13 | `ValidateApiGraph` 在 `/object_info` 未就绪时**静默跳过**（`VideoTaskRunner.cpp:115-123`） | 校验没跑却被当通过 | 无效图被提交（假通过） | **A** |
| 13-14 | 三种降级（缺 checkpoint / 无颜色图 / 缺 ControlNet）只有 `degraded` 标志，**无「必须进报告」的约束**；`Sanitize` 静默纠正越界值 | 降级不可见 | 无人值守下静默劣化（同 `04` §2.4 的纪律） | **A** |
| 13-15 | 无生成**一致性校验**（同一角色两次生成是否一致） | 角色形象会漂 | 视觉一致性崩（同 11-17） | **A** |

## 4. 验收判据

- [ ] §2.1 的路由表覆盖四类专项 + 检查，且 R1–R4 四条按需规则可判定。
- [ ] §2.2 的共同输入清单**不含**整章正文与全量实体（可 grep 校验）。
- [ ] §2.3 的九段结构与 `Assemble` 的九层**一一对应**（可脚本比对层名集合）。
- [ ] §2.3 的「禁止创造新角色/服装/道具/剧情/地点」原样保留。
- [ ] §2.4 的四层 + 时间轴齐备，且时间轴与 `Beat` 对齐要求明确。
- [ ] §2.5 的 `Start → Motion → Transformation → End` 原则与三段输入齐备。
- [ ] §2.6 的 U1–U5 五条决定明确；**U3 要求消除「选了会 unsupported」的选项**。
- [ ] §2.6 的对接分派表给出两种编译器的对齐粒度（64 / 32）与三条降级分支。
- [ ] §2.7 的 PV1–PV6 六条可判定；PV6 明确「Prompt 不是世界状态」。
- [ ] 全文**不得出现**「ComfyUI 未接入」之类与 §1.3 冲突的表述。
- [ ] §3 的 **15** 条差距在 `00` §3 有编号对应（13-13/13-14 → G21、13-15 → G20）。

## 5. 与其它卷的关系

- 视觉层的表与结构体 → `01` §2.6。
- `NarrativeShot` / `GenShot` / `PromptArtifact` / `GenerationResult` 契约 → `02` §2.7 / §2.8 / §2.10 / §2.11。
- `PROMPT_GEN` / `GENERATION` 在链条中的位置 → `03` §2.2、`11` §2.2。
- `input_state_hash` 与复用 → `04` §2.5。
- Prompt 外置与 `prompts/` 命名 → `05` §2.1。
- K19–K23 校验项 → `06` §2.3。
- 生成结果的落库与状态机 → `07` §2.4。
- 生成预算、超时与失败隔离 → `09` §2.2 / §2.4。
- 叙事状态 → 视觉状态映射、`ToGenShot` 转换 → `11` §2.3 / §2.5。
- 分镜与镜头结构、连续性规则 → `12` §2.7。
