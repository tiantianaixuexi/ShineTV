---
id: reference.ui-design-parity-gaps
kind: reference
status: current
scope: ui
source_of_truth:
  - webui/src/styles/views.css
  - webui/src/styles/ui.css
  - webui/src/styles/shell.css
  - webui/src/styles/base.css
  - webui/src/styles/tokens.css
  - webui/src/components/UI.jsx
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/Theme.h
  - src/ui/kit/theme/ThemeService.cpp
  - src/ui/kit/controls/WidgetCommon.h
  - src/ui/kit/controls/Controls.h
  - src/ui/kit/controls/Inputs.h
  - src/ui/kit/controls/Surfaces.h
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

## 〇、几何刻度基线（已对齐，规则记在这里）

**源码事实**：圆角、字号两套刻度都已收敛到 `webui/src/styles/tokens.css` 的
`:root` 块，改 QSS 时按这两条规则走，不要另起数值。

### 圆角（6 档，已 1:1）

`--r-xs/sm/md/lg/xl/pill = 4 / 6 / 10 / 14 / 18 / 999`。`Token.h` 的
`radius::kXs…kPill` 与之同值同序；`QssBuilder.cpp` 里出现的每个 `border-radius`
都取自其中一档。

| 刻度 | 值 | 典型控件 |
|---|---|---|
| `--r-xs` | 4 | `.seg > button`、`.check .box`、`.kbd`、缩略图兜边 |
| `--r-sm` | 6 | `.btn` / `.input` / `.select` / `.tag`(非 pill 者) / `.tree .node` / `[data-tip]` / `.icon-btn` / `.spin` 容器 |
| `--r-md` | 10 | `.card` / `.toast` / `.sheet` / `.seg` 容器 / `.menu-pop` |
| `--r-lg` | 14 | `.modal` / `.drawer` 内浮层 / `.empty .glyph` |
| `--r-xl` | 18 | 设计稿预留大容器档，**当前无控件占用**（QSS 里不出现） |
| `--r-pill` | 999 | `.tag` / `.prog` / `.chip` / `.stageflow .snode` / `.search` / 滚动条滑块 / 正圆控件 |

两条约定：

- **正圆写 999，不写半径数值**。单选钮、勾选框、开关轨道、滑块手柄都是「圆」，
  写 7 或 999 在 Qt 里渲染一致（QPainter 会钳到半边长），999 才是设计稿的写法。
- **方头元素写 0**。accent 左条、`.tl` 轴线这类本身没有圆角声明的细条走 `0px`，
  不要为了「在刻度上」硬凑成 4。

### 字号（整数档，设计稿的 x.5 档就近取整）

**外部前提**：Qt QSS 的 `font-size` 最终走 `QFont::setPixelSize(int)`
（`qfont.h:165` 的签名就是 `int`），小数在渲染前被量化到整设备像素。
`13.5px @ 100% 缩放` 实际是 14px——比设计稿**更**偏，不是更近。

**因此**：QSS 一律声明整像素值，设计稿的 x.5 档按就近取整落位：

| 设计稿 | 取值 | 落到的整像素档 |
|---|---|---|
| body 13.5px / `.card-title` 13.5px / `.input`（继承 body） | 13px | 基准字号 `font::kBase` |
| `.small` / `.seg > button` / `.check` / `.toast` 12.5px | 12px | |
| `.tag` / `.table th` / `[data-tip]` 11.5px | 11px | |
| `.kbd` / `.tag.sm` 10.5px | 10px | |

显式声明 13px 的控件（`.btn` / `.tabs` / `.table`）与基准同值，
所以控件观感与设计稿一致；差异只落在「设计稿 13.5px vs Qt 13px」这半像素上。
**待运行验证**：实际截图里 100% 缩放下 Qt 的取整方向（上取/下取）尚未截图确认。

### 主题集（5 套，已齐）

**源码事实**：`tokens.css` 的 5 套 `[data-theme]` 块与 Qt 端一一对应，
深空 / 薄暮 / 纸墨 / **水墨** / 极夜。水墨（宣纸 · 墨色 · 印泥红）是后补的，
`Themes/水墨.json` 的 21 个直出色值逐条抄自该块，4 个 alpha 值按既有约定落位：

