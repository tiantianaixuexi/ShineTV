---
id: design.audit
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/pages/shell/MainWindow.cpp
  - src/ui/pages/shell/TopBar.cpp
  - src/ui/pages/shell/SidePanel.cpp
  - src/ui/pages/shell/RightPanel.cpp
  - src/ui/pages/shell/BottomDock.cpp
  - src/ui/pages/shell/StatusBar.cpp
  - src/ui/pages/shell/ActivityRail.cpp
  - src/ui/pages/pipeline/GanttView.cpp
  - src/ui/pages/pipeline/PipelineWorkspace.cpp
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/app/AppEntry.cpp
last_verified: 2026-09-28
---

# 00 · 现状审计

以用户提供的 1900×880 主窗口截图（工坊 / 总控 / 总控台）为基准，对照源码逐条定位。**每条都有可复核的 `文件:行号`**，不是主观评价。

## 结论先行

界面难看不是"配色不好看"这一个原因，而是四层问题叠加：

1. **信息结构**：一屏里有 5 个导航入口在表达同一件事（工作区名出现 4 次），同时 3 个区域（侧栏 / 右栏 / 底栏）用固定像素常驻却装着占位文案。
2. **配色语义**：主色与"运行中"共用同一个色值，四级背景色阶的明度差太小，导致界面"糊成一片"。
3. **排版**：全局只有 10pt 一个基准字号，字号靠各处散落的 `setPixelSize` 拼出来，没有层级。
4. **一致性**：同一屏里同时存在两套 tab 样式、两套表格实现、四套硬编码 `22`。

---

## A. 信息结构（占问题量的一半）

### A1 三层导航 chrome 叠层，且信息重复

截图上从上到下依次是：面包屑 `灯语司声 ▾ / 总控 / 总控台` → 文档标签 `总控台 ×` → 中央页面标题 `全流程总控台 · 一句话到成片`。

同一个"当前在总控"的事实被表达了三次（面包屑、文档标签、页面标题），再加上左侧栏的标题「总控」是第四次。

- 面包屑：`src/ui/pages/shell/MainWindow.cpp:589-601`、`834-842`
- 文档标签行：`src/ui/pages/shell/MainWindow.cpp:603-613`
- 页面标题：`src/ui/pages/pipeline/PipelineWorkspace.cpp:23`
- 侧栏标题：`src/ui/pages/shell/SidePanel.cpp:28-30`

四层外壳（顶栏 / 面包屑 / 标签行 / 内容）合计占约 150px 垂直空间，而 `MainWindow.cpp:683` 的分栏比例 `{56, 240, 900, 280}` 把 280px 宽度给了右栏、900px 给了中央——在 1900px 窗口下中央内容实际只用了约 1/3，右侧留出大片空白（见 `05-panels.md`）。

### A2 侧栏是 6 份相同的占位文案

`SidePanel` 为 6 个工作区各生成一页，内容完全相同：

```
侧栏 · 总控 面板
本阶段（P03）只搭舞台：七区布局、分栏与持久化。
该面板的内容由后续阶段填充——
小说 P04 · 资产 P05 · 分镜 P06 · 出图 P07 · 出片 P08 · 总控 P09。
```

- `src/ui/pages/shell/SidePanel.cpp:33-49`

240px 宽、整屏高度的侧栏，只显示这段字。

### A3 右栏是假数据，恒占 280px

`RightPanel` 的三段全是写死的值：`项目（未选中）` / `工作区 总控` / `标签页 —`、96px 高的空白预览框、`后端 mock（P07 接 ComfyUI）` / `步数 20` / `预算/章 40 次调用`。

- `src/ui/pages/shell/RightPanel.cpp:50-75`

### A4 底栏 4 个 tab 各一行占位文案，常驻 220px

```
任务队列 → "任务队列由 P07 接入（Comfy 队列与视频任务）。"
日志     → "运行日志由 P09 汇总（logs/ 目录）。"
产物     → "产物浏览器指向项目 output/ 目录（图 / 视频 / 成片）。"
校验报告 → "校验报告由 P04-P06 各阶段门禁产出（K / G / C 检查）。"
```

- `src/ui/pages/shell/BottomDock.cpp:25-35`；默认展开高度见 `MainWindow.cpp:733`

### A5 顶栏把"项目切换"和"全局动作"挤在一行，等权

顶栏从左到右：项目按钮 → 巨大空白 `addStretch()` → 搜索命令 / ▶运行 / ⏹停止 / 🎨主题 / ⚙设置。右侧 5 个控件等权且都是文字按钮，左侧只有一个项目名。

- `src/ui/pages/shell/TopBar.cpp:46-92`

截图里 `addStretch()` 造成中间约 60% 宽度是空的，所有动作堆在右上角。

### A6 状态栏与顶栏职责重叠

顶栏有「🎨 主题」、状态栏有「深空」（点击弹同一菜单，`TopBar.cpp:110-112` / `MainWindow.cpp:784-786`）；状态栏同时显示 Comfy/LLM 健康，而这两个状态在其他页面并不总是相关。

