---
id: modules.qml-kit
kind: contract
status: current
scope: ui
source_of_truth:
  - src/ui/qml/Ctl.qml
  - src/ui/qml/Spinner.qml
  - src/ui/qml/AutoGrid.qml
  - src/ui/qml/Flex.qml
  - src/ui/kit/qml/ThemeBridge.h
  - src/ui/kit/qml/QuickHost.cpp
  - src/ui/qml/CMakeLists.txt
last_verified: 2026-09-29
---

# QML 共享套件契约

`src/ui/qml/` 下的 QML 组件分两类，**边界是硬性的**：

| 类别 | 命名 | 依赖 |
|---|---|---|
| **共享件**（本文件管辖） | 无页面前缀：`Ctl` / `Card` / `Button` / `Tag` / `Seg` / `Dot` / `Chip` / `IconBtn` / `Art` / `Kv` / `Progress` / `Spinner` / `StageFlow` / `AutoGrid` / `Flex` | 只能依赖 `Ctl` 与 `ThemeBridge` |
| **页私有件** | 页面前缀：`Gallery*` / `Assets*` / `Storyboard*` / `ImageFlow*` | 可以依赖共享件 |

`Card.bodyPad` 默认 16（`.card-b` 的 padding）。**`.card-h` 的 12 16 与 `.card-b` 的 16 不一样**，调用方要自己搭「标题栏 + 发丝线 + 内容区」这种结构时传 `bodyPad: 0` 自己排，否则会在 Card 的 16 之上再叠一层 16，只能靠负 `y` 抵消。

**判定标准只有一个：这份组件的第二个页面也用得上吗？** 用得上就是共享件，归 `src/ui/qml/<无前缀名>.qml`；用不上就是页私有件，带页面前缀。不要因为「现在只有一个页面在用」就把它塞进页私有件——那正是本文件要根除的重复来源。

## 为什么有这份契约

2026-09-29 之前的教训：共享层只冻结了 4 个组件且变体不够用，4 个页面迁移时各自手搓，产出 **11 组共 29 个重复文件**（Spinner×3、Button×4、Tag×3、Seg×3、Art×3、Dot×2、IconBtn×2、Kv×2、Progress×2、StageFlow×2、Chip×2）。三份 Spinner 各写各的，其中两份把 CSS 的 `border-top-color` 画成了**贴在顶部的横条**——用户一眼看出「转圈是直线的」。共享层设计的失败比页面实现的失败更贵：**一份错实现会被复制 N 份**。

## 纪律（写任何共享件之前逐条读）

### 1. 继承 `Ctl`

`Ctl.qml` 是 `Rectangle` 基座，已经把两批 token 接好了：`rSm` / `rMd` / `rLg` / `rPill`、`durFast` / `durBase` / `durSlow`、`reduce`（减少动效）、`shadows`（场景图是否支持 `layer.effect`）、`antialiasing: true`。**不要在子类里重新 `import` 一次 token，也不要自己写 `ThemeBridge.durations.fast` 这类表达式**——统一走 `root.durFast` / `root.reduce` / `root.shadows`。

### 2. 颜色只有两条合法路径

```qml
// A. 调色板语义 —— 传 tone 名（"ok" / "warn" / "busy" …），组件内部走 toneBg / toneEdge
property string tone: ""

// B. token 名 —— 传点分 token 名，组件内部 ThemeBridge.colors[名]
property string bgToken: "bg.elevated"

// C. 已经是颜色的场合（点、进度填充）—— 默认绑 ThemeBridge，调用方可覆盖
property color dotColor: ThemeBridge.colors["status.idle"]
```

**禁止**在 `.qml` 里出现 `#RRGGBB` / `rgb()` / `rgba()` / `Qt.rgba(0.2, 0.3, …)` 这类颜色字面量。透明、固定 alpha 的叠加用 `Qt.alpha(ThemeBridge.colors[…], 0.3)` —— 基准色仍来自 token。`tools/check-colors.ps1` 是门禁。

调用方要改外观，**改属性，不改实现**。这就是「暴露配色绑定」的确切含义：暴露的是*语义*，不是*第二套真值*。

### 3. 事件用信号，不用 `onClicked` 覆盖