| token | CSS 来源 | JSON 写法 |
|---|---|---|
| `shadow.scrim` | `--scrim: rgba(24,21,17,0.5)` | `#18151180`（原样带 alpha） |
| `shadow.accent` | `--shadow-accent` 的 `rgba(47,44,40,0.22)` | `#2F2C2838` |
| `shadow.2` | `--shadow-2` 的 `rgba(50,44,36,0.18)` | `#322C242E` |
| `shadow.1` | `--shadow-1` 的**第二层** `rgba(50,44,36,0.10)` | `#322C241A`（取扩散主导那层） |
| `line.*` / `fill.*` | `rgba()` 叠在底色上 | 预合成到 `bg.surface` 的**不透明**值 |

预合成约定与四套旧主题一致（可反查 `Themes/深空.json` 的 `line.subtle` =
`rgba(234,240,247,.07)` 叠在 `bg.surface` 上），不是新发明。

**加主题时必须同步改三处**（都按下标寻址，漏一处就是越界或加载失败）：
`Token.h` 的 `enum class ThemeId` → `Theme.h` 的 `kAllThemes` → `Theme.cpp` 的
`kThemes` / `g_themes` / `g_loaded`（后两者已改为按 `kAllThemes.size()` 推导）→
`ThemeService.cpp` 的 QSS 缓存数组（同样按 `kAllThemes.size()` 推导）。
`kAllThemes` 的下标就是 `ThemeId` 的枚举值，**顺序不可重排**（已持久化的主题名按名查，
但运行时缓存按下标寻址）。

**每主题字体族（已落地）**：水墨块额外覆盖了 `--font-ui`（衬线族：`Noto Serif SC` /
`SimSun` 等）。实现**没有**给 `ColorToken` 加字段——`ColorToken` 的字段顺序就是
QSS 的 `%N` 占位序、也是 `ColorTokenToJson/FromJson` 的键序（两者必须逐位一致，
插入即整体错位），塞一个非颜色的字符串进去会让「每个 token 都被 QSS 消费」的
自检判据失真。

改为：字体族另立一张按下标寻址的小表（`Theme.cpp` 的 `kFontFamilies`，与 `kAllThemes`
同序），QSS 模板里的基准规则用**非颜色占位符** `$FONTFAMILY$`，由 `QssBuilder::Build`
在 `FillTokens` 之后单独替换。`SelfCheck` 另加一条判据：输出里残留 `$FONTFAMILY$`
即判 FAIL（漏替换会让 Qt 把它当字体名解析）。自定义主题没有字体概念，回落到当前
内置主题的族。序列化契约与 `%N` 顺序因此**零改动**——五套主题 JSON 一个字没改。

> **这条思路已复用到 color-mix**（见第一节）：「需要在 QSS 里用某个 token 派生出新值、
> 又不能进 `%N` 序列」时，一律走「按下标寻址的小表 + `$XXX$` 非颜色占位符 +
> `SelfCheck` 盯残留」三件套。`$TONEBG$n$` / `$TONEEDGE$n$` 就是这么加的，
> 同样零改动五套主题 JSON。

**已截图验证**（`SHINE_P10_REVIEW` 主题矩阵，5 主题 × 流水线/出图/出片 = 15 张）：
水墨的 `pipeline-水墨.png` 与 `深空` 同页对照，标题与正文已是衬线族，浅色底上
**无深色系控件残留**，焦点环与主色可区分。

**仍未验证**：通过「主题菜单 → 切换」的**运行期交互**路径（而非 harness 直接
`ThemeService::Switch`）观感未截；本机无可交互桌面会话（见第五节第 4 条）。

---

## 一、渲染器能力边界

> **判据先说清**：QSS 缺的属性 ≠ Qt 缺的能力。QSS 是 CSS 的子集，但 **Qt Widgets
> 本身**的 API 面宽得多（`QGraphicsDropShadowEffect` / `QTimer`+`paintEvent` 自绘 /
> `QStyledItemDelegate` / `QGraphicsLayout`）。写「Qt 做不到」之前先确认那条能力在
> Qt 侧真的没有 —— 本文件历史上曾把「水墨主题衬线字体」记成「要动 ColorToken 序列化
> 契约，留作独立一轮」，而实际上另立一张按下标寻址的小表就解决了（见第〇节），
> 契约零改动。**下表 10 条里只有 `backdrop-filter` 一条是真边界。**

