---
id: design.p07-feedback-motion
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/kit/controls/Feedback.h
  - src/ui/kit/controls/Surfaces.h
  - src/ui/kit/controls/Controls.h
  - src/ui/kit/motion/Tween.h
  - src/ui/kit/motion/Easing.h
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/verify/gallery/QssStateProbe.cpp
  - docs/10-modules/ui-kit.md
  - docs/40-operations/verification.md
last_verified: 2026-09-28
---

# 07 · 空态 / 错误态 / 确认框统一、动效与焦点可达性

**工作量**：中
**风险**：低——局部替换，不动结构
**收益**：中——单看不明显，但决定"这软件成熟不成熟"

## 目标

1. 全应用统一用 `EmptyState` / `ErrorState` / `Confirm`，消灭手搓 `QLabel("暂无…")` 与原生 `QMessageBox`。
2. 焦点环改为「2px accent + 2px 外偏移」，在密集表格里也看得见。
3. 动效统一到 `motion::fast/base/slow` 三档，hover 一律 120ms；卡片反馈从位移改为描边/阴影过渡。
4. 「减少动效」开关全链路生效（含新增动效）。

## 不做什么

- 不做动画库/转场特效。
- 不改任何业务回调。

---

## 现状证据

| 问题 | 位置 |
|---|---|
| 空态手搓 `QLabel("暂无…")` 至少 3 处 | `docs/10-modules/ui-kit.md:68`（正确样板：`pages/storyboard/ContinuityView.cpp`） |
| 确认框三种写法（自绘 / 原生 `QMessageBox` / 自绘） | `docs/10-modules/ui-kit.md:69`：`RefLibraryView.cpp`、`InitChainView.cpp`、`ProjectWizardDialog.cpp` |
| 焦点环用 border 宽度模拟（无外偏移） | `src/ui/kit/theme/QssBuilder.cpp:209-214`、`:229-230`、`:304-305` |
| 卡片 hover 是 1px 位移 | `src/ui/kit/controls/Controls.h:94-101`（`Lift`） |
| Toast 右下角固定 4s（error 8s） | `src/ui/kit/controls/Surfaces.h:53-54` |
| 动效常量已定义但使用零散 | `src/ui/kit/theme/Token.h:94-107` |
| 五态探针已存在，可直接复用 | `src/ui/verify/gallery/QssStateProbe.cpp` |

---

## 方案

### 7.1 空态统一

`kit::widgets::EmptyState(icon, title, subtitle, actionText)` 已存在（`Feedback.h:33-43`）。统一用法：

| 场景 | title | subtitle | action |
|---|---|---|---|
| 没有任何章节 | 还没有章节 | 从「初始化链」创建第一章，设置世界与角色状态 | 初始化链 |
| 没有资产 | 还没有视觉资产 | 资产是分镜与出图的输入基准 | 生成资产 |
| 没有生成记录 | 还没有出图记录 | 提交后 ComfyUI 会在此显示进度 | 去出图 |
| 队列为空 | 队列空闲 | 提交任务后在这里显示进度 | — |

规范：

- 空态垂直居中，占位区高 ≥ 160px；
- 图标用 02 方案的 `Glyph`（空心容器 / 文件轮廓），24–32px，`text.muted`；
- **每个空态必须有下一步**（`Feedback.h:33` 的注释已写明这个要求）。

替换点（`docs/10-modules/ui-kit.md:68` 列出的 3 处 + 各页零散"（暂无摘要）"，如 `WorldBoardView.cpp:1036`）。

### 7.2 错误态统一

`ErrorState(title, detail)` + 「重试」+「查看详情」。

- 错误文案要说**发生了什么 + 影响**（"ComfyUI 连接失败（127.0.0.1:8188），出图无法提交"），不是"出错了"；
- 详情折叠进 Drawer，展示原始错误串；
- **不用 Toast 承载错误**：Toast 会自动消失，错误需要驻留。

替换：`MainWindow.cpp:305-311`（打开项目失败现在只弹 Toast）、`GalleryWorkspace.cpp:157-160`、`MainWindow.cpp:791`（"未连接（P07 接入健康检查）"）。

### 7.3 确认框统一

`kit::widgets::Confirm::Ask(parent, title, body, okText, {danger})`。

- 危险操作（删除资产、丢弃状态、覆盖账本）用 `danger` 变体 + 按钮文本写清后果（"丢弃未提交的 12 条状态修改"），不用"确定/取消"；
- 非破坏性操作直接执行，不加确认（过度确认同样让人烦）。

替换：`RefLibraryView.cpp`、`InitChainView.cpp`、`ProjectWizardDialog.cpp`。

### 7.4 焦点与键盘

QSS 现状（`QssBuilder.cpp:209`）：

```css
*[shineKind="button"]:focus { border: 2px solid %13; padding: 4px 13px; }
```

改法：Qt QSS 不支持 `outline`/`box-shadow`，因此用 **双层边框**模拟外偏移：

