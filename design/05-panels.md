---
id: design.p05-panels
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/pages/shell/SidePanel.cpp
  - src/ui/pages/shell/RightPanel.cpp
  - src/ui/pages/shell/BottomDock.cpp
  - src/ui/pages/shell/MainWindow.cpp
  - src/ui/kit/controls/Feedback.h
  - src/ui/kit/data/Panels.h
  - src/ui/kit/images/Grid.h
  - docs/10-modules/ui-kit.md
  - docs/10-modules/visual-storyboard.md
  - docs/10-modules/flow-comfy.md
last_verified: 2026-09-28
---

# 05 · 侧栏 / 右栏 / 底栏：从占位文案换成真实内容

**工作量**：大（3 个区域 + 6 个工作区的数据接入）
**风险**：中——涉及真实数据读取，必须遵守"UI 线程不做 IO"
**收益**：高。这三块占了约 740px 宽度和 220px 高度，现在是纯浪费。

## 目标

1. 侧栏：每个工作区显示**真实的资源树/列表**，无内容时用 `EmptyState` 给出下一步。
2. 右栏：改为**选中驱动的 Inspector**，无选中即隐藏（已在 04 设定宽度策略）。
3. 底栏：接**真实数据**（队列 / 日志 / 产物 / 校验），默认折叠为 28px 单行任务条。
4. 消灭所有"P0x 接入"式占位文案。

## 不做什么

- 不新增业务能力（不接新的 Comfy 接口、不新增 LLM 调用）。
- 不改流水线/账本的落盘格式。

---

## 现状证据

| 区域 | 现状 | 位置 |
|---|---|---|
| 侧栏 | 6 个工作区同一段占位文案，整栏 240px | `src/ui/pages/shell/SidePanel.cpp:33-49` |
| 右栏 | 3 段全是写死值 + 96px 空预览框，恒占 280px | `src/ui/pages/shell/RightPanel.cpp:50-75` |
| 底栏 | 4 个 tab 各一行占位文案，常驻 220px | `src/ui/pages/shell/BottomDock.cpp:25-35`；默认高 `MainWindow.cpp:733` |
| 顶栏点击反馈 | Toast「全流程运行由 P09 接入（当前为占位按钮）」 | `TopBar.cpp:574/579` |
| 命令面板 | 「新建章节」→ Toast「章节操作由 P04 接入」 | `MainWindow.cpp:875-878` |

---

## 方案

### 5.1 侧栏：每工作区一棵真树

统一结构：`SectionHeader` + `DataTree`（虚拟化，`kit/data/Table.h:68-85`）+ 空态 `EmptyState`。

| 工作区 | 侧栏内容 | 数据来源 |
|---|---|---|
| 总控 | 书/卷/章骨架 + 阶段进度概览 | `project::ListBooks`、`pipeline::AllStages()` |
| 小说 | 书 → 卷 → 章（懒展开） | `novelcore::NovelGraph`（`src/novel`） |
| 视觉资产 | 资产分层：角色 / 场景 / 道具 / 风格 | `novelcore::VisualAssetRow`（`src/ui/pages/assets/AssetWorkspace.h:77`） |
| 分镜 | 场景 → 镜头（`ShotRow`） | `novelcore::ShotRow` |
| 出图 | 工作流批次列表 + Comfy prompt_id | `flow::GraphHost`、`ComfyPanel` |
| 出片 | 视频任务列表（按镜） | `video::VideoTaskRunner`（`VideoTaskView`） |

规则：

- 懒展开：`DataTree::SetOnNeedChildren` 回调里再查库；
- 选中项与中央文档联动（点侧栏树 → 中央切到对应详情）；
- **禁止在 UI 线程查库**：所有查询走 `shine::async::RunOnWorker`，结果 `async::PostToUi` 回填（`AGENTS.md` 硬要求）；
- 空态文案示例：`还没有章节` / `从「初始化链」开始创建第一章` + 一个 `Button("初始化链")`。

### 5.2 右栏：Inspector（选中驱动）

```
┌ 属性 ────────────── 折叠 ┐
│ 章节   第 3 章          │
│ 标题   雨夜来客          │
│ 字数   2,431            │
│ 状态   已评审 ✓          │
├ 预览 ──────────────      ┤
│ [图 320×180]             │
├ 生成参数 ──────────      ┤
│ 后端   ComfyUI           │
│ 步数   20                │
│ 预算   40 次调用/章      │
└──────────────────────────┘
```

- 属性区从 `KeyValue` 取**当前选中项**的真实字段（不是写死字符串）；
- 预览区用 `images::ImageViewer` 的缩略模式（资产/分镜/出图/出片给图，小说给首段文本）；
- 生成参数只在选中可生成对象时出现（小说章节 / 视觉资产 / 镜头），否则整段不渲染；
- 无选中 → **整栏宽度 0**（04 已定），并在状态栏提示「未选中对象」；
- 宽内容（长文本）用 `KeyValue` 的折叠展开（`kit/data/Panels.h:27-33` 已有该能力）。

