---
id: refactor.workflow-ui-plan
kind: plan
status: draft
created: 2026-11-23
source_of_truth:
  - webui/src/views/Workflow.jsx
  - webui/src/components/WorkflowCanvas.jsx
  - webui/src/data/workflow.js
  - src/ui/imgui/pages/Page_Workflow.cpp
---

# 节点式视频工作流页面 —— 实施计划与 UI 方案

> 本页回答两件事：**为什么这么设计**（多方案对比 + 推荐方案的逐条推导）、
> **怎么一步步做出来**（每个 Step 的文件、内容、验收判据）。
>
> 视觉基准仍遵循 [`design-spec.md`](design-spec.md)：31 色 token、`.vw` 栅格、
> 画布页 `.canvas-page` + `float-panel` 浮层范式。本文只新增，**不删改任何既有页面**。

---

## 0. 目标与范围

### 0.1 一句话目标

在 ShineTV Studio 现有外壳里**新增第 8 个工作区「工作流」**，把
「剧情梗概 → 镜头设计 → 配音 → 字幕 → 剪辑」抽象成一张可自由连线的节点图，
并提供 Skill 自然语言入口，让 Agent 能一句话拉起整条视频生产线。

### 0.2 四条核心要求的落点

| 要求 | 落在哪里 | 验证方式 |
|---|---|---|
| ① 抽象节点（脚本/分镜/画面/视频/音频） | `workflow.js` 的 `NODE_TYPES`（11 种）+ 每个节点 `inputs/outputs` 端口定义 | 端口类型矩阵自检 |
| ② 统一链路（梗概→分镜→配音→字幕→剪辑） | 初始图 `DEFAULT_GRAPH` 就是这条链；画布内一条连线到底 | 节点图 12 节点 / 15 连线 |
| ③ 角色一致性（三视图 + 多角度锁定） | `charref` 节点 + 下游节点「角色锁」徽章 + 冲突检测 | 锁定冲突探针 |
| ④ Skill 接口（自然语言自动拉流程） | 顶部指令条 + `planWorkflow()` 解析器 + 生成反馈流 | 指令 → 12 节点自动生成 |

### 0.3 明确不做（本轮）

- 不删、不改 7 个既有工作区（总控/小说/资产/分镜/出图/出片/画廊）。
- 不改 `shine_core` 业务层（零 Qt、零 ImGui 的约定保持）。
- 不实现真实 AI 推理：页面是**设计稿 + 可交互原型**，数据来自 `workflow.js` 的
  确定性 mock；真实执行走既有 `pipeline` / `comfy` 接入点，接口在 §4.6 预留。
- `backdrop-filter` 毛玻璃、`box-shadow` 两处降级照 `design-spec.md` §9.3 接受。

---

## 1. 前置事实核查（2026-11-23 实测）

写计划前先核对源码，避免「以为有其实没有」。以下每条都读过了：

| 事实 | 证据 | 对本设计的意义 |
|---|---|---|
| 外壳有 **7 个工作区** | `src/ui/imgui/pages/Shell.h:30` `enum class Workspace` + `kWorkspaceCount = 7` | 新增第 8 个要同时改枚举、计数、图标/标签/副标题、分发 switch、导航栏 |
| 外壳固定档 | `Shell_Layout.h`：顶栏 52 / 活动栏 56 / 侧栏 240 / 检查器 280 / 底栏 190 / 状态栏 28 / 面包屑 38 | 新页必须在这套栅格里工作，不能自带一套尺寸 |
| 已有节点画布（C++） | `kit/View_FlowCanvas.h`：节点 150×76、5 态、左右各 1 端口、贝塞尔 `dx=max(36,|Δx|*0.55)` | **单端口不可复用**于本设计（需要多端口 + 类型），但缩放/平移/适配的算法可直接搬 |
| 已有节点画布（设计稿） | `webui/src/components/FlowCanvas.jsx` + `views.css:1017-1105`（`.fnode`/`.port`/`.flow-canvas`） | 同上；`canvas-page` / `float-toolbar` / `float-panel` 范式直接沿用 |
| `VisualNodeSystem` 已编译并链接 | `CMakeLists.txt:550-574`（`shine_vns` → `ShineTVStudio`），但 `src/ui/imgui/**` **零引用** | 移植时不必自研图算法；本轮 React 侧仍自绘，C++ 侧再评估是否切 `shine_vns` |
| 出图/出片页范式 | `webui/src/views/ImageFlow.jsx:197-243`：`canvas-page` + `FlowCanvas(fill, fitInset=380)` + 左上下浮动工具条 + 右侧 `float-panel`(348px) | 推荐方案 A 与它**同构**，学习成本为零 |
| 设计 token | 31 色 + `--r-*` / `--sp-*` / `--dur-*`；`tools/check-theme.ps1` 校验数量与顺序 | 新页**禁止**新写颜色，全部走 token |
| 组件库 | `UI.jsx` 导出 20 个组件（Button/IconBtn/Tag/StatusDot/Kbd/Card/Segmented/Tabs/Field/Input/TextArea/Select/Switch/Checkbox/Progress/Empty/KV/Art/ArtInk/Steps） | 配置面板全部用既有组件拼，不造新原语 |
| 图标库 | `Icon.jsx` 的 `P` 表（gauge/book/masks/clapper/image/film/...） | 新增节点类型若要新图标，需在 `Icon.jsx` 与 `Icon_Registry.cpp` 两处登记 |

---

## 2. 备选方案（三条完整方案 + 对比矩阵）

三条方案共用同一套节点模型（§3），差别在**布局、节点组织、交互逻辑、扩展性**。

### 方案 A：画布主导 + 浮层（LibTV / Figma 式）

```
┌──────────────────────────────────────────────────────────────┐
│ 顶栏 52                                                       │
├────┬──────────────────────────────────────────┬──────────────┤
│ 活 │ 面包屑 38                                 │              │
│ 动 │───────────────────────────────────────────│   检查器     │
│ 栏 │ ┌─ 页面工具条 ──────────────────────────┐ │   280        │
│ 56 │ ├────────────────────────────────────────┤ │  （外壳）    │
│    │ │ Skill 指令条（自然语言 + 快捷指令）    │ │              │
│    │ │ ┌────────────────────────┬───────────┐│ │              │
│    │ │ │                        │ 节点配置  ││ │              │
│    │ │ │   画布 canvas           │ 面板 348  ││ │              │
│    │ │ │   nodes + links + ports │  (浮层)   ││ │              │
│    │ │ │                        │           ││ │              │
│    │ │ ├────────────────────────┴───────────┤│ │              │
│    │ │ 节点库(折叠)   运行反馈流   小地图    ││ │              │
│    │ └───────────────────────────────────────┘│ │              │
│    ├───────────────────────────────────────────┤              │
│    │ 底栏 190（任务队列 / 生成日志 / 产物）      │              │
│    ├───────────────────────────────────────────┤              │
│    │ 状态栏 28                                   │              │
└────┴───────────────────────────────────────────┴──────────────┘
```