```css
*[shineKind="button"]:focus {
  border: 2px solid %27;      /* line.focus */
  padding: 4px 13px;          /* 保持不跳动 */
}
```

真正带外偏移的方案是自绘焦点框（`QPainter::drawRoundedRect` 外扩 2px）。**决策**：先做双层边框版（零成本），在检视台对比后再决定是否自绘；表格单元格选中态（`DataTable` / `StateMatrix`）必须走自绘焦点框，因为 QSS 命不中 item delegate。

键盘可达性检查清单：

- 所有 `IconButton` 已有强制 tooltip（`Controls.cpp:139`）；
- `DataTable` / `StateMatrix` 支持方向键移动焦点、`Space` 选中、`Enter` 打开详情；
- 焦点顺序：顶栏 → 活动栏 → 侧栏 → 页头工具条 → 内容 → 右栏 → 底栏；
- `Esc` 逐层关闭：浮层 → 右栏 → 底栏 → 命令面板。

### 7.5 动效

| 交互 | 时长 | 缓动 | 变化 |
|---|---|---|---|
| hover 反馈（所有控件） | 120ms `motion.fast` | `kStandard` | 统一，现在各控件无过渡 |
| 卡片 hover | 120ms | `kStandard` | **由 1px 位移改为描边色 + 阴影淡入**，消除抖动 |
| 面板折叠/展开 | 200ms `motion.base` | `kStandard` | 保持（`MainWindow.cpp:713`） |
| 活动栏指示条 | 200ms | `kStandard` | 保持（`ActivityRail.cpp:71`） |
| 右栏滑出 | 200ms | `kEmphasized` | 新增 |
| 抽屉 / 对话框 | 200ms | `kEmphasized` | 保持 |
| 状态点呼吸 | 1600ms 循环 | `linear` | 新增，遵守 `ReduceMotion` |

`Card::Lift`（`Controls.h:94-101`）改为在 `enterEvent/leaveEvent` 里切 `shineVariant` 或 `shineState`，由 QSS 改边框色——**不再移动控件**。

「减少动效」：现有 `shine::motion::ReduceMotion()`（`Theme.h`/`MotionScope`）与顶栏菜单开关（`TopBar.cpp:147-152`）保留；本方案新增的呼吸动画、滑出都要检查这个开关。验收时用画廊的 `gallery-motion-reduced.png`（`WidgetGalleryView.cpp:933-940`）。

---

## 改动清单

```
修改  src/ui/kit/controls/Feedback.h/.cpp      EmptyState/ErrorState 接 Glyph + 统一规格
修改  src/ui/kit/controls/Surfaces.h/.cpp      Confirm 变体；Toast/Error 分级策略
修改  src/ui/kit/controls/Controls.h/.cpp      Card::Lift → 描边/阴影过渡
修改  src/ui/kit/theme/QssBuilder.cpp          focus 规则统一到 %27；hover 过渡；卡片选中态
修改  src/ui/kit/data/Table.cpp                键盘导航 + 自绘焦点框
新增  src/ui/kit/data/StateMatrix.cpp          键盘导航 + 自绘焦点框
修改  src/ui/pages/storyboard/ChainView.cpp 等 3 处手搓空态 → EmptyState
修改  src/ui/pages/assets/RefLibraryView.cpp   确认框
修改  src/ui/pages/novel/InitChainView.cpp     去掉原生 QMessageBox
修改  src/ui/pages/project/ProjectWizardDialog.cpp  确认框
修改  src/ui/verify/gallery/WidgetGalleryView.cpp   补动效对照页
```

## 验收

```powershell
cmake --build build -j 8 --target ShineTVStudio

# QSS 五态探针（既有用它）
$env:SHINE_QSS_PROBE = "1"; & .\build\ShineTVStudio.exe

# 画廊：五态 + 动效关/开
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"; & .\build\ShineTVStudio.exe --widget-gallery
```

判据：

1. `gallery-motion-reduced.png` 与 `gallery-motion-normal.png` 差异只在动画相位，布局完全一致。
2. 键盘 Tab 走一圈：每个可交互控件都有可见焦点环；表格内方向键移动不跳到页面外。
3. 所有错误路径不再只靠 Toast：错误在页面上驻留，直到用户处理或重新触发成功。
4. 所有确认框外观一致；危险确认的按钮文案写明后果。
5. 卡片 hover 时相邻内容**不发生 1px 位移**（前后截图 diff 验证）。
6. `grep -rn "QMessageBox" src/ui` 在 `pages/` 下零命中。

## 风险

| 风险 | 缓解 |
|---|---|
| 错误态驻留会长期占空间 | 只在"当前视图的阻塞性错误"用 `ErrorState`，行级错误用行内 Tag |
| 全局加 hover 过渡带来重绘开销 | 只加 `border-color`/`background-color`/`color` 过渡，不加 `width`/`padding` 过渡 |
| 自绘焦点框与 QSS 焦点环双份 | 明确分工：QSS 管按钮/输入，delegate 管表格/矩阵 |
