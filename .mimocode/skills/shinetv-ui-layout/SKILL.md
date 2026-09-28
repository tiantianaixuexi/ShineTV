---
name: shinetv-ui-layout
description: ShineTV 当前 Qt Widgets 七区工作区、主题控件、FlowCanvas、快捷键和 UI 线程/性能约定。
---

# Qt UI 布局

先读：[UI 套件](../../../docs/10-modules/ui-kit.md)、[运行时](../../../docs/00-overview/runtime.md)。

## 外壳

`src/app/shell/MainWindow` 有项目中心两态：ProjectHub 与工坊。工坊七区：

1. 顶栏 `TopBar`
2. 活动栏 `ActivityRail`
3. 侧栏 `SidePanel`
4. 中央文档页/工作区
5. 右侧检查器 `RightPanel`
6. 底部队列 `BottomDock`
7. 状态栏 `StatusBar`

活动栏入口为总控、小说、资产、分镜、出图、出片。`Ctrl+B` 折叠侧栏，`Ctrl+J` 折叠底栏，`Ctrl+K` 打开命令面板。

## 套件

- `src/kit/theme`：`theme::Current`、Token、QSS、主题切换；
- `src/kit/widgets/data/images`：通用控件、数据展示、图片查看；
- `src/kit/canvas/FlowCanvas`：共享节点画布；
- `src/kit/motion`：统一动效。

页面从套件取控件，不复制颜色/按钮/表格/状态反馈实现。颜色只能来自 Token，主题切换由全局 QSS 完成。

## 线程与性能

- 15ms UI 定时器执行 `DrainUiQueue` 和图库 tick；
- 扫描、解码、下载、LLM、生成在 worker；结果回 UI 后才改模型；
- 大列表使用模型/虚拟化，图片不在 UI 线程解码；
- 画布消费 DTO，不直接访问 flow 内部对象。

## 验证

UI 改动要实际启动程序、走对应工作区并检查截图/状态；`tools/check-colors.ps1` 与 `tools/check-layers.ps1` 是静态门禁。
