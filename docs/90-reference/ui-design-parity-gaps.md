---
id: reference.ui-design-parity-gaps
kind: reference
status: current
scope: ui
source_of_truth:
  - webui/src/styles/views.css
  - webui/src/styles/ui.css
  - webui/src/styles/shell.css
  - webui/src/styles/tokens.css
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/Theme.h
  - src/ui/kit/controls/WidgetCommon.h
  - src/ui/kit/controls/Controls.h
  - src/ui/pages/assets/AssetDetailView.cpp
  - src/ui/pages/assets/AssetWorkspace.h
  - src/ui/pages/novel/DraftView.cpp
  - src/ui/verify/review/P05Review.cpp
  - src/ui/verify/checks/P04ChapterChecks.cpp
last_verified: 2026-09-29
---

# 设计稿对不齐的地方：UI 缺口清单

`webui/` 是 React 设计稿（未跟踪，仅作参照）。这份文件集中记录**当前无法在 Qt6 Widgets
里对齐**的视觉与交互差异，分三类：渲染器能力边界、Qt 控件行为限制、数据/架构缺口。

每条给出现状、原因、影响和要做什么才能补上。改 UI 前先看这里，避免重复踩同一类坑。

事实分级沿用 `docs/README.md`：**源码事实**指能由上述 `source_of_truth` 直接证明的；
**外部前提**指 Qt 自身的能力边界；**待验证**指尚未截图比对的，不当结论用。

---

## 一、渲染器能力边界（Qt QSS 不支持，无法 1:1 移植）

Qt 样式表是 CSS 的子集，设计稿里这些属性在 QSS 中**根本没有对应声明**，
写了也不生效。不要为它们编造半成品实现。

| 设计稿属性 | webui 出处 | Qt 现状 | 替代做法 |
|---|---|---|---|
| `box-shadow` | `.tl-card:hover`、`shadow-1/2/accent` 等 | **QSS 无此属性，但 Qt 有现成方案**：`QGraphicsDropShadowEffect`，已封装为 `widgets::ApplyShadow(w, ShadowLevel)` | 直接用，见下方「阴影」小节 |
| `backdrop-filter: blur()` | 浮动面板毛玻璃 | QSS 无滤镜系统 | 用 `bg.overlay` 不透明底（见下方说明） |
| `transform: translateY(-2px)` | `.tl-card:hover`、卡片 hover 抬升 | QSS 不能改几何 | 用 `motion::Tween` 主动画（`Card::Lift` 已做） |
| `transition: all var(--dur-N)` | `.chip`、按钮、卡片全局 | QSS 无 transition | 用 `kit::motion::Tween` + `Easing` 主动画（`kDurFastMs=120` / `kDurBaseMs=200` / `kDurSlowMs=320` 三档 token 已存在） |
| `color-mix()` | `.chip.on .cnt`、`accent-dim` | QSS 无 | 用已存在的近似 token（`fill.selected` 替 `accent-dim`、`fill.selected` 替 18% accent 混色） |
| CSS Grid / Flex 的 `minmax()`、`auto-fit` | `.asset-grid`、`.flowwrap` | QSS 不参与布局 | `QGridLayout` + `ResizeEvent` 重算，或用 `QScrollArea` + 固定列宽 |

### 阴影（已实现，不要重复造）

QSS 确实没有 `box-shadow`，但 **Qt 自带 `QGraphicsDropShadowEffect`**，
走 `widget->setGraphicsEffect()` 即可。已封装成 kit 里的一个函数：

```cpp
widgets::ApplyShadow(widget, widgets::ShadowLevel::Sm);   // 卡片 / 悬浮层
widgets::ApplyShadow(widget, widgets::ShadowLevel::Lg);   // 弹窗 / 抽屉 / 浮动面板
widgets::ApplyShadow(widget, widgets::ShadowLevel::Accent); // 主按钮辉光
```

