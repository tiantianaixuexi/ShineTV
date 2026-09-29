// src/ui/qml/Art.qml —— 过程艺术占位图（共享件：ui.css:1075-1090 的 .art / .art-cover）
//
// ================================ 合并来源：三份手搓实现收成一份 ================================
//   GalleryArt.qml   : seed + Shapes/PathSvg 真 Q 曲线；调色板只有 **6** 组；无 zoom、无 dimmed
//   AssetsArt.qml    : seed + zoom/zoomed（hover 1.07 档）；调色板 **12** 组；叠圆角 Rectangle 近似山形
//   ImageFlowArt.qml : **无 seed**（固定三色）+ Canvas 真 Q 曲线 + dimmed
//   → 统一：**12 组 token 三角 + seed + zoom/zoomed + dimmed**，绘制统一走 QtQuick.Shapes + PathSvg。
// =================================================================================================
//
// 设计稿（行号均为 webui/ 下真实位置）：
//   .art            { position:relative; overflow:hidden; border-radius:var(--r-sm);  ← ui.css:1075-1080
//                     background:var(--fill-muted) }
//   .art svg        { display:block; width:100%; height:100%;                         ← ui.css:1081-1086
//                     transition: transform 0.5s var(--ease-out) }
//   .art-cover svg  { position:absolute; inset:0 }                                      ← ui.css:1087-1090
//   Art 构图         viewBox 0 0 160 100 / preserveAspectRatio="xMidYMid slice"        ← UI.jsx:204
//                     12 组 ART_PAL（写死 hex，与主题无关）                            ← UI.jsx:184-197
//                     圆日 cx=sx cy=34 r=13 @.9 + r=20 @.25，三道山脊                     ← UI.jsx:212-216
//   hover 缩放       .asset-card:hover .thumb svg { transform: scale(1.07) }           ← views.css:720-722
//   --r-sm: 6px                                                                       ← tokens.css:10
//
// 几何（viewBox 160×100 → cover 裁切）：
//   preserveAspectRatio="xMidYMid slice" = 按 cover 缩放、居中、溢出裁掉，
//   故缩放系数 k = max(盒宽/160, 盒高/100)，内容 Item 摆在 x = (盒宽-160k)/2、y = (盒高-100k)/2。
//   ⚠️ 两个 transformOrigin 各管一件事，别写反：
//      · `frame`（装 160×100 内容的那层）取 **TopLeft** —— offX/offY 就是按「缩放绕
//        左上角」推的，取 Center 会让整幅画向左上平移 80(k-1)/50(k-1)，不填满盒子。
//      · `zoomBox`（hover 缩放那层）取 **Center** —— CSS 是对 svg 元素（盒 = 100%×100%
//        = 本组件盒）做 scale(1.07)，transform-origin 默认 50% 50%。因为 frame 铺满后
//        160×100 内容框的**视觉中心恰好落在本组件盒心**上，zoomBox 的中心 (80,50) 映射过去
//        正是盒心，取 Center 才与 CSS 等价（这里取 TopLeft 会让缩放朝左上角跑）。
//
// 三份旧实现的绘制手法为什么统一成 Shapes/PathSvg：
//   · ImageFlowArt 用 Canvas —— 真 Q 曲线没问题，但要 renderStrategy: Canvas.Immediate 才读得到
//     ThemeBridge（Cooperative 会把 onPaint 挪到渲染线程），而且每次改尺寸/换色都要 requestPaint。
//   · AssetsArt 用「三层上圆角 Rectangle」—— 画不出 Q 曲线，拐点位置靠 (seed%4/%6/%8) 手调，
//     与设计稿的路径算式对不上，且山脊高度与 UI.jsx:214-216 的三条 d 全不相同。
//   · GalleryArt 的 Shapes + PathSvg 直接吃 SVG 路径串，是三者里唯一与 UI.jsx 逐字符同源的。
//
// ⚠️ 踩过的坑（Shape 系，ArtInk.qml:20-30 与 docs ⑩ 有完整版）：
//   1. ShapePath 的默认描边是「不透明白色、宽 1」——纯填充**必须**显式写
//      `strokeColor: "transparent"; strokeWidth: 0`，否则每座山脊上留一圈白轮廓
//      （ArtInk 实测：单层不透明填充时边缘 alpha 冲到 87）。
//   2. ShapePath **没有** opacity 属性，SVG 的 fill-opacity 只能折算进 fillColor 的 alpha
//      （纯色填充下两者渲染结果完全一致）。
//   3. ShapePath 派生自 QQuickPath（QObject，不是 QQuickItem），**不能**当 Repeater 的 delegate；
//      Shape 本身当 delegate 也会在运行期炸（ImageFlowLink.qml:15-17 记着这条）。三座山只能字面展开。
//
// 为什么调色板统一成 12 组（三份旧实现不一致：GalleryArt 6 组 / AssetsArt 12 组 / ImageFlowArt 不分种子）：
//   1. 设计稿 ART_PAL 就是 12 组（UI.jsx:184-197），取模索引也是 % 12。表大小跟着设计稿走，
//      页面按 seed 取色时不会出现「设计稿有 12 种、QML 只有 6 种」的对不上。
//   2. 12 组能覆盖 AssetsArt 已上线的全部种子（3/6/9/12 等时间轴取景点位都 < 12），改成 6 组会让
//      seed 12 与 seed 0 撞色，缩略图网格里出现两张一模一样的图。
//   3. ImageFlowArt 原来不分种子（固定 bg.void / accent.primary / status.warn），现在等价于
//      seed = 0 = 第 1 组（bg.void / accent.primary / accent.secondary）。⚠️ 旧的固定三色在 12 组
//      三角里**没有**对应行（没有 c1=accent.primary 且 c2=status.warn 的行），所以 ImageFlow 迁移后
//      光源色会从 status.warn 变成 accent.secondary；这是「12 组 + token 化」的必然取舍，出图确认。
//
// ⚠️ 已知缺口（出图才能确认）：
//   1. .art 的 6px 圆角 + overflow:hidden 裁不掉 —— QML 的 clip 只认矩形框，缩放后的内容是直角，
//      会盖住圆角处的底色。设计稿是圆角裁切，这里是直角裁切。
//   2. UI.jsx:212-213 的绘制顺序是「r13 核心先画、r20 光晕后画」，本实现反过来（光晕在下、核心在上），
//      好让核心在 0.9 透明度下保持干净；r20 只有 0.25，两种顺序肉眼无差，但要出图确认。
import QtQuick
import QtQuick.Shapes
import Shine 1.0

