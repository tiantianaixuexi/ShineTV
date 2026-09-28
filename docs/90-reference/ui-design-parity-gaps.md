---
id: reference.ui-design-parity-gaps
kind: reference
status: current
scope: ui
source_of_truth:
  - webui/src/styles/views.css
  - webui/src/styles/ui.css
  - webui/src/styles/shell.css
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/kit/theme/Token.h
  - src/ui/kit/controls/Controls.h
  - src/ui/pages/assets/AssetWorkspace.h
  - src/ui/pages/novel/DraftView.cpp
  - src/ui/verify/review/P05Review.cpp
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
| `box-shadow` | `.tl-card:hover`、`shadow-1/2/3` 等 | QSS 无此属性；全仓也没有 `QGraphicsDropShadowEffect`（已核实零引用） | 用 `border` 变化 + `bg.elevated` 底色差表达层次 |
| `backdrop-filter: blur()` | 浮动面板毛玻璃 | QSS 无滤镜系统 | 用 `bg.overlay` 不透明底（见下方说明） |
| `transform: translateY(-2px)` | `.tl-card:hover`、卡片 hover 抬升 | QSS 不能改几何 | 只做 `:hover` 的描边/文字色变化（已实现） |
| `transition: all var(--dur-N)` | `.chip`、按钮、卡片全局 | QSS 无 transition | 用 `kit::motion::Tween` + `Easing` 主动画（`kDurFastMs=120` / `kDurBaseMs=200` / `kDurSlowMs=320` 三档 token 已存在） |
| `color-mix()` | `.chip.on .cnt`、`accent-dim` | QSS 无 | 用已存在的近似 token（`fill.selected` 替 `accent-dim`、`fill.selected` 替 18% accent 混色） |
| CSS Grid / Flex 的 `minmax()`、`auto-fit` | `.asset-grid`、`.flowwrap` | QSS 不参与布局 | `QGridLayout` + `ResizeEvent` 重算，或用 `QScrollArea` + 固定列宽 |

**源码事实**：`theme::bgOverlay` 现在是**不透明**浮层底（弹层/抽屉/提示/下拉），
遮罩走 `theme::shadowScrim`（带 alpha，QSS `*[shineKind="scrim"]`）。
这是为绕开「QSS 无法做半透明+模糊」而定的契约，不要改回半透明。

阴影 token（`theme::shadow::kSm/kMd/kLg`，`Token.h:107-111`）目前**只定义未被消费**——
QSS 里没有任何规则引用它们。可作为将来的 `QGraphicsDropShadowEffect` 数据源，
但在那之前不要当成"已有阴影"。

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

### 2. `QPlainTextEdit` 忽略 `textIndent`（小说正文首行缩进）

**症状**：中文小说正文应有 2em 首行缩进。`QPlainTextEdit` 的渲染路径**忽略**
`QTextBlockFormat::setTextIndent()`——已验证 `firstBlock().blockFormat().textIndent()`
返回 `28.0`，格式确实写进了文档，但画面上无缩进。记录位置：`pages/novel/DraftView.cpp:340`。

**要做什么**：迁移到 `QTextEdit`（其 HTML-ish 富文本路径尊重 `textIndent`）。
代价：`DraftView::Edit()` 返回类型变化，`src/ui/verify/checks/P04ChapterChecks.cpp` 等
P04 验收代码里对 `Edit()` 的调用要同步改。**未完成**——影响面比看上去大，需单独一轮。

### 3. `QTabWidget` 与设计稿的 `Segmented` 不一致

设计稿出图/出片用的是自定义 `Segmented`（胶囊分段控件），当前实现是原生 `QTabWidget`
（下划线页签）。

**为什么不换**：评审探针强耦合——`src/ui/verify/review/P07Review.cpp`、`P08Review.cpp`
用 `findChild<QTabWidget*>()` 反查并直接切换页签。换控件要连带改 P07/P08 的验收代码，
视觉收益不抵连带成本。**这是有意识的取舍，不是遗漏。**

### 4. 浮动面板用布局 overlay，不是真正的浮层

设计稿的浮动工具栏/面板用 CSS `absolute inset`。当前实现（`ImageFlowWorkspace` /
`VideoFlowWorkspace`）让 canvas / toolbar / panel / filmstrip **共享一个 grid cell +
alignment flags**，用对齐标志模拟绝对定位。

**后果**：窄窗口下面板会被压缩而不是盖住画布。这是 Qt 布局模型下的等效做法，
不接受"真正浮层"语义。若将来要求窄窗下面板必须浮在画布之上，需要改写为
child-over-parent 手动 `move()/resize()`，那是另一轮工作量。

---

## 三、数据 / 架构缺口

### 1. 资产详情 `.tl` 关联时间线（未实现）

设计稿 `webui/src/views/Assets.jsx:85-105` 有完整的事件轴：`.tl` 轴线 + `.ev` 事件点
（`left: pct(ch) + '%'` 定位）+ `.pin` 圆点 + `.lb` 标签，下方 `.tl-below` 是
「绑定镜头」chip 行 + 「参考图」缩略图行。

**QSS 已就位但代码未用**：`QssBuilder.cpp:638-643` 已定义 `tlaxis` / `tltick` /
`tlpin` / `tlcap` 四组规则，全仓无对应控件使用它们。

**卡在哪**：`AssetWorkspace` 当前**不持有章节列表**，拿不到"按章的外观基线/绑定镜头/
参考图"数据。要先定数据来源（候选：`src/novel/NovelStoryboard.h`、
`src/visual/*` 的实体—镜头绑定表），再从 workspace 往 `AssetDetailView` 灌数据。

**要做什么**：先在 `AssetWorkspace` 建立章节列表持有与查询，再在 `AssetDetailView`
按设计稿百分比布局渲染轴线与事件点。**未完成。**

### 2. 资产详情（`.vsec` / `.derive`）没有评审抓图（验收盲区）

`.vsec`（分区）和 `.derive`（派生链，108px 节点 + 22px 连接线）已实现在
`AssetDetailView`，但它位于**折叠的检查器内**，`src/ui/verify/review/P05Review.cpp`
只做了 `DetailProbe` 功能验证，**没有任何 harness 抓它的图**。

**后果**：这部分从未与设计稿做过视觉比对，可能有偏差而无人发现。这是**验证盲区**，
不等于已验证通过。

**要做什么**：在 `P05Review.cpp` 加一条"展开检查器 + 抓 AssetDetailView"的取证路径。

### 3. 设计稿 mock 数据在 C++ 侧不存在

设计稿的 `.tl`、参考图、绑定镜头都来自 `webui/src/data/mock.js`。C++ 侧的
`visual_assets` 表只有资产自身，没有"按章的外观基线图 / 绑定镜头 / 参考图"这些
关联记录。**这是缺口 1 的根因**：不是渲染做不了，是没有数据可渲染。

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