- **布局**：画布满铺工作区，所有辅助信息（节点库 / 配置 / 反馈）都是浮层，可折叠可隐藏。
- **节点组织**：自由坐标 + 网格吸附，无固定行列；节点按数据类型横向分层（左→右 = 内容→成品）。
- **交互逻辑**：拖节点、拖端口连线、框选、滚轮缩放（光标为锚）、拖背景平移、`F` 适配。
- **扩展性**：★★★★★ 新增节点类型只需加一条 `NODE_TYPES`，画布不关心语义；新增端口类型同理。
- **与现外壳兼容性**：★★★★★ 与 `ImageFlow/VideoFlow` 同构，零学习成本。
- **实现成本**：中（画布是主要工作量，约 420 行）。

### 方案 B：三栏 + 底部时间线（专业剪辑软件式）

```
┌──────┬──────────────────────────────┬──────────────┐
│ 节点 │        画布 canvas            │  节点配置     │
│ 库   │                              │  面板        │
│ 240  │                              │  320         │
│（常驻）│                              │              │
├──────┴──────────────────────────────┴──────────────┤
│ 时间线 timeline（按 镜1/镜2/... 排列轨道）            │
└─────────────────────────────────────────────────────┘
```

- **布局**：左节点库常驻、右配置常驻、底部时间线常驻，画布夹在中间。
- **节点组织**：节点在画布里自由摆放，但**另有一份时间线视图**同步反映剪辑顺序。
- **交互逻辑**：画布负责「结构」，时间线负责「顺序」；两处都要能选中同一实体。
- **扩展性**：★★★★☆ 结构清晰，但**两套视图的同步**是长期负担（选中/滚动/重排三处都要对齐）。
- **与现外壳兼容性**：★★★☆☆ 底部再塞一条时间线会与外壳底栏（190px）打架，要么吞掉底栏要么再增高。
- **实现成本**：高（画布 + 时间线 + 双向同步，约 700 行）。

### 方案 C：阶段泳道（Stage 泳道式）

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ 剧本泳道  │ 分镜泳道  │ 画面泳道  │ 配音泳道  │ 剪辑泳道  │
│ [梗概]    │ [镜1][镜2]│ [帧1][帧2]│ [旁白]    │ [成片]    │
│ [剧本]    │ [镜3][镜4]│ [帧3][帧4]│ [对白]    │ [字幕]    │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

- **布局**：五行水平泳道，节点在泳道内自动左→右排布，跨泳道连线竖直下落。
- **节点组织**：**按阶段强制归位**，节点不能跨泳道拖动。
- **交互逻辑**：拖拽受限于泳道；连线自动走正交折线；无法自由布局。
- **扩展性**：★★☆☆☆ 阶段固定 ⇒ 用户自定义节点（如「补帧」「风格迁移」）无处安放。
- **与现外壳兼容性**：★★★★☆ 不与底栏冲突，但画布高度被泳道数锁死。
- **实现成本**：中（约 450 行），但**牺牲了「自由连线」这条核心要求**。

### 2.4 对比矩阵（1–5 分，5 最好）

| 维度 | A 画布主导+浮层 | B 三栏+时间线 | C 阶段泳道 |
|---|---|---|---|
| 布局自由度 | 5 | 4 | 2 |
| 画布有效面积 | 5 | 3 | 4 |
| 节点组织清晰度 | 4 | 5 | 4 |
| 交互一致性（与现有页） | 5 | 3 | 4 |
| 扩展性（加节点类型/端口） | 5 | 4 | 2 |
| 实现成本（分高=成本低） | 4 | 2 | 3 |
| 与外壳 190 底栏兼容性 | 5 | 2 | 4 |
| **合计 /30** | **33→28** | 23 | 23 |

> 注：A 行合计按 6 项计 28（画布面积与自由布局双高）。

### 2.5 推荐结论

**采用方案 A**。三条理由：

1. **唯一同时满足「自由连线」与「不破坏外壳」**：B 会和底栏抢高度，C 直接锁死布局自由度。
2. **与 `ImageFlow/VideoFlow` 同构**：`.canvas-page` + `float-toolbar` + `float-panel` 的
   范式已经在本仓验证过两轮（出图、出片），新用户零学习成本，维护者零心智负担。
3. **扩展性最强**：节点类型与端口类型都是**数据**（`workflow.js`），加一种「补帧」节点
   只是往表里加一行，画布代码不动 —— 这正是「节点式工作流」的长期价值。

---

## 3. 节点模型（三方案共用的地基）

### 3.1 数据类型（Data Type）—— 8 种

端口的类型系统。颜色与图标固定，出现在端口上、连线中、面板字段标签前。

| 类型 id | 名称 | 色 token | 说明 |
|---|---|---|---|
| `text` | 文本 | `--info` | 梗概、剧本、台词 |
| `shots` | 镜头表 | `--accent` | 分镜节点产出的结构化镜列表 |
| `char` | 角色锁 | `--accent-2` | 三视图 + 多角度的锁定引用 |
| `image` | 帧图 | `--ok` | 关键帧 / 参考图 |
| `video` | 片段 | `--busy` | 生成的视频片段 |
| `audio` | 音频 | `--warn` | 配音 / 音乐 |
| `srt` | 字幕 | `--pending` | 带时间轴的字幕 |
| `timeline` | 时间线 | `--danger` | 剪辑合成后的可渲染时间线 |

### 3.2 节点类型（Node Type）—— 11 种

每种节点声明：`kind` / `name` / `icon` / `color(节点主色)` / `inputs[]` / `outputs[]` /
`fields[]`（配置面板字段）/ `defaultSize` / `category`（内容 / 视觉 / 声音 / 合成）。

