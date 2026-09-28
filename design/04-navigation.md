---
id: design.p04-navigation
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/pages/shell/MainWindow.cpp
  - src/ui/pages/shell/ActivityRail.cpp
  - src/ui/pages/shell/SidePanel.cpp
  - src/ui/pages/shell/Breadcrumb.cpp
  - src/ui/pages/shell/TopBar.cpp
  - src/ui/pages/shell/StatusBar.cpp
  - src/ui/kit/controls/Navigation.h
  - src/ui/kit/theme/Token.h
  - docs/10-modules/ui-kit.md
last_verified: 2026-09-28
---

# 04 · 导航层重构：rail 文字化、面包屑合并、栏宽策略

**工作量**：中（集中在 `pages/shell`，约 6 个文件）
**风险**：中——涉及布局持久化（`layout.dat`）与既有评审探针的 `findChild` 定位
**收益**：高。截图里最刺眼的问题（信息重复 4 次 + 大片空白 + 中间 60% 空档）都在这一步解决。

## 目标

1. 同一个"当前在哪"只表达一次。
2. 活动栏可自解释（图标 + 文字），不依赖 tooltip。
3. 顶栏收敛为「项目切换 | 全局动作 | 环境状态」三段。
4. 分栏比例与最小宽度按内容重定，杜绝"中央 900 但只用 1/3、右侧 280 全空"。

## 不做什么

- 不做左右栏互换、不做可拖拽的任意布局（Dock 化）——超出收益。
- 不改工作区语义与 `WorkspaceId` 映射（`MainWindow.cpp:98-117`，被 `project.json` 的 `ui.lastWorkspace` 依赖）。

---

## 现状证据

| 问题 | 位置 |
|---|---|
| "当前工作区"被表达 4 次 | 侧栏标题 `SidePanel.cpp:28-30` + 面包屑 `MainWindow.cpp:834-842` + 文档标签 `MainWindow.cpp:603-613` + 页面标题 `PipelineWorkspace.cpp:23` |
| 顶栏中部 `addStretch()` 空档 | `TopBar.cpp:47` |
| 活动栏 56px 纯图标，只有 tooltip 说明 | `ActivityRail.cpp:32-49` |
| 分栏 `{56, 240, 900, 280}` 在 1900px 窗口下中央实际只占约 1/3 | `MainWindow.cpp:683` |
| 面包屑与文档标签是两行独立 chrome | `MainWindow.cpp:589-613` |
| 状态栏与顶栏重复「主题」入口 | `StatusBar.cpp:71` vs `TopBar.cpp:77`，同一菜单 `MainWindow.cpp:784-786` |
| 文档标签默认就存在（`总控台`），与工作区名重复 | `MainWindow.cpp:619` |

---

## 方案

### 4.1 活动栏：56px → 72px，图标 + 文字

```
┌────────┐
│ ▣ 总控  │  ← 40px 高条目，icon 20 + 文字 13px，选中态左侧 2.5px accent 条（保留现有自绘）
│ ▣ 小说  │
│ ▣ 资产  │
│ ▣ 分镜  │
│ ▣ 出图  │
│ ▣ 出片  │
└────────┘
```

- `ActivityRail.cpp:32` `setFixedWidth(56)` → `72`；条目高度 40 → 44，间距 `space[3]` → `space[2]`；
- 条目内部改为 `QVBoxLayout`（图标 20px 上 / 文字 12px 下）或 `QHBoxLayout`（图标 + 文字），二者取一，**在检视台定稿后再改**；
- 选中态：左侧 2.5×22 指示条（`ActivityRail.cpp:84-90`）改为 2.5×24 高亮块 + `bg.surface` 底 + 文字转 `accent.primary`；
- hover：`fill.hover`；
- 底部加一个分隔 + 「设置」入口（把顶栏的 ⚙ 移下来，顶栏因此少一个按钮）。

> 宽度从 56 → 72 只多占 16px，但换来"不需要 hover 才知道是什么"，这是活动栏最大的可用性缺口。

### 4.2 面包屑：合并进页头，标签行按需出现

**决策：删掉独立的面包屑行与独立的文档标签行中的默认标签。**