几何参数硬编码在 `WidgetCommon.cpp` 的 `kShadowSm/kShadowLg/kShadowAccent`，
逐条对齐 `webui/src/styles/tokens.css` 的 `--shadow-1/2/accent`
（CSS 一条 box-shadow 可叠多层，effect 只出一层，取扩散占主导的那层）。
色值随主题变化，所以走 `ColorToken` 的 `shadow1`/`shadow2`/`shadowAccent`
（`Theme.h` 的 `Current()`），四套主题 JSON 各自一份。

**已挂阴影的位置**：`Card` hover（进/出事件自动开关）、出图/出片的浮动工具栏与浮动面板、
`CommandPalette`、`Drawer`、`Toast`。

**使用约束（踩过的坑）**：

- 一个控件**只能挂一个** graphics effect。重复 `ApplyShadow` 会先摘旧的再挂新的；
  要叠多层阴影只能自己继承 QWidget 重写 `paintEvent`。
- 阴影在控件四周占 blur 半径那么宽的空间。控件若在**固定尺寸**容器里（或本身是
  无边框 window + `setFixedWidth`，如 Toast），溢出的部分会被裁掉——需要父容器留边距。
- 主题切换会自动重挂（`ApplyShadow` 内部注册到 `QEvent::ThemeChange`），
  但前提是走 `ApplyShadow` 这个入口，自己直接 `new QGraphicsDropShadowEffect`
  挂上去的控件**不会**跟随主题。

**源码事实**：`theme::bgOverlay` 现在是**不透明**浮层底（弹层/抽屉/提示/下拉），
遮罩走 `theme::shadowScrim`（带 alpha，QSS `*[shineKind="scrim"]`）。
这是为绕开「QSS 无法做半透明+模糊」而定的契约，不要改回半透明。

`theme::shadow::kSm/kMd/kLg`（`Token.h`）是早期定义的固定几何常量，
与后来按主题拆分的 `shadow1/2/Accent` token **并存但互不引用**——实际生效的是后者。
看到 `shadow::` 命名空间时别以为那就是当前阴影实现。

---

## 二、Qt 控件行为限制

### 1. `QPushButton` 挂子布局后自绘文字会被裁

**症状**：按钮自身 `text()` 与挂上去的子控件抢同一块矩形，文字被布局裁掉、子控件压在文字上。

**本轮已解**（`113dc75`）：`widgets::Chip` 改「按钮文字清空 + 内部 `QHBoxLayout` 承载
两个 `QLabel`」，并**覆写 `sizeHint()` / `minimumSizeHint()` 走布局尺寸**——因为
`QPushButton::sizeHint()` 只按「无文字 + focus 框」算，完全不含内部布局，不覆写就会把
chip 压得只剩边框（首次实现时截图里就是「全[8]」标签被裁）。

**通用规则**：以后任何「按钮里放非文字内容」的控件，都必须同时覆写
`sizeHint()`/`minimumSizeHint()`，否则尺寸一定错。QSS 的 `padding` 对子控件无效，
若内边距由布局提供，QSS 里就别再写 `padding` 覆盖（否则边框与内容错位）。

### 2. `QPlainTextEdit` 忽略 `textIndent`（已绕过）

**症状**：中文小说正文应有 2em 首行缩进。`QPlainTextEdit` 的渲染路径**忽略**
`QTextBlockFormat::setTextIndent()`——已验证 `firstBlock().blockFormat().textIndent()`
返回 `28.0`，格式确实写进了文档，但画面上无缩进。`views.css:576`
`.draft p { text-indent: 2em }` 走 `QPlainTextDocumentLayout` 无解。

**已绕过**：`DraftView` 正文控件迁到 `QTextEdit`（富文本路径尊重 `textIndent`），
首行缩进正常渲染。连带改动只有一处——`checks/P04ChapterChecks.cpp` 里
`appendPlainText` 换成 `QTextEdit` 的 `insertPlainText`（**断言本身未削弱**，
哈希变化且 `== Sha1Hex` 的判据照跑）。