| # | kind | 名称 | 图标 | 输入端口 | 输出端口 |
|---|---|---|---|---|---|
| 1 | `premise` | 剧情梗概 | `sparkles` | — | `text` 梗概 |
| 2 | `script` | 剧本 / 章节 | `book` | `text` 梗概 | `text` 场次台词 |
| 3 | `storyboard` | 分镜设计 | `clapper` | `text` 剧本 | `shots` 镜头表 |
| 4 | `charref` | 角色设定 | `masks` | — | `char` 角色锁 |
| 5 | `image` | 画面关键帧 | `image` | `shots` 镜头 / `char` 角色锁 | `image` 帧图 |
| 6 | `video` | 视频片段 | `film` | `image` 首帧 | `video` 片段 |
| 7 | `voice` | 配音 | `mic` | `text` 台词 / `char` 音色 | `audio` 音轨 |
| 8 | `subtitle` | 字幕 | `type` | `audio` 音轨 / `text` 台词 | `srt` 字幕 |
| 9 | `bgm` | 音乐音效 | `music` | — | `audio` 音乐 |
| 10 | `edit` | 剪辑合成 | `layers` | `video` / `audio` / `srt` | `timeline` 时间线 |
| 11 | `export` | 导出成片 | `upload` | `timeline` 时间线 | — |

**分组规则**：一个节点可有多个输入（如 `edit` 吃 video/audio/srt 三类），
端口按**数据类型**着色，不按「输入/输出」着色。

### 3.3 端口（Port）模型

```js
{ id: 'shots', label: '镜头', type: 'shots', dir: 'in' }   // dir: 'in' | 'out'
```

- 端口位置：输入在节点**左边缘**，输出在**右边缘**，纵向按索引均匀分布。
- 单输入端口**最多一条**入边（扇入 = 1，新连线替换旧线）。
- 单输出端口**可扇出多条**（一个梗概喂多个剧本）。
- 端口直径 10px（比既有 `.fnode .port` 的 9px 大 1px，因为要容纳类型色环）。

### 3.4 连线类型兼容矩阵

行 = 输出类型，列 = 输入类型。`✓` 允许，`—` 拒绝并给出理由。

| 输出 ↓ / 输入 → | text | shots | char | image | video | audio | srt | timeline |
|---|---|---|---|---|---|---|---|---|
| text | ✓ | — | — | — | — | ✓ | ✓ | — |
| shots | — | ✓ | — | ✓ | — | — | — | — |
| char | — | — | ✓ | ✓ | — | ✓ | — | — |
| image | — | — | — | ✓ | ✓ | — | — | — |
| video | — | — | — | — | ✓ | — | — | ✓ |
| audio | — | — | — | — | — | ✓ | — | ✓ |
| srt | — | — | — | — | — | — | ✓ | ✓ |
| timeline | — | — | — | — | — | — | — | ✓ |

> 关键拒绝规则：`text → shots` 必须拒绝（梗概不能直接当镜头表，必须经 `storyboard` 转换）。
> 这条规则让「分镜」节点在结构上不可绕过 —— 正是「统一链路」的结构保证。

### 3.5 节点状态机

| 状态 | 色 token | 含义 | 节点表现 |
|---|---|---|---|
| `todo` | `--text-muted` | 未运行 | 灰边、灰端口 |
| `blocked` | `--warn` | 依赖未就绪 | 琥珀边 + 锁图标 + 「等待 X」 |
| `queued` | `--pending` | 已排队 | 蓝边 + 队列位次 |
| `running` | `--busy` | 运行中 | 紫边 + 转圈 + 进度条 |
| `done` | `--ok` | 完成 | 绿边 + 勾 |
| `failed` | `--danger` | 失败 | 红边 + 重试按钮 |
| `skipped` | `--idle` | 跳过 | 灰虚线 + 「已跳过」 |

**合法迁移**（其余迁移一律拒绝并记 warn 日志）：

```
todo ──依赖满足──▶ queued ──▶ running ──▶ done
 │                    │           │
 │                    │           └──▶ failed ──重试──▶ queued
 │                    └──取消──▶ todo
 ├──依赖不满足──▶ blocked ──依赖满足──▶ queued
 └──用户跳过──▶ skipped ──恢复──▶ todo
```

**依赖判定**：节点的 `blocked` 当且仅当「存在某条入边，其源节点状态 ∉ {done, skipped}」。

---

## 4. 推荐方案（A）详细设计

### 4.1 页面区域划分（含精确尺寸）

工作区可用区 = 视口 − 活动栏 56 − 侧栏 240 − 检查器 280。页面在其中**满铺**。

| 区域 | 位置 | 尺寸 | 层级 | 说明 |
|---|---|---|---|---|
| 画布 `canvas` | `inset:0` | 填满 | 0 | 网格背景 + 节点 + 连线 |
| 页面工具条 `wf-toolbar` | `top:16 left:16` | 高 40 | 6 | 标题 / 导入 / 导出 / 校验 / 运行 |
| Skill 指令条 `wf-skill` | `top:16 left:50%` | 宽 `min(560, 60%)`，高 44 | 7 | 自然语言入口 + 快捷指令 |
| 节点库 `wf-library` | `left:16 bottom:16` | 220 宽，可折叠至 44 | 6 | 11 种节点拖拽/点击加入 |
| 配置面板 `wf-inspector` | `right:16 top:16 bottom:16` | 348 宽 | 6 | 参数 / 端口 / 角色 / 日志 四个页签 |
| 运行反馈 `wf-feed` | `left:252 bottom:16` | 高 132（可折叠） | 6 | Agent 执行步骤流 |
| 小地图 `wf-minimap` | `right:380 bottom:16` | 180×110 | 5 | 全图缩略 + 视口框 |

> **为什么配置面板是浮层而不是外壳检查器**：外壳检查器（280px）是全局的，切到别的
> 工作区还在；而节点配置是**页面内的临时状态**，节点选中才出现。用 `float-panel` 与
> 出图页保持一致，用户不用学两套交互。

### 4.2 画布交互细则

| 操作 | 触发 | 行为 | 边界 |
|---|---|---|---|
| 选中节点 | 左键按下节点 | 高亮 + 打开配置面板 | 已选中再点 = 保持 |
| 拖动节点 | 左键按住节点移动 | 实时改坐标，连线跟随 | 吸附 8px 网格（`Alt` 暂停吸附） |
| 框选 | 左键拖背景 | 画选框，选中框内节点 | 框选从空白处开始 |
| 平移 | 拖背景 / 中键 / 空格+拖 | 改视图偏移 | — |
| 缩放 | 滚轮 | **以光标为锚**，0.35–2.0 | 阻止页面滚动穿透 |
| 适配 | `F` / 工具条按钮 | 全部节点居中，上限 1.15 | — |
| 连线 | 从输出端口拖到输入端口 | 实时预览贝塞尔，松手校验类型 | 非法则红线 + toast 说明 |
| 断线 | 拖输入端口 / 右键连线 | 删除该边 | — |
| 删除节点 | `Delete` / `Backspace` | 删节点 + 删其所有边 | 有下游时二次确认 |
| 复制节点 | `Ctrl+D` | 复制节点（不带连线） | — |
| 撤销 | `Ctrl+Z` | 撤销上一步图变更 | 栈深 50 |
| 重做 | `Ctrl+Shift+Z` | 重做 | — |