每个可交互组件都要 `signal clicked()`（或语义化的 `signal picked(...)`）。调用方既能写 `onClicked: …` 处理器，也能 `Connections` / 额外挂 handler。**不要**只在内部 `MouseArea` 上写 `onClicked: …` 就完事——那样调用方只能靠叠一层透明 `MouseArea` 抢点击（`Gallery.qml` 的进度步进钮就是这么打补丁的，属于要消灭的写法）。

`enabled` / `visible` 用 `Item` 内建的，**不要重复声明**。重复声明会触发 `Member enabled of the object X overrides a member of the base object`，QML 静默吞掉赋值。

### 4. 属性名避开 Qt 基类成员

`state` `states` `transitions` `font` `layer` `transform` `palette` `children` `type` `objectName` `opacity` `rotation` `scale` `visible` `enabled` `focus` `anchors` `tag` `data` `component` —— 撞上任何一个，QML **静默**丢弃调用方的赋值，不报错。

另外：**属性名一律小写开头**（历史上出现过 `Small` / `Text` 这种大写开头，`Small` 会被 QML 当成类型名解析）。

### 5. 尺寸从内容推导

用 `implicitWidth` / `implicitHeight`，让调用方拿 `anchors` 摆放。尺寸常量只能来自设计稿（`webui/src/styles/*.css` + `webui/src/views/*.jsx`），**不许凭印象填**。半像素按仓库既有规则就近取整（`11.5→11`、`12.5→12`、`10.5→10`），并在注释里写明原值与档位。

**页面层不写任何高度常数。** 卡片 / 列表项 / 表格行的高度必须由正文内容的 `implicitHeight` 推导，不许在页面里维护一张「每张卡多高」的表。Gallery 页曾经给 9 张卡各写死一个 `ch`（112/76/147/234/206/162/154/174/244），内容一改就跟对不上——实测溢出压住下一行标题，中间一道黑带，比写少了留一块死白。**猜出来的高度常数是这类 bug 的唯一来源**，共享组件换版后只会更多。

### 6. 动效全部读 `reduce`

```qml
duration: root.reduce ? 0 : root.durFast
running: <条件> && !root.reduce
```

`layer.effect` 必须写成 `layer.enabled: root.shadows && <条件>` —— `ThemeBridge.layerEffectsAvailable` 探测不到时（software 场景图后端）挂 layer 的 item 会被**整个吞掉**，不是「阴影没画出来」而是控件消失。

### 7. 引用实现的注释密度

`Spinner.qml` 是参考实现：文件头写清设计稿的 CSS 原文与行号、几何怎么从 CSS 盒模型推出来的、踩过什么坑。**共享件的头部注释要能让另一个人不打开设计稿就复核**。页面私有件可以简写，但涉及色值/几何的偏离必须写明「设计稿是 X，这里是 Y，因为 Z」。

### 8. 布局一律用 `AutoGrid` / `Flex`，不手搓

| 设计稿 | 用哪个 | 关键属性 |
|---|---|---|
| `grid-template-columns: repeat(auto-fit, minmax(N,1fr))` | `AutoGrid` | `minCell` / `maxColumns` / `gap`；子项宽度读 `cellW`，**不要自己算** |
| `display: flex; flex-wrap: wrap` | `Flex` | `gap` / `align`(start\|center\|stretch) / `justify`(start\|center\|end) |

行高一律自适应：栅格行高由 Qt `Grid` 原生取该行最高者（= CSS `align-items: start`），横排高度由 `Flex` 排版结果决定。**两者都不需要调用方声明高度。**

共同约束（`AutoGrid.qml` / `Flex.qml` 都只接管子项的**位置**）：

- 子项里**不要**写 `y:`，也**不要**再挂 `anchors.verticalCenter` —— 会打出 binding loop。
- 有 hover 位移（`translateY`）的 item **不要直接当子项**，套一层不绑 `y` 的 `Item`。
- `Column` 会接管直接子项的 `y`，子项里同样不要再写 `y:`。
- `Flex` 刻意不继承 Qt 的 `Flow`：Qt `Flow` 只有 `spacing`，没有 justify / align，换行时行内子项不居中。

