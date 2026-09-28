---
id: design.index
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/kit/controls/
  - src/ui/pages/shell/MainWindow.cpp
  - docs/10-modules/ui-kit.md
last_verified: 2026-09-28
---

# ShineTV Studio 界面重设计 · 方案集

用户反馈：当前界面"太丑"，要求重做**信息结构、配色、交互细节**。本目录是一次性设计提案区，按依赖顺序拆成 8 份可独立执行的方案，外加一份现状审计。

## 为什么放在 `design/` 而不是 `docs/`

`AGENTS.md` 规定 `docs/` 只承载"总览/模块/契约/工程/运行参考/源码索引"，不放开发计划。本目录是**设计提案**，不是进度表——每份文档只描述"要改成什么样、怎么改、怎么验收"，执行完即冻结；不做状态跟踪、不写完成度百分比。

## 事实标记

沿用 `docs/README.md` 的口径：

- **源码事实**：带 `文件:行号`，可当场复核；
- **设计约定**：本文档提出的目标值，尚未实现；
- **待验证**：只有实际启动程序 + 截图比对后才算通过。

## 执行顺序

> **给执行者（AI 或人）**：先读 [`EXECUTE.md`](EXECUTE.md) 的铁律与执行循环，再按下面顺序做，一次一个。

| 序 | 方案 | 主题 | 改动面 | 依赖 |
|---|---|---|---|---|
| 00 | [`00-audit.md`](00-audit.md) | 现状审计（15 条问题 + 证据） | 只读 | — |
| 01 | [`01-tokens-color.md`](01-tokens-color.md) | 颜色语义重排 + 层级对比 + 四套主题重调 | `kit/theme` | 00 |
| 02 | [`02-type-icons.md`](02-type-icons.md) | 排版层级 + emoji 换成矢量图标 | `kit/controls`、`kit/icons` | 01 |
| 03 | [`03-scaffold.md`](03-scaffold.md) | 页面骨架 `PageScaffold` + 缺失控件补齐 | `kit/controls` + 10 个页面 | 02 |
| 04 | [`04-navigation.md`](04-navigation.md) | 导航层重构：rail 文字化、面包屑合并、栏宽策略 | `pages/shell` | 03 |
| 05 | [`05-panels.md`](05-panels.md) | 侧栏/右栏/底栏从占位文案换成真实内容 | `pages/shell` + 业务页 | 03, 04 |
| 06 | [`06-data-state.md`](06-data-state.md) | 表格收敛 `DataTable` + 甘特重做成状态矩阵 | `kit/data` + 6 个视图 | 01, 03 |
| 07 | [`07-feedback-motion.md`](07-feedback-motion.md) | 空态/错误态/确认框统一、动效与焦点可达性 | `kit/controls` | 03 |
| 08 | [`08-gallery-gates.md`](08-gallery-gates.md) | 设计检视台 + 新增门禁 + 文档回写 | `tools`、`verify`、文档 | 全部 |

建议按 01 → 08 顺序做。01/02 会让后面每一张截图都变好看，是性价比最高的两步。

## 每份方案的固定结构

```text
目标 → 不做什么 → 现状证据（文件:行号） → 方案（具体到值/结构）
→ 改动清单（文件 + 符号） → 验收（命令 + 判据） → 风险 → 工作量
```

## 通用验收手段

```powershell
# 1) 静态门禁（改完必跑）
pwsh -File tools/check-layers.ps1
pwsh -File tools/check-colors.ps1
pwsh -File tools/check-theme.ps1

# 2) 构建
cmake --build build -j 8 --target ShineTVStudio

# 3) 设计检视台截图（每份方案都应在这里能看到自己的产物）
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"
& .\build\ShineTVStudio.exe --widget-gallery

# 4) 主窗口截图
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_shot.png
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\crop_zoom.ps1 -In build\_shot.png -X 0 -Y 0 -W 900 -H 500 -Out build\_crop.png
```

截图只能证明可见结果；编译通过、进程存在不能替代交互验证（`docs/40-operations/verification.md`）。

## 三条不可越过的红线

1. **颜色只走 Token**：不写内联 `setStyleSheet` 色值，不写 `QColor(120,130,160)` 这类字面量（`tools/check-colors.ps1` 会拦）。自绘控件取 `theme::Current()`，经 `widgets::TokenQColor` 转换。
2. **core 不碰 Qt**：`src/core|util|net|db|llm|comfy|media|flow|visual|novel|paint|mcp|project|pipeline|gpu` 不引入 Qt 头（`tools/check-layers.ps1`）。
3. **UI 线程不做 IO**：新增的图库扫描、状态刷新、日志读取继续走 `shine::async::RunOnWorker` + `async::PostToUi`。

## 全局设计原则（本方案集统一遵守）

| 原则 | 含义 | 反例（当前存在） |
|---|---|---|
| 一屏一件事 | 每页只有一个主标题、一个主动作 | 顶栏同时有 5 个等权按钮 |
| 主动作唯一 | 一屏最多一个 primary 按钮 | 顶栏"▶ 运行"与页面"一键全流程"同时 primary |
| 空态给下一步 | 空数据区必须有说明 + 行动按钮 | 右栏"预览区（P05/P07 接入）" |
| 状态不只靠颜色 | 状态点 + 形状/文字双编码 | 全靠色点的色盲场景 |
| 占位不出现 | 不把"P04–P08 填充""占位按钮"写进用户可见界面 | 侧栏 6 个面板同一段占位文案 |
| 密度分层 | 表头 12 / 正文 13–14 / 标题 20–28 | 全局 10pt 基准 + 散落的 `setPixelSize` |