**贝塞尔连线**：与既有实现一致 `dx = max(36, |Δx| * 0.55)`，
起点 = 源节点右边缘端口圆心，终点 = 目标节点左边缘端口圆心。
**运行中**的连线加 `stroke-dasharray: 6 6` + `flow-dash` 动画（沿用 `.fnode` 同款）。

**网格背景**：`radial-gradient(circle at 1px 1px, var(--line-normal) 1px, transparent 0) 0 0/22px 22px`
（与 `.flow-canvas` 完全一致），缩放时背景同步缩放（`background-size` 乘 `z`）。

### 4.3 各节点配置面板字段（逐节点）

配置面板四个页签：**参数**（字段表单）/ **端口**（输入输出与连接状态）/ **角色**（角色锁，仅 image/video/voice）/ **日志**（该节点的执行日志）。

#### ① `premise` 剧情梗概

| 字段 | 控件 | 类型 | 默认 | 说明 |
|---|---|---|---|---|
| 作品标题 | Input | string | 灯语回声 | — |
| 题材 | Select | enum | 都市奇幻 | 都市奇幻/悬疑/治愈/科幻/古装 |
| 一句话梗概 | TextArea | text | 一盏能记住梦的灯… | 3 行 |
| 目标时长 | Segmented | enum | 60s | 15s/30s/60s/自定义 |
| 画幅 | Segmented | enum | 9:16 | 9:16 / 16:9 / 1:1 |
| 风格参考 | Select | enum | 治愈系 | 治愈系/赛博/水墨/胶片/暗黑 |
| 受众 | Input | string | 18–30 都市青年 | — |

#### ② `script` 剧本 / 章节

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 源章节 | Select | 第 3 章 | 来自小说工作区 |
| 改编方式 | Segmented | 忠实改编 | 忠实/压缩/扩写 |
| 场次数量 | Input(number) | 5 | 1–20 |
| 对白密度 | Slider | 0.6 | 0–1 |
| 旁白 | Switch | 开 | 是否保留旁白 |
| 台词稿 | TextArea | — | 只读预览，可编辑 |

#### ③ `storyboard` 分镜设计

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 镜头数 | Input(number) | 6 | — |
| 平均时长 | Input(number) | 4.5s | — |
| 运镜 | Select(multi) | 推/摇/移 | 推/拉/摇/移/跟/升降 |
| 景别策略 | Select | 自动 | 自动/远景开场/近景为主 |
| 转场 | Select | 硬切 | 硬切/叠化/淡入淡出 |
| 镜头表 | 只读表格 | 6 行 | 镜号/景别/运镜/时长/描述 |

#### ④ `charref` 角色设定（角色一致性核心）

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 角色名 | Input | 林晓 | — |
| 三视图 | 三宫格槽位 | 正/侧/背 各 1 张 | 点击上传或从资产库选 |
| 多角度 | 四宫格槽位 | 仰/俯/左 45°/右 45° | 补足大角度运动 |
| 锁定 seed | Input(number) | 771204 | 同角色跨节点必须一致 |
| 锁定强度 | Slider | 0.82 | 0–1，越高越不漂移 |
| 允许漂移 | Slider | 0.12 | 阈值，超出则告警 |
| 服装/发型锁 | Switch | 开 | — |
| 参考图集 | 资产多选 | 3 张 | 来自资产工作区 |

#### ⑤ `image` 画面关键帧

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 角色锁 | 徽章 + 下拉 | 林晓 · 3 视图 + 4 角度 | 来自 `charref` 连线；无连线时显示「未锁定」红字 |
| 采样器 | Select | sdXL_light_04 | — |
| 步数 | Input(number) | 28 | — |
| CFG | Slider | 6.5 | — |
| 分辨率 | Segmented | 720×1280 | 按画幅联动 |
| 重生成次数 | Input(number) | 3 | 每次 +1 seed |
| Prompt | TextArea | 自动生成 | 可覆盖 |

#### ⑥ `video` 视频片段

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 时长 | Input(number) | 4.0s | — |
| 帧率 | Segmented | 24 | 24/30/48 |
| 补帧 | Select | RIFE ×2 | 关/RIFE×2/FILM |
| 运动强度 | Slider | 0.5 | — |
| 角色锁校验 | 只读 | ✔ 与 image 一致 | 不一致则红字 |
| 首尾帧 | 双图槽位 | 来自 image | — |

#### ⑦ `voice` 配音

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 音色 | Select | 温柔女声 | 来自 `charref` 音色锁 |
| 语速 | Slider | 1.0 | 0.5–2.0 |
| 音调 | Slider | 0.0 | -12–12 |
| 情感 | Select | 平静 | 平静/欢快/悲伤/紧张 |
| 逐句对齐 | Switch | 开 | 生成 SRT 时间轴 |
| 试听 | Button | — | 播放波形 |

#### ⑧ `subtitle` 字幕

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 字号 | Segmented | 中 | 小/中/大/特大 |
| 位置 | Segmented | 下中 | 上中/下中/下左 |
| 描边 | Switch | 开 | — |
| 双语 | Switch | 关 | 中英对照 |
| 样式预设 | Select | 简洁白 | 简洁白/描边黄/渐变 |
| 时间轴 | 只读表 | 来自 audio | 起止/文本 |

#### ⑨ `bgm` 音乐音效

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 曲目 | Select | 夜航 | — |
| 音量 | Slider | -18dB | — |
| 淡入淡出 | Switch | 开 | — |
| 卡点 | Switch | 关 | 按镜头切点对齐 |

#### ⑩ `edit` 剪辑合成

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 时间线 | 只读轨道 | 视频/音频/字幕 三轨 | 来自三条入边 |
| 转场 | Select | 硬切 | — |
| 调色 | Select | 原片 | 原片/暖调/冷调/电影感 |
| 输出分辨率 | Segmented | 1080×1920 | — |
| 码率 | Select | 8M | 4M/8M/12M |
| 响度归一 | Switch | 开 | -14 LUFS |

#### ⑪ `export` 导出成片