- `Breadcrumb` 不再占据整行（当前 `MainWindow.cpp:601` 是 `cl->addWidget(crumb_)`）。改为由 `PageScaffold` 的副标题承载：`项目 / 工作区 / 当前文档` 三级文本，用 `·` 分隔，`Caption` 12px `text.muted`。
- 文档标签行（`MainWindow.cpp:603-613`）改为**条件显示**：当 `doc_tabs_->count() > 1` 时才出现（用户确实开了多文档时才需要横向切换）。单文档时不占行。
- 面包屑的"点击第 0 项回项目中心"交互迁移到项目按钮菜单（`TopBar.cpp:28-45` 已有「项目列表…」），不再需要独立行。

净效果：垂直空间省下约 40px（面包屑 24 + 标签行 32 中的默认部分），且"当前在哪"只在页头出现一次。

### 4.3 顶栏三段式

```
[项目名 ▾]  [⋯ 主分组 | 次分组]                    [● Comfy  ● LLM   队列 3]  [进度]
```

| 段 | 内容 | 变化 |
|---|---|---|
| 左 | 项目切换按钮（保留） | 不变 |
| 中左 | 主分组：`▶ 运行` `⏹ 停止`；次分组：搜索（图标 + `⌘K` 提示） | 从右半挪到中部，紧邻项目名，符合"操作靠近上下文" |
| 中右 | — | 删掉 `addStretch()` 造成的大空档，改为弹性 spacer（宽度上限 40px） |
| 右 | 服务状态（Comfy/LLM/队列）+ 主题按钮 + 设置 | 健康状态从状态栏上移，与顶栏合并 |

**状态栏瘦身**：只剩「阶段进度（含进度条）| 预算 | 版本」。主题名从状态栏移除（顶栏已有）。健康状态点改用 `StatusDot`（03 方案）。

> 决策记录：健康信息同时出现在顶栏和状态栏，是"重复表达"。选顶栏，因为它跟"运行/停止"是同一组全局动作。

### 4.4 分栏与宽度策略

| 栏 | 现 | 目标 | 理由 |
|---|---|---|---|
| 活动栏 | 56 | 72 | 加文字标签 |
| 侧栏 | 240 | 280，下限 220 | 要放真实资源树（05） |
| 中央 | 900 | `stretch=1`，最小 640 | 真正的弹性区 |
| 右栏 | 280 | 320，下限 280，**默认隐藏**（宽度 0） | 无选中项时不该占 320px |

关键改动在 `MainWindow.cpp:677-684`：

```cpp
hsplit_->setSizes({72, 280, 1000, 0});   // 默认右栏折叠
hsplit_->setStretchFactor(2, 1);        // 只有中央吃剩余空间
hsplit_->setCollapsible(true);
```

右栏改为**选中驱动**：没有选中项时宽度为 0；选中后以 `motion::base` 滑出到 320。键盘 `Ctrl+I` 手动钉住/取消钉住。

### 4.5 内容最大宽度

中央内容区加最大宽度约束，避免 1900px 窗口下正文横跨 1300px（阅读行长过长）：

- 可视内容宽度上限 **1080px**，超出后左侧对齐（不居中——左对齐更符合工具型界面）；
- 表格类页面（06 方案）例外，允许撑满。

### 4.6 快捷键

| 键 | 动作 | 现状 |
|---|---|---|
| `Ctrl+1`…`Ctrl+6` | 切工作区 | 新增 |
| `Ctrl+B` | 侧栏 | 已有（`MainWindow.cpp:198`） |
| `Ctrl+I` | 右栏钉住 | 新增 |
| `Ctrl+J` | 底栏 | 已有（`:201`） |
| `Ctrl+K` | 命令面板 | 已有（`:202`） |
| `Ctrl+Enter` | 运行当前流程 | 已在命令面板（`:871`），需补全局快捷键 |
| `Ctrl+,` | 设置 | 新增（从顶栏菜单搬） |

### 4.7 布局持久化

`layout.dat` 结构变化（`MainWindow.cpp:961-970` 及 `RestoreLayout`）：

```jsonc
{
  "splitter": [72, 280, -1, 0],   // -1 = 中央自适应
  "railPinned": false,
  "inspectorPinned": false,
  "dockHeight": 28,               // 04 只改默认值，05 再改折叠行为
  "docTabsVisible": false
}
```