⚠️ **共享件名不得撞 Qt 内建类型。** `Grid.qml` 会被 `import QtQuick` 的内建 `Grid` 盖住，症状是 `Could not find property "minCell"`，而 qmllint **不提示「你写的类型不是你想的那个」**。所以叫 `AutoGrid.qml`。`Button.qml` 撞 `QtQuick.Controls.Button` 是同一类。`Flex` 安全：QtQuick 没有这个类型。

### 9. 图标走槽位，不在页面里手摆

设计稿的图标是内联 SVG（本仓是 `Icon` / `IconBtn` 那套 `PathSvg` 描边），`Button` / `IconBtn` 的字符位 `glyph` 只能画字符。所以共享件提供**图标槽**（`Button.iconSlot`，一个 `Component`），由共享件把它摆进 `.btn .icon` 定死的 15×15（sm 13×13）方盒里，跟着 `padding` 和 `gap` 一起算进 `implicitWidth`：

```qml
Button {
    id: b
    text: "主要"
    variant: "primary"
    iconSlot: Component {
        Icon { anchors.fill: parent; name: "play"; glyphColor: b.fgColor }
    }
}
```

- 组件根**要显式 `anchors.fill: parent`**：`Loader.item` 没有 implicit 尺寸可依赖。
- 页面需要 `pragma ComponentBehavior: Bound` 才能在外层 `Component` 里引用按钮的 id。
- **不要**再在页面里用「Item 垫在按钮外面 + 手写 x」那套绕法：那样图标掉到按钮的背景和圆角之外，是设计稿里根本不存在的形态（`Gallery.qml` 2026-09-29 之前就是这么画的）。

## QML 层的已知接缝坑

以下每条都是本仓实测出来的，**动手前先扫一眼**（更详细的版本在 `docs/90-reference/ui-design-parity-gaps.md`）：

- 页面根对象必须是 `Item`，不能是 `Window`。
- **anchors 与显式 `x` / `y` 互斥，谁写谁生效是静默的。** 实测踩过两回：
  `.card-h` 的发丝线同时写了 `anchors.top` 和 `y: headH-1`，anchors 赢，线被画到卡片**最上沿**与边框重叠，出图里「标题下面那条水平框」直接消失；`anchors.fill` 与显式 `y` 同理会让布局错乱。要定位就用 `anchors.topMargin`，**不要**再叠一个 `y`。
- **`transformOrigin: Item.Center` + `scale` 会把缩放支点放在项自己的中心**，渲染矩形整体向左上平移 `w/2×(k-1)` / `h/2×(k-1)`。凡是用「按左上角推的缩放 + 居中偏移」算 cover 缩放（`k = max(w/vw, h/vh)`、`offX = (w-vw·k)/2`）的组件，承载内容的内层 Item **必须取 `Item.TopLeft`**。2026-09-29 `Art.qml` 取了 `Center`，40×25 的项 scale 4.5 渲染在 `(-70,-43.75)-(110,68.75)` 而盒子在 `(0,0)-(180,112.5)`；640×400 的对比台因此只画出左上 400×250，右侧下侧整片露底色。
  **注意别搞反**：hover 缩放那层要的是 `Center`（CSS `transform-origin: 50% 50%`），两层各管一件事，见 `Art.qml` 的注释。