| 字段 | 控件 | 默认 | 说明 |
|---|---|---|---|
| 文件名 | Input | 灯语回声_ep03.mp4 | — |
| 容器 | Segmented | MP4 | MP4/MOV |
| 编码 | Select | H.264 | H.264/H.265 |
| 命名模板 | Input | `{project}_ep{ep}.mp4` | — |

### 4.4 角色一致性设计

**目标**：同一角色在「图 / 视频 / 配音」三类节点里长得一样、声音一样。

三层机制：

1. **源头锁**（`charref` 节点）：三视图（正/侧/背）+ 多角度（仰/俯/±45°）建参考包；
   锁定 `seed`、`prompt` 片段、服装/发型开关。
2. **传递校验**：`image` / `video` / `voice` 节点声明 `char` 输入端口。
   连线时校验：下游引用的 `seed` 必须与上游 `charref.seed` 一致，否则**端口显示红色 + 面板红字**。
3. **冲突检测**（页面级）：遍历所有 `charref` 节点，同名角色但 `seed` 不同 ⇒ 顶部黄条
   「角色「林晓」存在 2 个不同锁定 seed，可能导致崩坏」，可一键统一。

节点上的视觉表达：`image` / `video` 节点头部一枚 `char` 徽章（`--accent-2` 底 + 角色名），
无锁定时显示空心徽章 + 「未锁定」。

### 4.5 Skill 接口设计（自然语言 → 自动生成流程）

#### 入口（页面内）

- **指令条**：`sparkles` 图标 + 输入框 + 「生成」按钮，占位文案
  「描述你要的视频，例如：把第 3 章改编成 60 秒竖屏短片，治愈系画风」。
- **快捷指令 chips**：`全流程` / `仅重跑失败` / `加一版竖屏` / `替换角色` / `继续未完成的`。
- **反馈流**：提交后 `wf-feed` 逐条追加，每条含 `{图标, 文案, 耗时, 状态}`。

#### 反馈流脚本（确定性 mock，展示「流程自动生成」）

```
① 解析指令            意图=全流程 · 章节=第3章 · 时长=60s · 画幅=9:16      120ms  ✔
② 读取项目上下文      灯语回声 · 第 3 章 · 6 镜 · 角色=林晓               86ms   ✔
③ 规划节点图          11 类节点 → 采用 9 类 · 预计 12 节点               64ms   ✔
④ 建立节点            梗概→剧本→分镜→角色→图×3→视频×3→配音→字幕→剪辑    210ms  ✔
⑤ 连接端口            15 条边 · 类型校验 0 冲突                          48ms   ✔
⑥ 角色锁定            林晓 · seed=771204 · 3 视图 + 4 角度               102ms  ✔
⑦ 依赖校验            无环 · 无孤立节点 · 关键路径 7 层                  31ms   ✔
⑧ 就绪                按 Ctrl+Enter 运行 · 预计 6 分 12 秒                —      ✔
```

#### Skill 调用契约（供 Agent 接入）

页面只负责展示与触发，真实能力通过统一契约暴露。三种动作：

```
POST { base }/skill/workflow/plan
  req : { instruction: string, project: string, chapter?: string,
          constraints?: { duration?, aspect?, style?, characters?[] } }
  resp: { nodes: Node[], links: Link[], warnings: string[], skill: string }

POST { base }/skill/workflow/run
  req : { graphId: string, nodeIds?: string[] }     // 省略 = 全图
  resp: { runId: string }

GET  { base }/skill/workflow/status?runId=...
  resp: { nodes: { [id]: { state, progress, log[] } } }
```

- **Skill 标识**：`skill: "shinetv.workflow.v1"`，便于 Agent 路由与版本协商。
- **无后端时**：`planWorkflow()` 用 `workflow.js` 的模板表本地生成，**离线可用**，
  这也是设计稿能跑起来的前提。
- **幂等**：同一 `instruction` 重复提交 ⇒ 复用现有图，只在反馈流提示「已存在同构图」。

### 4.6 异常与空态

| 场景 | 表现 | 文案 |
|---|---|---|
| 工程未打开 | 画布中央空态 | 「打开一个工程后，工作流会自动从小说章节生成」+「打开工程」按钮 |
| 画布无节点 | 空态 + 虚线示例 | 「还没有节点」+「从左侧节点库拖入」+「或让 Skill 生成全流程」 |
| 节点缺输入 | 节点 `blocked` + 端口红点 | 「等待上游：分镜设计」 |
| 连线类型不匹配 | 预览线变红 + 松手 toast | 「镜头表(sh ots) 不能直接接入 梗概(text)，请经「分镜设计」转换」 |
| 成环 | 拒绝 + toast | 「连线会形成环，已撤销」 |
| 角色 seed 冲突 | 顶部黄条 | 「角色「林晓」存在 2 个不同锁定 seed」+「统一」按钮 |
| 依赖节点被删 | 下游全部转 `blocked` | 下游节点头显示「依赖缺失」 |
| Skill 无法解析 | 反馈流红条 | 「没能理解这条指令，试试：把第 3 章改编成 60 秒竖屏短片」 |
| 运行中失败 | 节点 `failed` + 面板「重试」 | 「生成失败：显存不足（阶段 2/3）」+「重试」「跳过」 |
| 画布缩放超限 | 到 0.35 / 2.0 停住 | 工具条百分比变灰 |

### 4.7 响应式适配

外壳本身是固定档（`Shell_Layout.h`），所以「响应式」= **工作区可用宽度变化时，
浮层与画布的重组规则**，而不是媒体查询。

| 可用宽度 | 行为 |
|---|---|
| ≥ 1180 | 完整：工具条 + 技能条 + 节点库展开 + 配置面板 + 反馈流 + 小地图 |
| 900–1180 | 技能条缩为图标按钮（点击展开成浮层）；节点库默认折叠；隐藏小地图 |
| 720–900 | 配置面板默认折叠为右侧 44px 竖条；反馈流隐藏（并入底栏） |
| < 720 | 节点库变底部抽屉；技能条独占一行；画布仍可缩放平移（保证可用） |

> 由于外壳最小设计宽度是 720（`Shell.cpp` 里 `max(origin.width(), 764.0f)`），
> `<720` 分支只在「侧栏/检查器全开导致工作区被挤窄」时出现，仍要保证可折叠。

### 4.8 组件层级

