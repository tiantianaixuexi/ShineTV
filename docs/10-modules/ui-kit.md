---
id: modules.ui-kit
kind: reference
status: current
scope: ui
source_of_truth:
  - src/widget/theme/Token.h
  - src/widget/theme/Theme.h
  - src/widget/theme/ThemeService.h
  - src/widget/controls/WidgetCommon.h
  - src/widget/canvas/FlowCanvas.h
  - src/pages/shell/MainWindow.h
  - src/pages/shell/ActivityRail.h
  - src/pages/shell/CommandPalette.h
last_verified: 2026-09-25
---

# UI 套件与工作区

## 主题

`theme::ColorToken` 有 22 个颜色 token，点分名顺序由 `kColorTokenNames` 固定；四套内置主题为 `DeepSpace`、`Dusk`、`PaperInk`、`PolarNight`。主题 JSON 位于 `src/widget/theme/Themes/`，构建后复制到 exe 旁的 `themes/`。

颜色、间距、圆角、字体、阴影和动效的 Token 命名及常量在 `Token.h`。`ThemeService` 负责加载、切换、QSS 应用、持久化和减少动效；页面不能写硬编码样式色值。

## 控件约定

`kit::widgets` 的控件通过动态属性和全局 QSS 获得状态：

- 交互控件五态：`Normal`、`Hover`、`Pressed`、`Disabled`、`Focus`；
- 容器要表达有数据、空态、错误/降级；
- 语义色来自 `accent.*` / `status.*`；
- 页面只组合套件控件，不复制按钮、卡片、表格、状态标签和反馈控件。

`kit::data` 提供表格、树、阶段流、统计/键值、Diff、JSON 树等；`kit::images` 提供网格、查看器、对比和放大镜。

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