- 摆子项用 `AutoGrid` / `Flex`（见纪律第 8 条），不要手搓槽位计算，也不要拿 Qt 的 `Flow` / `Row` / `Grid` 去摆带 hover 位移的子项——会和子项自己写的 `y` 抢。
- 共享件文件名撞 Qt 内建类型时，qmllint **不会**报「你用的不是我以为的那个」：`Grid.qml` 会被 `import QtQuick` 的内建 `Grid` 静默盖掉，症状只剩 `Could not find property "minCell"`。所以叫 `AutoGrid.qml`。
- `anchors.fill` 与显式 `y` 互斥。
- `ShapePath` 的默认描边是「不透明白色、宽 1」：纯填充要显式 `strokeColor: "transparent"` 或 `strokeWidth: 0`；开放路径（`PathAngleArc`）必须显式 `fillColor: "transparent"`，否则会在圆心糊出色块。
- `ShapePath` 不能做 `Repeater` 的 delegate（`ShapePath` 派生自 `QObject` 不是 `QQuickItem`），N 段要显式展开。
- 路径起点要设在 `ShapePath.startX/startY` 上，只设 `PathCubic.x/y` 会被旁路。
- 绑定求值期间不能写自己依赖的属性。
- `onWidthChanged` / `onHeightChanged` 的**单表达式体**会被 QQml 当成属性赋值；用块体 `onPaintKeyChanged: { requestPaint() }` 或合并成一个 `paintKey` 字符串。
- Canvas 读 `ThemeBridge` 时用 `renderStrategy: Canvas.Immediate`（Cooperative 会把它挪到渲染线程）。
- `PathAngleArc` 的角度：0° 在 3 点钟方向，y 轴向下，**正角度在屏幕上顺时针**（90°=6 点，180°=9 点，270°=12 点）。要画顶部一段写 `startAngle: -135; sweepAngle: 90`。
- **`PathSvg` 没有 `viewBox`**：路径串里的坐标按 1:1 设备像素画，不做任何缩放。从 `Icon.jsx` 抄 24 视口的路径进一个 15×15 的盒子，几何会原样画成 24×24 并往右下溢出。正确做法是给承载的 `Shape` 一个 `transformOrigin: Item.TopLeft` + `scale: width / 24` 的**整体缩放**，描边宽用设计稿原值（随 scale 一起变小）。⚠️ 只把 `strokeWidth` 乘 `width/24` 换算是**半截修复**，几何没缩，结果就是「图标比文字大、吊在文字下面、没居中」——`Icon.qml` 曾长期如此。
- `Loader.item` 的静态类型是 `QObject`，直接读 `implicitHeight` 会被 qmllint 报 `missing-property`；显式声明成 `Item` 又变成 `incompatible-type`。量正文高度用 `loader.childrenRect.height`（`PanelCard.qml` 是参考实现）。
- 上面那条的另一半：被 `Loader` 加载的正文，**根对象必须自写 `height: implicitHeight`**。不写的话 Loader 会接管高度，和外面读 `childrenRect` 的那层形成绑定环（`Binding loop detected`）。
- **`setContextProperty` 必须在 `Load()` 之前调**（`QmlAssetsPage::AcquireHost` 是参考实现）。`Load()` 内部会同步创建根对象并**首次求值所有绑定**，那一刻属性还没进上下文 → 绑定求值成 `undefined` 且被**永久缓存**，之后再注入也救不回来。症状是一屏 `ReferenceError: Page is not defined` + 连带 `TypeError`，而 `QQuickWidget` 自己一声不吭、`Load()` 还返回 true。`QmlPageReview` 的 `assets` 页同此纪律。
- **桥上的空态 map 必须带全零值键**。只回 `{none: true}` 的话，QML 侧 `Page.runtime.detail` / `.layer` / `.phaseLabel` 读到 `undefined` → `Unable to assign [undefined] to QString/QColor`，一次空态刷一片红。判「有没有」用 `none` 这类标志位，不要对字段做 truthiness。同 `Assets.qml` 的 `emptyAsset`。
- **主题键名写错不会报错，只会取到 `undefined`**：`fill.selected`（不是 `fill.active`）—— 赋给 `color` 报 `Unable to assign [undefined] to QColor`，整块底色不画。核对的土办法：把某套主题 JSON 的 `colors` 键名提出来，与 `ThemeBridge.colors["…"]` 的实参做差集。
- **QML 里不要写 `#RRGGBB` / `rgba(0,0,0,…)`**：`check-colors` 门禁会拦。要遮罩用 `Qt.alpha(ThemeBridge.colors["bg.void"], 0.86)`。

## 资源打包

`qt_add_resources` 用 `GLOB "src/ui/qml/*.qml"` + `CONFIGURE_DEPENDS`，**扁平**、同目录隐式导入。共享件用无前缀名即可直接 `<Button />`；**新增共享件不需要改 CMake**，重命名也会被自动重新扫描。命名纪律见下面「去掉 `Gallery` 前缀」一节 —— 页私有件**不该**带页面前缀。

## 已淘汰的页私有件（2026-09-29）

三次提交（`369065c` / `a5c37fd` / `0471d2d`）合计收掉 26 份重复实现，文件已删除。共享件的头部注释里仍会提到这些名字 —— 那是**历史来源**（记录每份旧实现贡献了什么、哪里写错了），不是活路径。想知道某个名字对应什么，看下表：