```
Workflow (view)
├─ .canvas-page
│  ├─ <WorkflowCanvas>                    ← 画布（唯一有坐标变换的组件）
│  │  ├─ svg.wf-links                     ← 连线层（贝塞尔 + 动画 + 预览线）
│  │  ├─ .wf-node × N                     ← 节点
│  │  │  ├─ .wf-node-head  (icon + 标题 + 状态)
│  │  │  ├─ .wf-node-body  (主值预览 + 角色徽章)
│  │  │  └─ .wf-port × M   (in/out，按类型着色)
│  │  ├─ .wf-marquee                      ← 框选框
│  │  └─ .wf-minimap                      ← 小地图
│  ├─ .wf-toolbar                         ← 页面工具条
│  ├─ .wf-skill                           ← Skill 指令条
│  ├─ .wf-library                         ← 节点库（可折叠）
│  ├─ .wf-feed                            ← 运行反馈流
│  └─ .wf-inspector.float-panel           ← 配置面板（4 页签）
│     ├─ .fp-h   选中节点标题 + 状态
│     ├─ .fp-b   Segmented + 字段表单
│     └─ .fp-f   运行/重跑按钮 + 校验状态
```

**状态归属**（关键）：

- `graph`（节点/边/选中/视图）—— 页面级 `useState`，因为要跨「节点库拖入」「Skill 生成」
  「配置面板编辑」三处共享。
- `skillLog` —— 页面级数组，Skill 触发时 append。
- 每个节点内部的「运行中进度」—— 存在节点对象上（`node.progress`），不进全局 store，
  避免每秒 rerender 整页。
- 主题 / toast / 文件查看器 —— 复用既有 `useApp()`（`notify` / `setViewer`）。

---

## 5. 分步实施计划（每步含文件与验收）

> 阶段一 = React 设计稿（`webui/`）；阶段二 = C++ 1:1 移植（`src/ui/imgui/`）。
> 每步都可独立验收，失败不阻塞前序步骤。

### 阶段一：React 设计稿

#### S1 · 数据模型 `webui/src/data/workflow.js`（新增）

- 导出 `DATA_TYPES`（8 种，含 label + 色 token）、`NODE_TYPES`（11 种，含端口与字段）、
  `DEFAULT_NODES` / `DEFAULT_LINKS`（12 节点 / 15 边）、`SKILL_PRESETS`（5 条快捷指令）、
  `CHAR_REFS`（2 个角色）、`planWorkflow(instruction)`（本地规划器）、
  `canConnect(fromType, toType)`（兼容矩阵）。
- **验收**：`node -e "import('./src/data/workflow.js').then(m=>console.log(m.NODE_TYPES.length, m.DEFAULT_NODES.length, m.DEFAULT_LINKS.length))"` 输出 `11 12 15`。

#### S2 · 画布组件 `webui/src/components/WorkflowCanvas.jsx`（新增）

- 世界坐标变换（`{x,y,z}`）、光标锚点缩放、拖节点、拖背景平移、框选、
  贝塞尔连线、端口命中（半径 9px）、拖拽连线预览、`F` 适配、网格背景。
- Props：`nodes` / `links` / `selected` / `onSelect` / `onChange` / `onConnect`。
- **验收**：`npm run build` 通过；浏览器内拖动节点、缩放、连线不丢帧、控制台无报错。

#### S3 · 页面 `webui/src/views/Workflow.jsx`（新增）

- 组合 S2 画布 + 工具条 + Skill 条 + 节点库 + 配置面板（4 页签）+ 反馈流 + 小地图。
- 配置面板按选中节点类型渲染 §4.3 的字段表。
- Skill 提交 → `planWorkflow()` → 替换图 + 逐条追加反馈流。
- **验收**：11 种节点各选中一次，配置面板字段与 §4.3 一致；Skill 能生成 12 节点。

#### S4 · 样式 `webui/src/styles/workflow.css`（新增）+ 接线

- `.wf-*` 全部走 `tokens.css` 的 `--r/--sp/--dur/--bg/--line/--text/--accent`。
- 修改 `webui/src/main.jsx`（`import './styles/workflow.css'`）、
  `webui/src/App.jsx`（`VIEWS` 加 `workflow: Workflow`）、
  `webui/src/data/mock.js`（`WORKSPACES` 加第 8 项，**插在 video 与 gallery 之间**）。
- **验收**：导航出现「工作流」；切换无白屏；5 套主题（`data-theme`）下均正常；
  `tools/check-colors.ps1` 等价的人工核对：**新文件里 0 个硬编码颜色**。

#### S5 · 交互打磨与空态

- 删除节点二次确认、撤销/重做（栈深 50）、复制节点、断线、成环拒绝、类型拒绝 toast。
- 4 类空态（无工程 / 无节点 / blocked / failed）全部可达。
- **验收**：走一遍 §4.6 表格的 10 个场景，逐条对照。

#### S6 · 构建与预览核对

- `cd webui && npm run build` 通过；起 `npm run dev` 或 `vite preview` 截图核对。
- **验收**：5 套主题各 1 张截图；1600×960 与 1280×800 两种宽度各 1 张。

### 阶段二：C++ 1:1 移植（在阶段一视觉定稿后）

#### S7 · 工作区注册

- `src/ui/imgui/pages/Shell.h`：`enum class Workspace` 在 `VideoFlow` 后加 `Workflow`，
  `kWorkspaceCount` 7 → 8。
- `Shell.cpp`：`WorkspaceIcon/Label/Subtitle` 加分支（icon=`flow`、label=`工作流`、
  sub=`节点式视频生产线`）；`DrawWorkspace` 的 `switch` 加 `case 6`；
  并把 `flow` 判定从 `workspace == 4 || == 5` 改成 `>= 4 && <= 6`。
- **验收**：`tools/check-layers.ps1` / `check-colors.ps1` / `check-theme.ps1` 全绿。

#### S8 · 页面骨架 `src/ui/imgui/pages/Page_Workflow.h/.cpp`（新增）

- 与 `Page_ImageFlow.cpp` 同构：`Draw(Rect, ImDrawList*)`，内部用 `kit::` 组件。
- 节点数据用 `std::vector<WfNode>` / `std::vector<WfLink>`，由页面持有（跨帧）。
- **验收**：导航第 8 项可点，页面出画布 + 空态。

#### S9 · 画布渲染（自绘）

- 视口变换 `FlowView`（复用 `kit/View_FlowCanvas.h` 的缩放/适配算法），
  但节点渲染与端口命中**新写**（多端口 + 类型色）。
- 端口命中：`Rect::contains` 手算（禁 `IsMouseHoveringRect`）。
- **验收**：拖节点/缩放/连线在真机可用；`overall=PASS`。