Ctl {
    id: root

    // —— 调用方 API ——
    // 种子同时决定配色（artPal 取模）与构图（圆心 sx + 三条山脊的拐点），与设计稿同一套算式
    property int seed: 0
    // zoom = 允许 hover 缩放（.asset-card 的那档）；zoomed = 当前处于 hover 态。
    // ⚠️ 两者必须分开：缩放动效只有 0.5s 过渡（.art svg 的 transition），
    //    hover 态归调用方（卡片自己知道鼠标在不在自己身上）。
    property bool zoom: false
    property bool zoomed: false
    // 未评审时整块 opacity .25（ImageFlowArt 的 dimmed），过渡走 --dur-3
    property bool dimmed: false

    // .art svg { width:100%; height:100% } —— CSS 没给内在尺寸，这里用 viewBox 盒当默认
    // （AssetsArt 同值）。三个页面的调用点（Gallery.qml:593 92×60、ImageFlow.qml:718 150 高、
    // ImageFlow.qml:806 190 高、Assets* 各 .thumb）全都显式给了宽高，implicit 只是兜底。
    implicitWidth: 160
    implicitHeight: 100

    radius: root.rSm                          // .art { border-radius: var(--r-sm) }  = 6
    color: ThemeBridge.colors["fill.muted"]   // .art { background: var(--fill-muted) }
    clip: true                                // .art { overflow: hidden }

    // —— 12 组主题 token 三角，替代设计稿写死的 ART_PAL（UI.jsx:184-197）——
    // ⚠️ 取模顺序与行序沿用 AssetsArt 的 12 组，理由见文件头「为什么调色板统一成 12 组」。
    // ⚠️ 属性名不能叫 palette —— QQuickItem 已有 palette 成员，重复声明会覆盖基类成员。
    readonly property var artPal: [
        ["bg.void",          "accent.primary",   "accent.secondary"],
        ["bg.void",          "accent.secondary",  "status.warn"],
        ["bg.void",          "accent.info",       "status.busy"],
        ["bg.void",          "status.pending",    "text.primary"],
        ["bg.void",          "accent.secondary",  "status.danger"],
        ["bg.void",          "status.ok",         "status.warn"],
        ["bg.void",          "status.busy",       "accent.info"],
        ["bg.void",          "status.danger",     "accent.secondary"],
        ["bg.void",          "accent.primary",    "status.pending"],
        ["bg.void",          "status.warn",       "status.ok"],
        ["bg.void",          "text.secondary",    "status.danger"],
        ["bg.void",          "accent.secondary",  "accent.info"]
    ]
    readonly property var pal: root.artPal[Math.abs(root.seed) % root.artPal.length]
    readonly property color cDark: ThemeBridge.colors[root.pal[0]]   // 暗底 / 天空顶 / 山一·山三
    readonly property color c1:    ThemeBridge.colors[root.pal[1]]   // 天光 / 天空底 55% / 山二 30%
    readonly property color c2:    ThemeBridge.colors[root.pal[2]]   // 光源

    // UI.jsx:201 —— 圆心横坐标
    readonly property real sx: 30 + ((root.seed * 37) % 40)

    // UI.jsx:214-216 —— 三道山脊，路径串逐字符照抄（含 T 平滑二次段）
    readonly property string p1: "M0 78 Q " + (20 + (root.seed % 20)) + " 58 " + (45 + (root.seed % 15))
                                 + " 74 T 100 70 T 160 76 V100 H0 Z"
    readonly property string p2: "M0 88 Q " + (35 - (root.seed % 18)) + " 72 " + (70 + (root.seed % 12))
                                 + " 86 T 160 84 V100 H0 Z"
    readonly property string p3: "M0 94 Q 50 86 100 92 T 160 90 V100 H0 Z"

    // cover 缩放系数 + 居中偏移（见文件头「几何」）
    readonly property real k: Math.max(root.width / 160, root.height / 100)
    readonly property real offX: (root.width - 160 * root.k) / 2
    readonly property real offY: (root.height - 100 * root.k) / 2
    // views.css:721 的 scale(1.07)。⚠️ 乘法必须作用在**独立的**内层 Item 上：
    //    同一项上既有 k 又有 hover 倍数时，Behavior 会把「改尺寸导致的 k 变化」也一起补间，
    //    缩略图换尺寸时会拖一条 0.5s 的缩放动画。
    readonly property real zoomK: (root.zoom && root.zoomed) ? 1.07 : 1.0

    // .art-cover 的 inset:0 + .art 的 overflow:hidden —— 裁切由根的 clip 承担
    Item {
        id: frame
        x: root.offX
        y: root.offY
        width: 160
        height: 100
        // ⚠️ **必须是 TopLeft**，不能是 Center（2026-09-29 实测踩出来的）。
        // offX/offY 是按「缩放绕左上角」推的：渲染矩形 = (offX, offY) 到
        // (offX+160k, offY+100k)，正好是 cover 裁切并居中的那一块。
        // 换成 Center，Qt 会把缩放支点放在本项中心 (80,50) 上，渲染矩形整体向左上
        // 平移 80(k-1) / 50(k-1)。实测：40×25 的项 scale 4.5 渲��在
        // (-70,-43.75)-(110,68.75)，而它的盒子在 (0,0)-(180,112.5)。
        // 资产页对比台 640×400（k=4）因此只画出左上角 400×250，右侧和下侧整片
        // 露出底色 —— 就是「Art 没居中 / 没填满」。
        transformOrigin: Item.TopLeft
        scale: root.k

        // 未评审整块 .25 —— 挂在内容上（与 .art 的底色无关），过渡走 --dur-3 = durSlow
        opacity: root.dimmed ? 0.25 : 1.0
        Behavior on opacity {
            NumberAnimation { duration: root.reduce ? 0 : root.durSlow }
        }

        Item {
            id: zoomBox
            width: 160
            height: 100
            transformOrigin: Item.Center
            scale: root.zoomK
            // .art svg { transition: transform 0.5s var(--ease-out) }
            Behavior on scale {
                NumberAnimation { duration: root.reduce ? 0 : 500; easing.type: Easing.OutCubic }
            }

            // 天空：linear-gradient(dark → c1 @ 0.55)  ← UI.jsx:206-211
            Rectangle {
                width: 160
                height: 100
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: root.cDark }
                    GradientStop { position: 1.0; color: Qt.alpha(root.c1, 0.55) }
                }
            }

            // 光源：r20 光晕 @.25 + r13 核心 @.9，圆心 (sx, 34)  ← UI.jsx:212-213
            Rectangle {
                x: root.sx - 20
                y: 34 - 20
                width: 40
                height: 40
                radius: 20
                color: Qt.alpha(root.c2, 0.25)
            }
            Rectangle {
                x: root.sx - 13
                y: 34 - 13
                width: 26
                height: 26
                radius: 13
                color: Qt.alpha(root.c2, 0.9)
            }

            // 三道山脊 ← UI.jsx:214-216
            Shape {
                anchors.fill: parent
                // ⚠️ 三个 ShapePath 逐个展开：ShapePath 是 QObject 不是 QQuickItem，
                //    既不能进 Repeater，也不能给 opacity（fill-opacity 折进 fillColor 的 alpha）。
                // ⚠️ 每一层都必须显式关描边，否则山脊上留一圈不透明白轮廓。
                ShapePath {
                    strokeColor: "transparent"; strokeWidth: 0
                    fillColor: Qt.alpha(root.cDark, 0.75)
                    PathSvg { path: root.p1 }
                }
                ShapePath {
                    strokeColor: "transparent"; strokeWidth: 0
                    fillColor: Qt.alpha(root.c1, 0.3)
                    PathSvg { path: root.p2 }
                }
                ShapePath {
                    strokeColor: "transparent"; strokeWidth: 0
                    fillColor: Qt.alpha(root.cDark, 0.9)
                    PathSvg { path: root.p3 }
                }
            }
        }
    }
}