| 已删除 | 收进 |
|---|---|
| `GallerySpinner.qml` / `StoryboardSpin.qml` / `ImageFlowSpin.qml` | `Spinner.qml` |
| `AssetsBtn.qml` / `StoryboardButton.qml` / `ImageFlowBtn.qml`（外加旧冻结版 `Button.qml`） | `Button.qml` |
| `AssetsTag.qml` / `StoryboardTag.qml` | `Tag.qml` |
| `AssetsSeg.qml` / `ImageFlowSeg.qml` | `Seg.qml` |
| `GalleryArt.qml` / `AssetsArt.qml` / `ImageFlowArt.qml` | `Art.qml` |
| `GalleryDot.qml` / `ImageFlowDot.qml` | `Dot.qml` |
| `GalleryIconBtn.qml` / `ImageFlowIconBtn.qml` | `IconBtn.qml` |
| `GalleryKv.qml` / `AssetsKv.qml` | `Kv.qml` |
| `GalleryProgress.qml` / `ImageFlowProg.qml` | `Progress.qml` |
| `AssetsChip.qml` / `StoryboardChip.qml` | `Chip.qml` |
| `GalleryStageFlow.qml` / `StoryboardStage.qml` | `StageFlow.qml` + `StageNode.qml` |
| `GalleryFlow.qml` | `Flex.qml`（并补上 `align` / `justify`，末行可居中） |

`src/ui/qml` 的 `.qml` 文件数：去重前 **59**（`369065c^`）→ 去重后 **43**（`0471d2d`）→ 加进 `AutoGrid` / `Flex`、删掉 `GalleryFlow` 后 **44**。用 `git ls-tree -r --name-only <rev> -- src/ui/qml` 复核。

## 去掉 `Gallery` 前缀（2026-09-29）

`Gallery.qml` 是**组件陈列页**（对标 `webui/src/views/Gallery.jsx`），它下面 10 个 `Gallery*`
子件全部只被它一处引用 —— 前缀不表达任何依赖关系，只是命名污染。契约同「第二个页面也用得上
就是共享件」：用不上就去前缀。

| 旧名 | 新名 |
|---|---|
| `GalleryIcon.qml` | `Icon.qml` |
| `GalleryEmpty.qml` | `Empty.qml` |
| `GalleryArtInk.qml` | `ArtInk.qml` |
| `GalleryKbd.qml` | `Kbd.qml` |
| `GalleryTabs.qml` | `Tabs.qml` |
| `GalleryTable.qml` | `Table.qml` |
| `GalleryField.qml` | `Field.qml` |
| `GalleryInput.qml` | `Input.qml` |
| `GallerySelect.qml` | `Select.qml` |
| `GalleryTextArea.qml` | `TextArea.qml` |
| `GalleryToggle.qml` | `Toggle.qml` |
| `GalleryCard.qml` | `PanelCard.qml`（**不是 `Card2`**） |

⚠️ 去前缀会撞上已有共享件 `Card.qml`（面壳：`bg-panel` / `line-subtle` / `r-md`）。
`GalleryCard` 是**带标题栏 + 内容区的完整外壳**，语义不同。**不要用 `Card2` 这种序号后缀** ——
那比原前缀更糟，它把「撞名」记下来却不解决语义。按语义命名才是 `PanelCard`。

`GLOB` + `CONFIGURE_DEPENDS` 会自动重新扫描，改完重编即可；改名不必动 CMake。


## 自检

```powershell
C:\msys64\mingw64\bin\qmllint.exe -I C:/msys64/mingw64/share/qt6/qml <file.qml>
```

**不要加 `--bare`**：加了之后解析不到 QtQuick 的真实类型，属性类型/枚举检查全部失效。

已知预期噪音：`Failed to import Shine` 及其连带的 `Unqualified access`（C++ 注册的 QML 单例没有 qmltypes）。**除此之外零告警。**

端到端取证（出真图）只有主 Agent 能做：`SHINE_QML_REVIEW` + `SHINE_QML_PAGES` 环境变量跑 `ShineTVStudio.exe`，见 `docs/40-operations/verification.md`。子 Agent 交付时给出 qmllint 结果即可。