#### S10 · 配置面板 + 节点库 + Skill 条

- 用 `kit/Widget_*` 拼；颜色全部从 `kit/Widget_Color.h` 取。
- **验收**：`check-colors.ps1` 0 新增；11 种节点面板字段齐全。

#### S11 · 回归取证

- `out\_make_fixture.ps1` → `out\_run_review.ps1`，为工作流页加 3 张截图 +
  1 条判据（至少 1 个节点可见 / 至少 1 条连线可见）。
- **验收**：`overall=PASS` ↔ 退出码 0；新旧截图差异集 ⊆ 同二进制波动集。

---

## 6. 验收清单（Definition of Done）

| # | 判据 | 怎么验 |
|---|---|---|
| 1 | 新增第 8 个工作区，既有 7 个不受影响 | 逐个点开，截图比对 |
| 2 | 11 种节点可创建、可拖、可删、可复制 | 手动走查 |
| 3 | 端口类型校验生效（`text → shots` 被拒） | 尝试连线，应红 |
| 4 | 12 节点 / 15 边默认图完整呈现五段链路 | 数节点 |
| 5 | 状态机 7 态可见，迁移合法 | 触发 Skill 生成，观察 todo→running→done |
| 6 | 角色锁：三视图 + 多角度 + seed 一致性 | 选中 `charref` 与 `image`，对照面板 |
| 7 | Skill 指令能生成完整图 + 反馈流 8 条 | 提交一条指令 |
| 8 | 10 类异常/空态可达且文案正确 | 逐条对照 §4.6 |
| 9 | 5 套主题下颜色正确、无硬编码 | 切主题 + `check-colors.ps1` |
| 10 | 构建通过、无新增门禁告警 | `Build.bat --release` + 4 个门禁 |

---

## 7. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| 画布与外壳 `ScrollRegion` 抢滚轮 | 缩放时页面滚动 | 画布容器 `wheel` 监听 `{passive:false}` + `stopPropagation`（沿用 `FlowCanvas.jsx:65-83`） |
| 浮层与外壳 child 抢 `HoveredId` | 面板按钮点不动 | 沿用 `kit::OverlayMouseGate()`；React 侧靠 z-index + `pointer-events` |
| 节点拖出可视区后找不回 | 用户困惑 | 小地图 + `F` 适配 |
| 连线锚点在缩放后错位 | 视觉错位 | 连线统一在**世界坐标系**计算，整体 transform |
| C++ 侧自绘节点文本用裸 `AddText` | 门禁红 | 一律走 `kit::Text` + `theme::font::*` |
| 31 色 token 不够用 | 想加色 | **不加**。新语义一律复用 `--info/--ok/--warn/--busy/--accent-2/--pending` |
| 角色锁语义与真实 pipeline 不匹配 | 移植时返工 | 契约（§4.5）先行，页面只依赖契约不依赖实现 |

---

## 8. 与既有文档的关系

- 视觉判据：[`design-spec.md`](design-spec.md)（本文不新增颜色、不改外壳尺寸）。
- 文件地图：[`README.md`](README.md) §3（S7–S10 新增的文件按该 convention 命名）。
- 坑与纪律：[`记忆.md`](记忆.md)（画布、浮层、HitTest、Rect 四参等条目全部适用）。

---

## 9. 实施结果（2026-11-23，阶段一已完成）

### 9.1 实际产出的文件

| 文件 | 状态 | 说明 |
|---|---|---|
| [`webui/src/data/workflow.js`](../webui/src/data/workflow.js) | 新增 | 8 种数据类型 / 11 种节点 / 12 节点 15 边默认图 / 兼容矩阵 / 本地规划器 |
| [`webui/src/components/WorkflowCanvas.jsx`](../webui/src/components/WorkflowCanvas.jsx) | 新增 | 画布：世界变换、光标锚点缩放、拖节点、框选、端口连线、拔线重连、小地图 |
| [`webui/src/views/Workflow.jsx`](../webui/src/views/Workflow.jsx) | 新增 | 页面：顶栏 / Skill 条 / 节点库 / 配置面板四页签 / 生成反馈流 / 角色冲突条 |
| [`webui/src/styles/workflow.css`](../webui/src/styles/workflow.css) | 新增 | 全部走 `tokens.css` 的 token，0 个颜色字面量 |
| `webui/src/components/Icon.jsx` | 改 | 新增 `music` 图标（`bgm` 节点用），既有图标一行未动 |
| `webui/src/App.jsx` | 改 | `VIEWS` 加 `workflow: Workflow` |
| `webui/src/main.jsx` | 改 | 追加 `import './styles/workflow.css'` |
| `webui/src/data/mock.js` | 改 | `WORKSPACES` 在 `video` 与 `gallery` 之间插入第 8 项「工作流」 |
| `webui/src/shell/Shell.jsx` | 改 | 面包屑加 `workflow` 标签；`.ws-view` 的 `overflow:hidden` 条件加 `workflow` |
| `webui/src/views/ProjectHub.jsx` | 改 | 文案「六个工作区」→「七个工作区」 |
| `out/_shot.mjs` · `out/_drive.mjs` | 新增 | CDP 截图 / 步骤驱动（无 npm 依赖），用于本页的版式与交互取证 |

既有 7 个工作区、`FlowCanvas.jsx`、`shine_core` **一行未删未改**。

### 9.2 与设计稿原计划的差异（都是实施中发现的更优解）

| # | 原计划（§4.1） | 实际实现 | 原因 |
|---|---|---|---|
| 1 | 工具条与 Skill 条**各自独立**浮在左上/中上 | 合并成一个 `.wf-topbar`（工具条 + Skill 条同一行） | 1600 宽下两者实测**重叠 158px**；合并且 Skill 条右对齐到检查器，零重叠且自适应换行 |
| 2 | 运行反馈流是**左下固定浮层**（132px 高） | 改为 **Skill 条下拉**（`wf-skill-feed`） | 左下已被节点库占用；反馈流是「生成中」的临时信息，贴着指令条更符合因果 |
| 3 | 节点库是**左侧竖条**（220×305） | 改为**底部横条**（620×133），窄屏收成图标条（74px） | 竖条吃掉 305px 高度，把 12 节点图挤到 z=0.39（节点 71×34 不可读）；横条后 z=0.68（125×60） |
| 4 | 默认图 14 条边 | 实际 **15 条** | 原表漏了 `edit → export`，导致 `export` 成为孤立节点（被自检抓到） |
| 5 | insets 写死常量 | 改为**实测**顶栏/节点库高度（`ResizeObserver`） | 窄屏顶栏换行后高度从 78 变 135，写死会让节点压到 Skill 条下面 |
| 6 | 窄屏靠媒体查询 | `data-narrow` + 窄屏**默认折叠配置面板** | 1280 宽下配置面板占 348px，画布只剩 564px；折叠后画布回到 884px |

