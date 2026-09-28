---
id: modules.ui-kit
kind: reference
status: current
scope: ui
source_of_truth:
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/Theme.h
  - src/ui/kit/theme/ThemeService.h
  - src/ui/kit/theme/Palette.h
  - src/ui/kit/theme/QssBuilder.h
  - src/ui/kit/controls/WidgetCommon.h
  - src/ui/kit/canvas/FlowCanvas.h
  - src/ui/pages/shell/MainWindow.h
  - src/ui/pages/shell/ActivityRail.h
  - src/ui/pages/shell/CommandPalette.h
last_verified: 2026-09-28
---

# UI 套件与工作区

## 主题

`theme::ColorToken` 有 22 个颜色 token，点分名顺序由 `kColorTokenNames` 固定；四套内置主题为 `DeepSpace`、`Dusk`、`PaperInk`、`PolarNight`。主题 JSON 位于 `src/ui/kit/theme/Themes/`，构建后复制到 exe 旁的 `themes/`。

颜色、间距、圆角、字体、阴影和动效的 Token 命名及常量在 `Token.h`。`ThemeService` 负责加载、切换、QSS 应用、持久化和减少动效；页面不能写硬编码样式色值。

## 工具落点

- Qt-free 的通用工具（字符串/时间/文件/JSON/编码/静态反射/随机）放 `shine_core` 的 `src/util`，已注册为 header-only（不占 CMake 源文件清单）。新增工具放这里，不要放进 `src/ui`。
- `util::WriteFileEnsuredDir`（建父目录 + 写文件）与 `util::EnsureDir`（只建目录、自检层丢弃返回值）是「建目录 + 落盘」的唯一写法；不要再在业务层拼 `create_directories` + `WriteFileBytes` 两行。
- 依赖 Qt 的小助手放 `src/ui/layout/QtLayout.h`（`shine_core` 里唯一的 Qt 例外）。
- 已核实可用的 C++26 能力（GCC 16 + `-freflection`）：`util/Reflect.h` 用的就是 P2996 静态反射（`^^T` / `std::meta` / `[: :]`），不要再讨论「能不能替掉」。注意 GCC **不定义** `__cpp_reflection` 宏，探测特性要靠实际编译而不是查宏。

## 换肤契约（QSS + QPalette）

一次换肤下发两样东西，两者同源于同一组 Token：

| 通道 | 覆盖什么 | 生成处 |
|---|---|---|
| 全局 QSS | 被选择器命中的控件（含 `shineKind` 动态属性段） | `QssBuilder::Build` |
| `QPalette` | QSS 之外的绘制路径：QSS 里的 `palette(...)`、未被规则命中的控件、原生子绘制（弹层、内置对话框按钮、清空按钮） | `theme::PaletteFor`（`src/ui/kit/theme/Palette.h`） |

**源码事实**：应用此前只 `setStyleSheet`、从不 `setPalette`，未命中路径取的是系统浅色调色板，深色主题下表现为深底黑字。`ThemeService::ApplyQss` / `Preview` / `RevertPreview` 现在三条路径都同时下发调色板。

**约束**：颜色一律走 QSS 的 `%N` 占位符或 `theme::Current()` token 拼串；不要用内联 `setStyleSheet("color: palette(...)")` 绕开这条链路。`tools/check-layers.ps1` rule 3 只拦字面色值，`palette(...)` 这类「不写色值但脱离 Token」的写法靠本节约定兜。

## 控件约定

`kit::widgets` 的控件通过动态属性和全局 QSS 获得状态：

- 交互控件五态：`Normal`、`Hover`、`Pressed`、`Disabled`、`Focus`；
- 容器要表达有数据、空态、错误/降级；
- 语义色来自 `accent.*` / `status.*`；
- 页面只组合套件控件，不复制按钮、卡片、表格、状态标签和反馈控件。

`kit::data` 提供表格、树、阶段流、统计/键值、Diff、JSON 树等；`kit::images` 提供网格、查看器、对比和放大镜。