**迁移时的坑**：`QTextEdit` 默认 `acceptRichText=true`，粘贴 HTML 会改变
`toPlainText()` 的结果，静默破坏 P04 守的「哈希与落盘一致」不变式。
已显式 `setAcceptRichText(false)` 堵住。**别的页面若要从
`QPlainTextEdit` 迁过来，务必带上这一句。**

### 3. 手动 `move()` 布局的三个必踩坑

用「无布局容器 + `resizeEvent` 里手动 `move()`」模拟 CSS `absolute` 时（资产时间线
`RelTimeline` 就是这么做的），有三个坑会让画面静默出错：

- **必须连高度一起定死**。`QFrame` 只 `setFixedWidth(w)` 而不定高，在无布局的父里
  会撑满剩余高度——竖线会一路画到底、压穿下方的文字。写 `setFixedSize(w, h)`。
- **`move()` 之前必须显式 `show()`**。父控件已经显示过之后 `new` 出来的子控件默认带
  `WA_WState_Hidden`，`grab()` / 截图不会点亮它们——表现为「一部分元素凭空消失」。
- **`QLabel` 要先 `adjustSize()`**。默认宽度 100px，不调的话 `move(x - w/2)`
  按错误宽度居中，文字整体偏移。

### 4. `QTabWidget` 与设计稿的 `Segmented` 不一致

设计稿出图/出片用的是自定义 `Segmented`（胶囊分段控件），当前实现是原生 `QTabWidget`
（下划线页签）。

**为什么不换**：评审探针强耦合——`src/ui/verify/review/P07Review.cpp`、`P08Review.cpp`
用 `findChild<QTabWidget*>()` 反查并直接切换页签。换控件要连带改 P07/P08 的验收代码，
视觉收益不抵连带成本。**这是有意识的取舍，不是遗漏。**

### 5. 浮动面板用布局 overlay，不是真正的浮层

设计稿的浮动工具栏/面板用 CSS `absolute inset`。当前实现（`ImageFlowWorkspace` /
`VideoFlowWorkspace`）让 canvas / toolbar / panel / filmstrip **共享一个 grid cell +
alignment flags**，用对齐标志模拟绝对定位。

**后果**：窄窗口下面板会被压缩而不是盖住画布。这是 Qt 布局模型下的等效做法，
不接受"真正浮层"语义。若将来要求窄窗下面板必须浮在画布之上，需要改写为
child-over-parent 手动 `move()/resize()`，那是另一轮工作量。

---

## 三、数据 / 架构缺口

### 1. 资产详情 `.tl` 关联时间线（已实现，记录实现方式的坑）

设计稿 `webui/src/views/Assets.jsx:85-105` 的事件轴（`.tl` 轴线 + `.ev` 事件点按
`left: pct(ch) + '%'` 定位 + `.pin` 圆点 + `.lb` 标签，下方 `.tl-below` 是「绑定镜头」
chip 行 + 「参考图」缩略图）已在 `AssetDetailView` 的 `RelTimeline` 里实现。
数据全部真实只读：`visual_states`（外观基线事件，最新一条为 `hot` 带「· 当前」→
对应 `Assets.jsx:68`）、`chapters`（轴线刻度）、`shots.character_ids_json`（绑定镜头）、
`generated_images`（参考图缩略 52×36）。

**CSS 用 `absolute` + `%` 定位，Qt 布局表达不了**，所以子控件是在 `resizeEvent` 里
由 `Layout()` 手动 `move()` 的。三个由此踩到的坑：

- **无布局容器里必须连高度一起定死**。`.stem` 竖线最初只 `setFixedWidth(2)`，
  在无布局的父里会撑满剩余高度（92px），从 `kStemTop` 一路画到底，**压穿下方
  「第 N 章」刻度标签**。要写 `setFixedSize(2, h)`，h 取到轴线为止。