### 9.3 实测验收读数（CDP 取证，非目测）

版式审计脚本 `out/_audit-wf.js`（节点/浮层矩形 + 三类重叠检测）：

| 判据 | 1600×960 | 1280×800 |
|---|---|---|
| 浮层互相重叠 `overlaps` | `[]` | `[]` |
| 节点被裁出画布 `clipped` | `[]` | `[]` |
| 节点互相压住 `nodeOverlap` | `[]` | `[]` |
| 浮层压住节点 `panelNodeOverlap` | `[]` | `[]` |
| 节点渲染尺寸 | 125×60（z=0.68） | 125×60（z=0.68） |
| 端口数 / 连线 path 数 | 28 / 45（=15 边 × 3） | 28 / 45 |
| JS 运行时错误 `window.__errs` | `[]` | `[]` |

交互取证（`out/_drive.mjs` + `out/_steps-tabs.json`）：

| 场景 | 读数 |
|---|---|
| 点 `charref` → 角色页签 | `charHero=true`，三视图 3/3 + 多角度 4/4 全就绪，seed 771204 |
| 点 `edit` → 端口页签 | 5 个端口行，4 条来源（视频片段 ×2 / 配音 / 字幕），0 未连接 |
| 点 `img2` → 日志页签 | 2 条日志（入队 + OOM 失败），失败态出现「重试 / 跳过」 |
| Skill「全流程」 | 反馈流 7 条逐条追加（2→3→4→5→7 步），重建 12 节点 15 边，无警告 |
| 点「运行」 | 6s 后 9 完成 / 1 运行 / 1 阻塞 / 1 待运行（拓扑序推进中） |
| 八个工作区来回切换 | 8/8 命中，仅工作流页有 `.wf-node`（12 个），`__errs=[]` |

> ⚠️ 附带发现（**非本页引入**）：外壳 `documentElement.scrollWidth = 1681 > innerWidth = 1600`，
> 来源是底栏 `.dock`（`scrollWidth=1681`）与活动栏 tooltip（`.rail` `scrollWidth=228`）。
> 在「总控」页同样为 1681，属既有现象；本轮未改外壳。

### 9.4 阶段二：C++ 1:1 移植（已完成）

| 文件 | 状态 | 说明 |
|---|---|---|
| [`src/ui/imgui/pages/Page_Workflow.cpp`](../src/ui/imgui/pages/Page_Workflow.cpp) | 新增（~1400 行） | 页面实现：11 类节点表、画布（世界变换/光标锚点缩放/拖拽/框选→平移/端口连线/拔线重连/点阵底）、顶栏、Skill 条、节点库、四页签配置面板、反馈流 |
| `src/ui/imgui/pages/WorkspacePages.h` | 改 | 加 `class WorkflowPage`（**页面类集中声明**，与其它六页同一约定） |
| `src/ui/imgui/pages/Shell.h` | 改 | `enum class Workspace` 加 `Workflow`；`kWorkspaceCount` 7→8；成员 `workflow_` |
| `src/ui/imgui/pages/Shell.cpp` | 改 | `WorkspaceIcon/Label/Subtitle/Target` 各加 case 6；`DrawWorkspace` 加 `case 6`；`flow` 判定改 `>=4 && <=6` |
| `src/ui/imgui/kit/Icon_Registry.cpp` | 改 | 补 `music` 图标（数组 49→50）；不补会静默回落到 `info` |
| `src/ui/imgui/verify/ReviewSession.h` | 改 | `kThemedWorkspaces` 加 `Workflow`（主题轮 30→35 组） |

**与原计划的四处差异**（实施中按项目约定修正）：

| # | 原计划 | 实际 | 原因 |
|---|---|---|---|
| 1 | 新建 `Page_Workflow.h` | 类声明进 `WorkspacePages.h`，只留 `.cpp` | 本仓六个页面类**全部**集中在 `WorkspacePages.h`；另开头文件会出现两份声明 |
| 2 | 新建 `kit/View_WfCanvas`（多端口画布） | 画布**内联在页面里** | 多端口渲染与本页字段强耦合，抽成 kit 会导出 `Node` 私有类型；先内联，等第二个使用方出现再抽 |
| 3 | 复用 `kit::FlowCanvas` | **不复用**，只借 `kit::FlowView` + 贝塞尔公式 | 那个组件是单端口 + 固定 150×76；硬用会把出图/出片页一起改坏 |
| 4 | 节点库竖条 | 底部横条 + 点阵底 | 与 webui 对齐（竖条吃 305px 高度，把图挤到不可读） |

**验收读数（全部实测）**：

| 项 | 结果 |
|---|---|
| 构建 | `Build.bat --release` → `[100%] Built target ShineTVStudio`，`build/release/ShineTVStudio.exe` 19.4 MB |
| 门禁 | `check-layers` PASS · `check-colors` PASS（0 字面量 / 24 已登记）· `check-theme` PASS · `check-i18n` PASS |
| 裸文字 | `find-raw-text` **0 处**（全页走 `kit::Text`/`DrawTextClipped` + `theme::font::*`） |
| 1:1 回归 | **96 张 / 0 失败 · `overall=PASS`**（比改动前多 5 张 = 工作流页 ×5 主题） |
| 重复命中 | `duplicate-hits: 24 → 0`（首轮我在同一矩形上分别调 `Hovered`+`Clicked`，各注册一个 InvisibleButton；改成一次 `HitTest`） |
| 画布像素 | 画布区底色 `#17202D` 占 37.5%、accent `#35D0B4` 2910 px、unique 颜色 6125（对照出图页 3784） |

**已知缺口（诚实记录）**：

- 取证覆盖：本页只有 `ws-layers` + 5 张主题图，**没有** hover / overlay / 快捷键探针（那些表是按坐标写死的，本页节点坐标随 fit 变化，写死会假红）。
- 字段值：配置面板的输入值存在「节点下标 → 字段串」的静态缓存里，**没有**接回 `pipeline` / `comfy`；Skill 解析是关键词离线规划；「运行」是状态模拟，不产生真实产物。
- 点阵底：按视图偏移逐点画（约 3000 个圆/帧），视口很大时可考虑改纹理平铺。