## 已知重复实现（源码事实，尚未收敛）

以下是 2026-09-28 全量普查定位到的重复点，**均已核实存在**，但本轮未合并。给后续改动者定位用，不是开发计划。

| 模式 | 位置 | 备注 |
|---|---|---|
| 手搓 `QTableWidget`（`kit::data::DataTable` 已有） | `pages/pipeline/{GanttView,LedgerView}.cpp`、`pages/imageflow/{BindingView,BatchRenderView}.cpp`、`pages/videoflow/{ChainView,VideoTaskView}.cpp` | 6 处；改用 `DataTable` 会连带影响评审侧定位 |
| 绕过 `kit` 直接用 `QTabWidget` | `pages/{Pipeline,VideoFlow,ImageFlow}Workspace.cpp` | 评审侧因此要用 `findChild<QTabWidget*>()` 反查 |
| 状态点自绘（3 套写法 + 2 份色值映射） | `pages/shell/StatusBar.cpp`、`pages/novel/NovelWorkspace.cpp`、`pages/project/ProjectHubView.cpp` | 尚无 `kit::StatusDot` |
| 空态手搓 `QLabel("暂无…")` 而非 `kit::EmptyState` | `pages/videoflow/ChainView.cpp`、`pages/pipeline/LedgerView.cpp`、`pages/imageflow/BindingView.cpp` 等 | `pages/storyboard/ContinuityView.cpp` 是正确用法样板 |
| 确认框三种写法并存 | `pages/assets/RefLibraryView.cpp`、`pages/novel/InitChainView.cpp`（原生 `QMessageBox`）、`pages/project/ProjectWizardDialog.cpp`（自绘） | `kit::Surfaces` 无 Confirm 变体 |
| 中文相对时间格式化 | `pages/project/ProjectHubView.cpp` | 业务无关，可进 `ui/layout/QtLayout.h` |
| 哈希短显示 `left(12)` | `pages/novel/DraftView.cpp` | 可加 `util::ShortHash()` |
| `QString::fromStdString` 与 `fromUtf8` 两种 `Text()` 助手语义不同 | `pages/novel/AutoRunPanel.cpp`（fromUtf8，正确）、`pages/imageflow/ImageFlowWorkspace.cpp`（fromStdString） | 中文路径下后者有编码风险 |

已收敛的部分见 `ui/kit/controls/WidgetCommon.h`（`SetTextColor` / `SectionTitle`）与 `ui/verify/review/ReviewProbe.h`（评审层 `Pump`/`Grab` 公共实现）。

## FlowCanvas

`kit::FlowCanvas` 是 Qt `QGraphicsView`，只接收 `FlowCanvasNode`/`FlowCanvasLink` DTO。它支持选择、删除、复制、全选、缩放、适配视图、连线和内嵌原生编辑器；出图与出片工作区共用这一实现。

## 应用外壳

`MainWindow` 有两种状态：

1. `ProjectHubView`：项目列表/新建入口；
2. 工坊：顶栏、活动栏、侧栏、中央文档页、右栏、底栏、状态栏。

活动栏目前提供总控、小说、资产、分镜、出图、出片入口。`Ctrl+B` 折叠侧栏，`Ctrl+J` 折叠底栏，`Ctrl+K` 打开命令面板。布局和最近项目状态写入 AppData 下的 `layout.dat`，恢复失败应回退默认布局并提示。

## 线程/渲染纪律

- 页面可以持有 DTO 和模型，不直接持有网络线程对象；
- 大列表使用模型/虚拟化，图片解码在 worker；
- 画布只通过 DTO 和回调与 `flow` 交互；
- 可见性验收必须启动实际窗口并检查截图，编译通过不代表布局正确。

## 关键符号

- `theme::Current` / `ThemeService::Switch`
- `widgets::SetForcedState`
- `kit::FlowCanvas`
- `app::MainWindow`
- `app::CommandPalette`
- `app::ActivityRail`
