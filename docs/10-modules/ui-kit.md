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
last_verified: 2026-09-29
---

# UI 套件与工作区

## 主题

`theme::ColorToken` 有 28 个颜色 token（数量 = `theme::kColorTokenCount`，别再写死字面量），点分名顺序由 `kColorTokenNames` 固定；四套内置主题为 `DeepSpace`、`Dusk`、`PaperInk`、`PolarNight`。主题 JSON 位于 `src/ui/kit/theme/Themes/`，构建后复制到 exe 旁的 `themes/`。

**源码事实（design/01 落地）**：token 只许在 `ColorToken` / `kColorTokenNames` 末尾追加 —— 三者顺序（结构体字段 / 点分名 / QSS `%N`）必须逐位一致，`QssBuilder::FillTokens` 按 `%N` 位置替换，中途插入会让整张 QSS 错位。`bg.overlay` 现在是**不透明浮层底**（弹层 / 抽屉 / 提示 / 下拉弹窗），浮层遮罩走 `shadow.scrim`（QSS `*[shineKind="scrim"]`）。用户自建的自定义主题缺新键时不会整体加载失败：`LoadCustomThemes` 以当前内置主题为底做缺键补全。

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

### 换肤后重挂页面级 QSS：走 `widgets::RefreshOnThemeChange`，不要等 `QEvent::ThemeChange`

页面根控件常有一份**页面专属 QSS**（`PageQss()` 之类），色值从 `theme::Current()` 现算，
换肤后必须重算。统一入口：

```cpp
widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
```

> ⚠️ **不要在 `qApp` 上装事件过滤器等 `QEvent::ThemeChange`。**
> 换肤走 `ThemeService::ApplyQss`，它只做 `app->setPalette()` + `app->setStyleSheet()`，
> **全树没有任何地方 post `ThemeChange`**，所以那种过滤器永远不触发——换肤后
> 页面 QSS 静默停在旧主题（全局 QSS 已更新，于是出现「局部旧、局部新」的混色）。
> 旧代码的注释「见 ApplyQss 里的 QEvent::ThemeChange」是**错的**。
>
> 离屏探针实测（Qt 6.11.2）：`qApp` 只收到 `ApplicationPaletteChange(38)`；
> 控件收到 `PaletteChange(39)` 与 `StyleChange(100)`；`ThemeChange(210)` 永不到达。
> `RefreshOnThemeChange` 收的就是**控件级**这三个事件，并内置重入保护
> （回调里 `setStyleSheet` 会再次派发 `StyleChange`）。
>
> 2026-09-29 已把 14 份各写各的 `PageStyleRefresher` / `ToolsStyleRefresher` /
> `ShellStyleRefresher` 等收敛到这一个入口。

`ApplyShadow` 内部也走同一机制（每个控件自带重挂回调），因此阴影色跟随换肤。

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
| `setStyleSheet("color: …")` 内联写 token 色（绕过 QSS） | `pages/novel/ReviewView.cpp`、`pages/novel/StateDiffView.cpp` 等 | `kit::data::Panels.cpp` 的 KPI 涨跌色已改为 `shineVariant="up/down"` + QSS 规则 |

已收敛的部分见 `ui/kit/controls/WidgetCommon.h`（`SetTextColor` / `SectionTitle`）与 `ui/verify/review/ReviewProbe.h`（评审层 `Pump`/`Grab` 公共实现）。

## 页面留白与设计稿数值来源

页面最外层布局统一调 `shine::util::PageMargins()` / `PageSpacing()`（`ui/layout/QtLayout.h`），
数值与 `webui/src/styles/views.css` 的 `.vw` 逐值对齐：`padding 20px 24px 26px`、`gap 16px`。
新增页面不要再直接写 `theme::space::kSteps[]` 下标。

控件的几何与排版数值（按钮 h30/r6/f13/w600、标签 h20/f11.5/w600、卡片 r10、输入 h30/r6、
进度条 h6/pill、表头 f11.5/w600、页签 p8 12/f13/w600/选中 accent+2px 下划线）
统一写在 `kit/theme/QssBuilder.cpp` 的 kit 样式段里，**不在页面里写几何值**。

Qt 无法 1:1 移植的设计稿特性（`box-shadow`、`backdrop-filter`、CSS transform、
transition）、`QPushButton` 挂子布局后必须覆写 `sizeHint`、`QPlainTextEdit` 忽略
`textIndent` 等控件行为限制，以及资产详情 `.tl` 时间线等数据缺口，统一记录在
[`90-reference/ui-design-parity-gaps.md`](../90-reference/ui-design-parity-gaps.md)。
改 UI 前先看那份文件，别重复踩同一类坑。

`kit::canvas::FlowCanvas` 的节点自绘同样走 `theme::Current()` token
（节点 w150 / r10 / 1.5px 边 / 标题 12px w700 + accent 标记 / 端口 11px muted），
四套主题下节点与连线随主题变化。

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
- 缩略图统一走 `images::SetThumbAsync(QLabel*, path, box, mode, fallback, on_ready)`：
  它在 worker 上 `setScaledSize()` **按目标尺寸下采样后再解码**（不是把原图读进内存再缩），
  并用 `QPointer` 守着目标控件。**不要在 UI 线程循环里 `QImageReader::read()`**
  ——资产页原来就是这么写的，一张 4000×3000 的 PNG 就能冻住整个页面；
- 画布只通过 DTO 和回调与 `flow` 交互；
- 可见性验收必须启动实际窗口并检查截图，编译通过不代表布局正确。

## 关键符号

- `theme::Current` / `ThemeService::Switch`
- `widgets::RefreshOnThemeChange`（换肤后重挂页面 QSS / 阴影）
- `images::SetThumbAsync`（worker 解码缩略图）
- `util::ClearLayout(lay, keep_tail)`（清布局；`keep_tail` 保留末尾 N 项，如常驻的 `addStretch`）
- `widgets::SetForcedState`
- `kit::FlowCanvas`
- `app::MainWindow`
- `app::CommandPalette`
- `app::ActivityRail`