Qt 样式表是 CSS 的子集，设计稿里这些属性在 **QSS** 中没有对应声明。右列写的是
Qt 侧的实际落地手段。

| 设计稿属性 | webui 出处 | QSS 现状 | Qt 侧实际做法 |
|---|---|---|---|
| `box-shadow` | `.tl-card:hover`、`shadow-1/2/accent` | 无此属性 | **已做**：`widgets::ApplyShadow(w, ShadowLevel)` 包 `QGraphicsDropShadowEffect`，见下方「阴影」小节 |
| `backdrop-filter: blur()` | 浮动面板毛玻璃 | 无滤镜系统 | **真边界**。Qt 没有「采样背后窗口」的 API（`QGraphicsBlurEffect` 只能糊控件自己的内容，不是背景）。已定契约：`bg.overlay` 走**不透明**底 + `shadow.scrim` 遮罩 |
| `transform: translateY(-2px)` / `scale(0.97)` | `.card.hoverable:hover`、`.btn:active` | QSS 不能改几何 | **均已做**：`Card::Lift` 用 `motion::Tween` 移 2px；`Button::mousePressEvent` 走 `CaptureBackdrop` + 中心缩放自绘，按下 0.97、松开回 1.0 |
| `transition: all var(--dur-N)` | `.chip`、按钮、卡片全局 | 无 transition | **已做**：`kit::motion::Tween` + `Easing` 主动画（`kDurFastMs=120` / `kDurBaseMs=200` / `kDurSlowMs=320` 三档 token 已存在） |
| `color-mix()` | `.tag.ok` 等 tone 底、`.gantt .gcell`、`gates` | 无 | **已做**：在 `QssBuilder::Build` 阶段按主题把 tone 色按 12%（底）/ 35%（边）预合成到 `bg.surface`，经 `$TONEBG$n$` / `$TONEEDGE$n$` 派生态占位符注入。`ColorToken` 字段序**零改动** |
| `opacity: 0.45` | `.btn:disabled`、`.stageflow .snode.skip` | 无 opacity 属性 | **未做**。注意**不能**用 `QGraphicsOpacityEffect` 补：一个控件只允许挂一个 graphics effect，会把 hover/选中的阴影顶掉（`pages/storyboard/StoryboardTimeline.cpp:31` 记了这个坑，现改用直接给像素图乘 alpha）。要补得走自绘路线 |
| `@keyframes` 循环动画 | `.prog.run` 微光、`.dot.run` 脉冲、`.empty .glyph` 浮动 | 无 keyframes | **均已做**：`Spinner` / `StatusDot` / `ProgressBar` 微光 / `EmptyState` 的 `FloatGlyph`（4s ±6px）全部 QTimer 推进相位 + `paintEvent` 自绘；「减少动效」下退化为静态 |
| `:focus-within` | `.search:focus-within` | 只认控件自身焦点 | **已做**：事件过滤把焦点转发到父框的 `focused` 属性（`SearchBox`） |
| 每主题改字体族 | `[data-theme="inkwash"] --font-ui`（衬线） | `QssBuilder` 只按 `%N` 替换颜色 | **已做**：不进 `ColorToken`，另立 `kFontFamilies` 表 + 非颜色占位符 `$FONTFAMILY$`，见第〇节 |
| CSS Grid 的 `minmax()` / `auto-fit` | `.asset-grid`、`.gal-grid` | QSS 不参与布局 | **已做**：`QGridLayout` 本身没有 auto-fit，改由宿主在宽度变化时回调重算列数 + `setColumnStretch` 复刻 `1fr`（`AssetWorkspace::ReflowAssets` / `WidgetGalleryView.cpp` 的 `GalGrid::ColumnsFor`）；`flex-wrap` 由 `layout/FlowLayout.h` 覆盖 |


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
（CSS 一条 box-shadow 可叠多层，effect 只出一层，取扩散占主导的那层：
`--shadow-1` 取第二层的 blur/alpha）。
色值随主题变化，所以走 `ColorToken` 的 `shadow1`/`shadow2`/`shadowAccent`
（`Theme.h` 的 `Current()`），五套主题 JSON 各自一份。