- `src/ui/pages/shell/StatusBar.cpp:59-84`、`MainWindow.cpp:761-773`

---

## B. 配色

### B1 主色与"运行中"撞色

深空主题里：

```
accent.primary  #3ECFB2   ← 主按钮 / 焦点环 / 活动栏指示条
status.busy     #3ECFB2   ← 运行中
```

- `src/ui/kit/theme/Themes/深空.json:16`、`src/ui/kit/theme/Themes/深空.json:24`

薄暮主题同样 `accent.primary = status.busy = #F2A65A`，纸墨主题同样 `#0E8C7A`。结果是"运行中"和"这是主按钮/当前选中"视觉上完全无法区分。

### B2 四级背景色阶的明度差太小

深空主题四级背景：

| token | 值 | 与上一级的差 |
|---|---|---|
| `bg.void` | `#0B0E14` | — |
| `bg.surface` | `#121A24` | Δ ≈ 1.5% L* |
| `bg.panel` | `#182230` | Δ ≈ 1.5% L* |
| `bg.elevated` | `#1C2733` | Δ ≈ 0.9% L* |

- `src/ui/kit/theme/Themes/深空.json:4-7`

这解释了截图里"中央页面、右侧面板、底部区域看起来是同一块底"。另外 `bg.surface`（画布）与 `bg.panel`（卡片）的区分几乎不可见，卡片因此没有"浮起来"的感觉。

### B3 状态轴缺一档，且没有"排队中"

现有状态色：`status.idle` / `status.busy` / `status.ok` / `status.warn` / `status.danger`（`src/ui/kit/theme/Token.h:41`）。Comfy 队列与流水线实际有 6 态（空闲 / 排队 / 运行 / 完成 / 降级 / 失败），"排队"只能借用 `idle`，与"未开始"混同。

### B4 交互态色缺失，选中/悬停没有专门 token

QSS 里的 hover/selected 直接复用 `bg.elevated(%4)` 与 `bg.panel(%3)`，导致"悬停"和"卡片底色"同源；表格斑马行 `alternate-background-color: %3` 与卡片同色，视觉噪声大。

- `src/ui/kit/theme/QssBuilder.cpp:141`（`alternate-background-color: %3`）、`QssBuilder.cpp:194`（button secondary 用 `%4`）

### B5 token 数量 22 被硬编码在 6 处

新增 token 会同时打断这些点：

- `src/ui/kit/theme/Theme.cpp:143-144`（`std::array<std::uint32_t, 22>` ×2）
- `src/ui/kit/theme/QssBuilder.cpp:446`、`461`、`470`
- `src/ui/pages/settings/StyleEditorDialog.cpp:65`、`78`、`147`

而 `QssBuilder.cpp:447-448` 的 `%N` 是**位置编号**，在中间插入 token 会让整张 QSS 模板的编号全部错位。

---

## C. 排版

### C1 全局只有一个基准字号，排版靠散落的 setPixelSize

- 全局：`AppEntry.cpp:87-93` 设 `QFont` 10pt（约 13px），之后 **QSS 里没有任何 `font-size`**（`QssBuilder.cpp` 全文无字号声明）。
- 于是字号在 ~10 处各自决定：`Panels.cpp:56`（26px 大数字）、`ProjectHubView.cpp:117/171/380/392`（kSizes[5]/[5]/[2]/[0]）、`NovelWorkspace.cpp:178`（pointSize+2）、`AutoRunPanel.cpp:78`、`ReviewView.cpp:161`（pointSizeF-1）、`StateDiffView.cpp:133`、`Flow.cpp:226/230`（11px / 9px 自绘）。

`theme::font::kSizes = {11, 12, 13, 15, 18, 26}`（`src/ui/kit/theme/Token.h:71`）定义了 6 档，但 QSS 不消费它，控件也不按档取值。

### C2 缺页面级标题层级

`WidgetCommon.h:41-44` 的注释自己承认：「页面里『new QLabel + SetKind(statetitle) + SetSemibold』三行块重复出现二十余处」。`SectionTitle` 把所有层级的标题压成同一个样式，截图中「全流程总控台 · 一句话到成片」与页面里的小节标题视觉权重相同。

---

## D. 一致性

### D1 甘特表：31 列文本"待办"

```cpp
table_->setItem(chapter, stage + 1, new QTableWidgetItem(QStringLiteral("待办")));
```

- `src/ui/pages/pipeline/GanttView.cpp:36`
- 阶段表共 30 个阶段（T1–T17 + V0–V11）：`src/pipeline/StageMachine.cpp:11-19`

问题叠加：① 每格都是文字，没有状态色；② 表头只有 `T1 T2 … V11` 代码，没有阶段名；③ 叙事段（T）与视觉段（V）混在同一行不分组；④ 走的是原生 `QTableWidget`，与 kit 的 `DataTable` 不是一套；⑤ `setSectionResizeMode(ResizeToContents)` 让 31 列只占约 1/3 宽度，右侧全空。

### D2 一屏两套 tab 样式