**向后兼容**：读不到 `docTabsVisible` 时按 `docTabsVisible = (count > 1)` 处理；`splitter` 长度不足或含负数时回退默认值 + Toast 提示（沿用 `MainWindow.cpp:231-234` 的既有容错路径）。

---

## 改动清单

```
修改  src/ui/pages/shell/ActivityRail.cpp/.h    72px、图标+文字、设置入口、选中态
修改  src/ui/pages/shell/TopBar.cpp/.h          三段式重排、健康状态上移、搜索移到中左
修改  src/ui/pages/shell/StatusBar.cpp/.h       瘦身为 进度/预算/版本
修改  src/ui/pages/shell/MainWindow.cpp/.h      去掉 Breadcrumb 行、标签行条件显示、
                                                分栏比例、右栏折叠、Ctrl+1..6、布局 JSON
修改  src/ui/pages/shell/Breadcrumb.h            保留类（CommandPalette/评审仍可能引用），降级为页头副标题生成器
修改  src/ui/pages/novel/…/各 Workspace        SetSubtitle(项目/工作区/文档)
删除或保留  src/ui/pages/shell/Breadcrumb.cpp     视引用情况
```

**已核实**：`src/ui/verify` 下没有任何 `objectName` 字符串匹配（grep `Breadcrumb|activityRail|topBar|sidePanel|rightPanel|bottomDock|statusBar` 零命中），所以外壳 `setObjectName` 的改动不影响评审探针。探针的定位手段只有三种：

| 探针 | 定位方式 | 位置 |
|---|---|---|
| P09Review | `findChild<QTabWidget*>()->setCurrentIndex(n)` | `P09Review.cpp:41,44,47` |
| P06Review | `dynamic_cast<T*>` 遍历 `findChildren<QWidget*>()` | `P06Review.cpp:51` |
| P04Review | 遍历 `findChildren<QLabel*>()` 做文本断言 | `P04Review.cpp:112` |

因此本方案（外壳）与 06 方案（换 `QTabWidget`）互不影响；但 **06 必须同步改 `P09Review.cpp`**。

## 验收

```powershell
cmake --build build -j 8 --target ShineTVStudio
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_shell.png

# 折叠判定自检（既有）
$env:SHINE_P03_FOLD = "1"; & .\build\ShineTVStudio.exe
# 多模态评审包（既有，验证探针未被改坏）
$env:SHINE_P03_REVIEW = "$PWD\build\p03"; & .\build\ShineTVStudio.exe
```

判据：

1. 主窗口截图中，"当前工作区"文字**只出现一次**（页头副标题）。
2. 活动栏每个条目**无需 hover 即可读出名称**。
3. 顶栏不再有大面积空白；运行/停止紧邻项目名。
4. 1900px 宽窗口下，正文列宽不超过 1080px，左对齐。
5. 首次进入项目时右栏宽度为 0；在任意表格选中一行后右栏滑出到 320。
6. `Ctrl+1`…`Ctrl+6` 切换工作区时，侧栏与页头副标题同步更新；活动栏指示条正确滑动。
7. 关窗重开后，栏宽、钉住状态、当前工作区全部恢复；`layout.dat` 损坏时回退默认并 Toast。
8. `SHINE_P03_FOLD` 与 `SHINE_P03_REVIEW` 自检退出码 0。

## 风险

| 风险 | 缓解 |
|---|---|
| 删掉面包屑行后评审探针失配 | **已核实：`src/ui/verify` 全目录不含 `objectName` / `Breadcrumb` / `sidePanel` 等字符串**（grep 零命中），探针用的是 `findChild<QTabWidget*>` / `dynamic_cast` / 文本匹配（`P09Review.cpp:41-47`、`P06Review.cpp:51`、`P04Review.cpp:112`）。改外壳不会撞到探针 |
| 活动栏加宽挤占中央 16px | 中央最小宽 640，1280 窗口下仍可用 |
| 右栏默认隐藏改变既有用户肌肉记忆 | 状态栏加一行"已选 N 项"提示；`Ctrl+I` 可永久钉住 |
| `layout.dat` 旧格式 | 显式兼容分支 + 回退 Toast，不静默失败 |