**源码事实**：这三个 shadow token **故意不出现在 QSS 里**——QSS 没有 `box-shadow`
这属性，写 `%29/%30/%31` 是无意义的空占。因此 `QssBuilder::SelfCheck` 的
「每个 token 都必须被模板消费」判据对末尾三项做了显式豁免（见该函数的 `is_shadow_token`）。
它们真正的消费方是 `widgets::ApplyShadow`，改阴影档位别去 QSS 里找。

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

### 2. `flex-wrap` 的替身（已补：`layout/FlowLayout.h`）

`views.css:840` 的 `.tl-below` 是 `flex-wrap: wrap`，检查器变窄时 chip 会折到第二行。
此前 Qt Widgets 没有 flow layout，只能把 chip 钉在各自 `sizeHint` 上，窄时**溢出而不裁切**
（安全但不是换行）。

**已实现**：`src/ui/layout/FlowLayout.h` 提供 `FlowLayout`（布局本体）与
`FlowContainer`（把高度对齐到 `heightForWidth()` 的薄容器）。`.tl-below` 已改用它。

用这个类要注意两件事，都是 Qt 的硬限制而不是实现取巧：

- **高度要显式给**。`QLayout` 不能反向通知父控件要多高，而 QWidget 布局系统也不会
  主动调 `QLayout::heightForWidth()`。直接 `new FlowLayout(parent)` 塞进 `QVBoxLayout`
  会导致换行后底部被裁 —— 所以配了 `FlowContainer`（自身高度 = 当前宽度下的
  `heightForWidth`）。要自己写容器的话，必须在 `resizeEvent` 里同步高度。
- **换行按 `sizeHint` 判定**，所以子控件别为了塞进一行而调小 `minimumSizeHint`，
  那会让断行位置算错。

`AddStretch()` 对应 CSS 的 `flex:1` 留白（弹性空隙吃掉本行剩余宽度，不触发换行）。

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

## 四、控件级差异清单（本轮逐控件核对 `ui.css` ↔ `controls/*.cpp` + QSS）

**源码事实**：左列是设计稿规则，右列是 2026-09-29 这一轮核对后的实际状态。
「已对齐」= 数值逐条相同或已按第〇节的刻度规则落位。