- **`move()` 之前必须显式 `show()`**。父控件已经显示过之后 `new` 出来的子控件
  默认带 `WA_WState_Hidden`，`grab()` 不会点亮它们——表现为「轴在、刻度和事件全没了」。
- **`adjustSize()` 不能省**。`QLabel` 默认宽度 100px，不调的话 `move(x - w/2)`
  按错误的宽度居中，文字整体偏移。

**已知偏差**：设计稿 `.stem` 是 `top:30 h14`，会与上方 `.ev` 标签（约 26–39）重叠，
当前实现把 stem 改到 40–44 让它正好搭上轴线、避开标签。其余数值逐条照抄 CSS。

### 2. `.tl-below` 不换行（`flex-wrap` 缺替身）

`views.css:840` 的 `.tl-below` 是 `flex-wrap: wrap`，检查器变窄时 chip 会折到第二行。
Qt Widgets 没有 flow layout。当前把 chip 钉在各自 `sizeHint` 上，窄时**溢出而不裁切**
（安全但不是换行）。真要换行得在 `kit/layout/` 写一个流式布局，属另一轮工作量。

### 3. 评审抓图曾覆盖不到检查器内容（已修）

`.vsec` / `.derive` 位于折叠的检查器内。`P05Review.cpp` 此前只把 workspace 交给宿主，
检查器是个没布局的游离子控件——所有 `sheet-*` 截图实际只拍到了左侧卡片网格。
现在评审宿主会按外壳的方式把 `InspectorBody()` 摆到 workspace 旁边，
并新增 `detail-vsec` / `detail-derive-chain` / `detail-timeline` 三张取证图。

**给后续改动的提醒**：新增页面级控件后，务必确认评审 harness 真的能拍到它，
否则「没报错」会被误当成「验过了」。

### 4. 设计稿 mock 数据与 C++ 侧字段的差异

设计稿这些关联数据来自 `webui/src/data/mock.js`（含硬编码的 `CH=6` 章数、
写死的"外观基线 ② · 当前"文案）。实现一律用**真实库里的数据**：刻度按实际章节数渲染
（3 章的书只显示 3 个刻度，不会假称 6），「当前」取 `visual_states` 里最新一条而非硬编码。
视觉上和 mock 略有出入，但不会对用户说假话。

---

## 四、验收体系自身的已知噪音

不是 UI 问题，但会干扰读自检报告，一并记录：

| 现象 | 位置 | 说明 |
|---|---|---|
| `Could not parse stylesheet of object QLabel` | P10 运行期 | 既有噪音，与本轮改动无关（已用 stash 对照验证过） |
| `QString::arg: 1 argument(s) missing` | P05 fixture 搭建 | 既有噪音 |
| `SHINE_P10_S8` / `SHINE_P10_S10` FAIL | `checks/P10Checks.cpp` | 断言 `dist/ShineTVStudio2/ShineTVStudio.exe` 存在，那是**打包产物**；本工作区无 `dist/`（`build/` 已 gitignore）。与 UI 无关 |
| P04_S1–S4 报告格式不同 | `checks/P04WorldChecks.cpp` 等 | 用 `key=val PASS` 而非 `[PASS]`，按 `[PASS]` 正则统计会误显示为 0 项，**别误判成"没跑"** |

---

## 五、怎么用这份文件

- **改 UI 前**：先查第二节，避免再踩 `QPushButton` 尺寸、QSS padding 这类已修过的坑。
- **被要求"再像一点"**：第一节是硬边界，别在这里浪费时间；能动的在第二、三节。
- **评审截图与设计稿有差异时**：先确认差异是否落在第一节（能力边界，无法修），
  再看是不是第二、三节的未完成项。
- 补完任一条后，**同步更新本文件**并删掉对应条目，别留"已修复"的记录当历史。