中央用原生 `QTabWidget`（`PipelineWorkspace.cpp:38-44`），底栏用 `kit::widgets::Tabs`（`BottomDock.cpp:21-23`）。前者走 QSS 的 `QTabBar::tab`，后者是 kit 自绘指示条。`docs/10-modules/ui-kit.md:66` 已记录：`pages/{Pipeline,VideoFlow,ImageFlow}Workspace.cpp` 共 3 处绕过 kit。

### D3 页面内边距不统一

`PipelineWorkspace.cpp:21` 是 `setContentsMargins(0,0,0,0)`（内容贴边），`GalleryWorkspace.cpp:67-68` 是 `kSteps[2]`（4px）。同一应用里两种页面节奏。

### D4 6 处手搓 `QTableWidget` 与 `DataTable` 并存

`docs/10-modules/ui-kit.md:65` 记录：`pages/pipeline/{GanttView,LedgerView}`、`pages/imageflow/{BindingView,BatchRenderView}`、`pages/videoflow/{ChainView,VideoTaskView}`。

### D5 emoji 当图标用

| 位置 | 字形 |
|---|---|
| 活动栏 | `⌂ 📖 🎭 🎬 🖼 🎞`（`ActivityRail.cpp:22-26`） |
| 顶栏 | `🔍 ▶ ⏹ 🎨 ⚙`（`TopBar.cpp:50-92`） |
| 搜索框前缀 | `🔍`（`Inputs.cpp:657`） |
| Toast | `✔ ! ✕ ℹ`（`Surfaces.cpp:193-196`） |
| 空态/错误态 | 图标参数、`⚠`（`Feedback.cpp:41/88`） |

`Controls.h:67` 的注释写着「P03 换真图标」，至今未换。彩色 emoji 在深色底上与整套单色 Token 体系冲突，且跨机器渲染不一致（截图里活动栏图标明显比其他控件"花"）。

### D6 状态点自绘 3 套、色值映射各写一遍

`StatusBar.cpp:16-44` 是其中一套（`Dot` 类 + tone→token 的 if 链），`pages/novel/NovelWorkspace.cpp`、`pages/project/ProjectHubView.cpp` 另有两套。`docs/10-modules/ui-kit.md:67` 已记录。

### D7 确认框三种写法并存

`pages/assets/RefLibraryView.cpp`（自绘）、`pages/novel/InitChainView.cpp`（原生 `QMessageBox`）、`pages/project/ProjectWizardDialog.cpp`（自绘）。原生对话框在深色主题下不受 QSS 完整控制。`docs/10-modules/ui-kit.md:69` 已记录。

---

## E. 反馈与可达性

- **焦点环用边框宽度模拟**：`QssBuilder.cpp:209` 等处用 `border: 2px solid %13; padding: 4px 13px`（比常态各减 1px）来避免布局跳动。方案可行但没有外描边（offset），在密集表格里几乎看不见。
- **卡片 hover 是位移**：`Card::Lift` 靠移动 1px 实现（`Controls.h:94-101`），会与相邻内容产生 1px 抖动。
- **状态信息只在色点**：`StatusBar.cpp:16-44` 的 9px 圆点是唯一的状态载体，无形状/文字冗余编码。
- **占位文案泄漏到界面**：`TopBar.cpp:574/579`、`MainWindow.cpp:872/877` 里点了「运行」「新建章节」会 Toast「全流程运行由 P09 接入（当前为占位按钮）」——开发阶段语言直接暴露给用户。

---

## F. 缺失的 kit 能力（重做时必须先补）

| 缺失 | 后果 | 证据 |
|---|---|---|
| 页面骨架（页头/工具条/内容区） | 每个页面自己拼 padding 和标题 | `GalleryWorkspace.cpp:65-105` vs `PipelineWorkspace.cpp:19-45` |
| 状态点控件 | 3 套自绘 | `docs/10-modules/ui-kit.md:67` |
| 确认对话框变体 | 3 种写法 | `docs/10-modules/ui-kit.md:69` |
| 阶段状态矩阵控件 | 甘特只能画成文本表 | `GanttView.cpp:36` |
| 图标资源 | 只能用 emoji | `Controls.h:67` 注释 |
| 排版角色（Role） | 字号散落 10 处 | 见 C1 |

---

## 证据清单核对表

做方案时可直接引用以下已核实事实：

- 主题 4 套、颜色 token 22 个：`src/ui/kit/theme/Token.h:15`、`:48`
- 阶段 30 个（T1–T17 / V0–V11）：`src/pipeline/StageMachine.cpp:11-19`
- 布局分栏 `{56, 240, 900, 280}`：`src/ui/pages/shell/MainWindow.cpp:683`
- 快捷键 Ctrl+B / Ctrl+J / Ctrl+K：`MainWindow.cpp:197-203`
- 工作区 6 个：`SidePanel.cpp:12-17`
- 底栏 4 个 tab：`BottomDock.cpp:21-23`
- 设计检视台入口 `--widget-gallery` + `SHINE_GALLERY_SHOTS`：`src/ui/verify/checks/P02Checks.cpp:120-126`
- 截图脚本已知坑（DX11 黑屏 / 前台窗口 / 窗口越界）：`scripts/capture_window.ps1:9-14`