| 控件 | 设计稿要点 | 本轮状态 |
|---|---|---|
| Button | secondary = `bg-elevated` 底；ghost = `text-secondary` 字；danger = 透明底 + `line-normal` 边 | **已对齐**（此前 secondary 用 `bg.panel`、ghost 用 accent 字、danger 是实心 danger 底） |
| Button | `:active` `scale(0.97)`、`:disabled` `opacity .45`、primary 的 inset 高光 | 按压缩放**已做**（`Button::CaptureBackdrop` + 中心缩放自绘，`motion.dur.fast`）；`:disabled` 的 opacity 与 primary inset 高光**未做**（QSS 无 opacity / inset，见第一节） |
| IconButton | 28×28（sm 22×22）、r-sm、`text-secondary` 字、active = `fill.selected` + accent | **已对齐**（尺寸此前是 32/24，active 态多画了一圈边） |
| Tag | h20 p0 8 r-pill f11.5 w600；tone 底 = 12% 混色 | 几何已对齐；tone 底仍是实心 `status.*`（`color-mix` 缺失） |
| Card | r10 + `line-subtle`；hover = `line-strong` + shadow-1 + `translateY(-2px)` / dur-2 | **已对齐**（抬升此前是 1px + motion.fast） |
| Segmented | 选中态 `bg-elevated` + shadow-1 | 底色已对齐；shadow-1 用 1px 描边代替（QSS 无 `box-shadow`） |
| Tabs | p8 12 / f13 w600 / muted；选中 accent + 2px 下划线 | **已对齐**（原生 `QTabWidget` 的取舍见第二节第 4 条） |
| Field / Input / TextArea | label f12 w600；input h30 p0 10 r6；textarea p8 10 + line-height 1.6 | **已对齐**（`.field` 的 6px gap 走布局间距；input 不再写死字号，跟随基准） |
| Select | r6 h30；弹层 r10 + `bg-overlay`；行 r6 p7 10 | **已对齐**（下拉箭头 Qt 是字符 `▾`，CSS 是 10×6 SVG） |
| SearchBox | **独立**控件：r-pill + `line-subtle` + h30 + `:focus-within` | **已对齐**（此前复用 `.input` 的 r-sm；焦点转发到 `focused` 属性） |
| Switch | 34×19 pill；旋钮 13px、起点 2、行程 15；关 = `bg-elevated` 底 + `text-secondary` 旋钮 | **已对齐**（此前 36×20，旋钮恒为 `bg-surface` 且行程按宽度算） |
| Checkbox / Radio | 15×15、r-xs、1.5px `line-strong` 边、文字 gap 8；常态 `text-secondary`，选中 `text-primary` | **已对齐**（此前 16×16 + 1px + 常态就是 `text-primary`） |
| Progress | h6 r-pill + `fill-muted`；`thin` h4；`run` 微光 | **已对齐**：`SetThin(true)` 走 QSS 的 `shineSize="thin"`（4px，min/max 双向钉死，只写 min 会被 sizeHint 抬高）；`SetShimmer(true)` 是 QTimer 推进相位 + `paintEvent` 在 chunk 上叠一条平移的渐变高光，并按胶囊做 `QPainterPath` 裁剪 |
| Spinner | 14px / sm 11px，2px 边、顶边 accent，0.7s 线性 | Qt 为 14/20/32 三档、周期约 0.53s（按钮 loading 用的是 14 档，与 CSS 一致） |
| Tooltip | `bg-overlay` + `line-normal` + r6 + p4 9 + f11.5 | **已对齐**；`scale(.94→1)` 与 shadow-1 未做（无边框 window 挂阴影会被窗口边界裁掉） |
| Empty | p40 20 + gap 10；`.glyph` 52×52 r-lg 虚线框 + 24px 图标；标题 f13 w600 `text-secondary` | **已对齐**（此前没有 52×52 字形框，标题走 `statetitle` 的 `text-primary`）；`float-y` 浮动**已做**（`Feedback.cpp` 的 `FloatGlyph`：整框 + 图标一体自绘，4s 周期 ±6px 余弦相位，「减少动效」下退化为静态） |
| Table | th f11.5 w600 p8 12 + 仅下边线；td p9 12 + `line-subtle` | **已对齐**（此前 th 多一条竖分隔线、p6 12；td 是 p5 12） |
| Table | 行 `:hover` = `fill-hover`；`.sel` = `fill.selected` + inset 2px accent 左条 | **已对齐**：QSS 既没有 `::row:hover` 也没有 `inset` 阴影，改由 `data/Table.h` 的 `RowChrome` 代理自绘（hover 底铺 `fill.hover`；选中左条只在最左可见列画，避免逐 cell 变 N 根竖条）。选中行的底色与文字色仍由全局 QSS 的 `item:selected` 兑现 |
| Tree | `.node` h28 p0 8 r6；`.kids` 左侧竖线 + 缩进 | kit 只给 `QTreeView` 基类（f12 + item 圆角）；行高与缩进在页面布局里 |
| Kbd | min-w18 h18 p0 5 r-xs 底边 2px f10.5 w600 | **已对齐**（此前 p1 6 且无字号/字重） |
| StatusDot | `.dot` 7px 正圆、`.dot.run` 脉冲 | **已对齐**：新增独立控件 `widgets::StatusDot`（7px 正圆，tone 走 `status.*` token）。`.dot.run` 的脉冲用 QTimer 推进相位 + `paintEvent` 自绘光晕环与本体呼吸；`SetPulse` 在「减少动效」下退化为静态实心点（语义仍是「运行中」） |
| KeyValue / Steps | `.kv` f12.5 + gap 6 14；`.steps` 20px 圆圈序号 | 控件在 `src/ui/kit/data/Panels.*`，**不在本轮范围**，未核对 |

---

## 五、页面级复刻：本轮形成的约定与仍在的取舍

### 1. 页面专属 QSS 落在哪（2026-09-29 全页面复刻定下的做法）

`QssBuilder.cpp` 承载**共享控件层**；页面私有样式**一律不进它**，改用：

```cpp
// 页面 .cpp 内定义局部 QSS 函数，用只作用于本页根控件的 objectName 收敛作用域
static QString StoryboardQss() { return QStringLiteral(R"( #shotWs QFrame { … } )"); }
// 构造末尾：
root->setObjectName("shotWs");
root->setStyleSheet(StoryboardQss());
```

换肤重挂沿用 `WidgetCommon.cpp` 的既有做法：监听 `QEvent::ThemeChange` 后重新 `setStyleSheet`。
**注意 ThemeChange 不是唯一入口**——自定义主题经 Palette/Style 通道进来时同样要重刷。