### 5.3 底栏：任务条 + 真实四页

**折叠态（默认，28px）**：

```
[● 空闲] 任务队列 3 · 阶段 T11 正文写作 · 用量 62%（24/40 章）        [展开 ▲]
```

**展开态（≥180px）** 四页接真实数据：

| Tab | 内容 | 来源 |
|---|---|---|
| 任务队列 | Comfy 队列 + 视频任务合并，按状态分组 | `flow::` Comfy 适配层 + `VideoTaskRunner` |
| 日志 | `logs/` 尾部滚动（虚拟化列表） | `shine::log` 落盘目录 |
| 产物 | 项目 `output/` 图片/视频缩略网格 | `kit/images/Grid.h`（`ThumbGrid`） |
| 校验报告 | 门禁结果表（K/G/C 检查） | 各阶段 checks 产物 |

规则：

- 队列有活动任务时，折叠条上有 `StatusDot(Busy)` + 计数，且**不允许自动折叠**；
- 日志读取走 worker，尾部增量追加，不整文件重读；
- 产物网格复用 `GalleryWorkspace.cpp:116-130` 的 worker 解码 + 虚拟加载写法，不要另写一份。

### 5.4 占位文案清理清单

| 位置 | 现有文案 | 改成 |
|---|---|---|
| `SidePanel.cpp:40-42` | 「本阶段（P03）只搭舞台…」 | `EmptyState` + 真实引导 |
| `RightPanel.cpp:61` | 「预览区（P05/P07 接入…）」 | 真实缩略 / 无预览则不显示该段 |
| `RightPanel.cpp:70-72` | 「后端 mock（P07 接 ComfyUI）」 | 真实后端与参数 |
| `BottomDock.cpp:26-29` | 4 条「由 P0x 接入」 | 真实数据 |
| `TopBar.cpp:574/579` | 「由 P09 接入（当前为占位按钮）」 | 未接线时按钮置灰 + tooltip「未就绪」，**不做假反馈** |
| `MainWindow.cpp:872/877` | 「由 P09/P04 接入（占位命令）」 | 同上，命令面板里置灰 |
| `MainWindow.cpp:790-791` | 「未连接（P07 接入健康检查）」 | 真实探测结果，或显示「未探测」 |

> 原则：**没有接通的东西就置灰，不要用 Toast 假装成功**。这与 `docs/00-overview/product.md:46`「不把『未校验』伪装成『校验通过』」同源。

---

## 改动清单

```
修改  src/ui/pages/shell/SidePanel.h/.cpp        6 个真实面板 + EmptyState + worker 查询
修改  src/ui/pages/shell/RightPanel.h/.cpp      选中驱动 Inspector
修改  src/ui/pages/shell/BottomDock.h/.cpp       折叠条 + 四个真实页
修改  src/ui/pages/shell/MainWindow.cpp         移除占位 Toast、接 Inspector 选中信号
修改  src/ui/pages/pipeline/PipelineWorkspace.cpp:73-77  「P09 接入」→ 真实预算/队列
修改  src/ui/pages/{novel,assets,storyboard,imageflow,videoflow}/*.cpp   提供选中项 DTO
修改  CMakeLists.txt                            如新增源文件
```

## 验收

```powershell
cmake --build build -j 8 --target ShineTVStudio
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_panels.png

# 大数据量回归（虚拟化不能退化）
$env:SHINE_P10_S1 = "$PWD\build\p10s1.txt"; & .\build\ShineTVStudio.exe
```

判据：

1. 全应用 grep「P0x 接入」「本阶段」「占位」**零命中**（排除注释与文档）。
2. 侧栏 6 个工作区各有不同内容；无数据时显示 `EmptyState` + 可点行动按钮。
3. 右栏在未选中时宽度为 0；在小说页选中一章后滑出，显示该章真实字段与首段文本。
4. 底栏默认 28px 单行；展开后 4 个 tab 均有真实内容（队列、日志、产物缩略图、校验表）。
5. 大项目（≥100 章、≥500 图）下侧栏/底栏滚动流畅，`SHINE_P10_S1` 的 p95 < 17ms 基线不破。
6. 顶栏「运行」在未接线时是**置灰态**（视觉可见），不再弹占位 Toast。
7. 所有新增查询路径确认在 worker 线程执行（代码走 `async::RunOnWorker` + `PostToUi`）。

## 风险

| 风险 | 缓解 |
|---|---|
| 侧栏接真实数据后打开变慢 | 懒展开 + 缓存 + worker；首屏只加载顶层节点 |
| Comfy 未启动时队列页空白 | 显示 `EmptyState`「ComfyUI 未连接」+ 「检查地址」按钮，不静默 |
| 产物网格大量图片解码卡顿 | 复用 `GalleryWorkspace.cpp:116-130` 的 worker + `PrefetchVisible` 虚拟加载 |
| 底栏日志文件很大 | 只读尾部 N 行；限制内存中的行数 |