### 2. 仍存在的有意取舍（不是遗漏，改前先读）

| 位置 | 设计稿 | 现状 | 理由 |
|---|---|---|---|
| `ReviewView` K01–K29 | 3 元素 gate 行 | 保留 5 列 `DataTable`（多 `detail`/`severity`） | `06` §2.3 验收判据要这两列；改列会动断言。**要不要为 1:1 牺牲这两列，待产品决定** |
| 出图/出片分栏 | `Segmented` 胶囊 | `QTabWidget` 下划线页签 | `P07Review.cpp` / `P08Review.cpp` 用 `findChild<QTabWidget*>()` 反查并切页 |
| 浮动面板 | `absolute inset` | 共享 grid cell + alignment | 窄窗下面板被压缩而非盖住画布；改 child-over-parent 手动 `move()` 是另一轮工作量 |
| 顶栏「运行/停止」 | 单按钮两态 | 两个按钮 | P09 流水线状态未接，做成切换就是假状态 |
| `.stem` 时间轴竖线 | `top:30 h14` | 改到 40–44 | CSS 原值会与上方 `.ev` 文字行（26–39）重叠 |

### 3. 本轮真实踩到的坑（3 个：QSS 注释色值 / Tween 累积 / StyleChange 自递归）

**QSS 注释里写色值字面量会让主题自检 FAIL。**
`kKitTemplate` 是 `R"QSS(… )QSS"` 原始字符串，自检的 `kColorRe` 扫的是**整段输出含注释**。
在注释里写「本规则对应 accent 辉光」并顺手附上 rgba 字面量，会被判「不可回溯的颜色字面」，
5 套主题一起 `qss=FAIL`。**加注释时不要写十六进制色值，也不要写 rgb/rgba 函数调用式的字面量。**

**`motion::Tween` 不自删，反复 new 会累积。**
`Tween.h` 明写「不自删 —— 谁创建谁回收」。挂到卡片这类长生命周期父对象上时，
每次交互 `new` 一个 Tween 会随交互次数单调累积。正确做法是**成员复用一个实例**，
每次只 `stop()` + 重跑。

**在 `changeEvent` 里响应 `StyleChange` 又调 `setStyleSheet` = 无限递归。**
`setStyleSheet()` 自身会派发 `QEvent::StyleChange`。若 `changeEvent` 把 `StyleChange`
也当成「需要换肤重刷」的信号去调那个 `setStyleSheet` 的函数，就是
setStyleSheet → StyleChange → 重刷 → setStyleSheet 的死循环，
进程以 `0xC00000FD`（栈溢出）崩掉，而且**崩在控件构造期**，表现得像"程序莫名其妙起不来"，
很容易误判成环境问题或既有缺陷。
本轮 `GanttView::ApplyQss()` 就中了这招，连带把 `SHINE_P04_REVIEW` 和 `--widget-gallery`
全部打挂。凡是「事件里重新 setStyleSheet」的写法，**必须加一个重入标志位**。
自查口诀：`grep -n "changeEvent" -A 10` 看到 `setStyleSheet`，先问一句会不会自己触发自己。

### 4. 视觉验收的环境限制（2026-09-29 实测）

本机**没有可交互桌面会话**：Qt 窗口能被创建（尺寸正确）但 `IsWindowVisible=false`，
`scripts/capture_window.ps1` 按标题找不到窗、回退抓屏只会拍到锁屏，
`PrintWindow` 返回 `true` 但输出**整张纯黑**。所以「实机启动截图」这一环在本机不可用。

**可用的替代路径是评审 harness**：它走 `QWidget::grab()` 离屏渲染，不依赖桌面。
用 `SHINE_P03_REVIEW` / `P04` / … / `P10` 各跑一遍即可拿到各页真实渲染图
（本轮 P03–P10 全跑通，共 53 张，覆盖项目 Hub / 小说 / 资产 / 分镜 / 出图 / 出片 / 画廊）。

**离屏渲染有两个只有它才会暴露的坑**（实机运行不一定看得出来）：

- 离屏渲染**不等事件循环**。凡是靠 `deleteLater()` 清理的旧控件，在 `grab()` 时可能
  **还没被回收、仍绘制在原处**——表现为新旧文字叠在一起。本轮 `Breadcrumb::Rebuild()`
  就这样让面包屑出现重叠字与成对分隔符。修法是 `hide()` 立刻停止绘制 + 保留
  `deleteLater()` 负责安全回收（不能就地 `delete`：重建常常由被删按钮自己的信号触发）。
- 离屏渲染**不经过桌面合成**，靠屏幕坐标抓图的方案在这里一律失效。

读结论时还要注意：harness 抓的是**离屏控件树**，覆盖不到的东西要另行确认
（第二节第 5 条与第三节第 3 条讲的就是这类盲区）。

---

## 六、验收体系自身的已知噪音

不是 UI 问题，但会干扰读自检报告，一并记录：

| 现象 | 位置 | 说明 |
|---|---|---|
| `Could not parse stylesheet of object QLabel` | P10 运行期 | 既有噪音，与本轮改动无关（已用 stash 对照验证过）。2026-09-29 复核 P10 主题矩阵（15 张）时 stderr 未再出现，来源仍未定位——**别当成已修复**，可能是主题/时序相关 |
| `QString::arg: 1 argument(s) missing` | P05 fixture 搭建 | 既有噪音 |
| `SHINE_P10_S8` / `SHINE_P10_S10` FAIL | `checks/P10Checks.cpp` | 断言 `dist/ShineTVStudio2/ShineTVStudio.exe` 存在，那是**打包产物**；本工作区无 `dist/`（`build/` 已 gitignore）。与 UI 无关 |
| P04_S1–S4 报告格式不同 | `checks/P04WorldChecks.cpp` 等 | 用 `key=val PASS` 而非 `[PASS]`，按 `[PASS]` 正则统计会误显示为 0 项，**别误判成"没跑"** |
| `check-theme: PASS (four themes parse)` | `tools/check-theme.ps1` | 提示串里的「four」是旧文案；判据是 `Count -lt 4`，现在目录里有 5 个 JSON，脚本仍然通过。**别照着提示串以为只有 4 套主题** |
| `font-size: 13.5px` 会渲染成 14px | QSS 全局 | 第〇节的取整规则：QSS 里只写整像素 |
| `QLayout: Attempting to add QLayout "" to QWidget ""` ×2 | `SHINE_P04_REVIEW` 运行期 | 本轮观察到的 Qt 告警，**来源尚未定位**（只记录现象，不下结论）。重建式页面里对同一成员控件二次 `new QVBoxLayout(w)`（w 已有布局）会触发；`pages/novel/WorldBoardS3.cpp` 是本仓最密集的重布局点，且该文件本轮**未改动**。各处渲染图未见异常 |

---

## 七、怎么用这份文件

- **加控件 / 改 QSS 前**：先看第〇节的圆角与字号刻度规则，别再引入 2/3/5/7 这种设计稿没有的半径；
  也别在 `QssBuilder` 注释里写色值字面量（第五节第 3 条）。
- **改 UI 前**：先查第二节，避免再踩 `QPushButton` 尺寸、QSS padding 这类已修过的坑。
- **给页面加私有样式**：按第五节第 1 条走局部 `setStyleSheet`，不要往 `QssBuilder` 里塞页面规则。
- **想「做得更像设计稿」时**：**别把第一节当放弃的理由**。第一节现在只有 `backdrop-filter`
  一条是真边界（Qt 没有采样背后窗口的 API），其余 9 条都已用 Qt API 补齐。写「Qt 做不到」
  之前先问一句：**这是 QSS 的限制还是 Qt 的限制？**两者结论完全不同 ——
  `box-shadow` → `QGraphicsDropShadowEffect`、`transform`/`@keyframes` → `QTimer`+`paintEvent`
  自绘、`::row:hover` → `QStyledItemDelegate`、`flex-wrap` → 自写 `QLayout`、
  `color-mix` → Build 阶段预合成。历史上「水墨主题衬线字体」被误记成「要动 ColorToken
  序列化契约、留作独立一轮」，实际另立一张按下标寻址的小表就解决了，契约零改动。
- **评审截图与设计稿有差异时**：先确认差异是否落在第一节（真能力边界，当前仅毛玻璃），
  再看是不是第二、三、四节的未完成项；第五节第 2 条列的是**有意识的取舍**，别当 bug 顺手"修掉"。
- **补完任一条后，**同步更新本文件**并删掉对应条目，别留"已修复"的记录当历史。
