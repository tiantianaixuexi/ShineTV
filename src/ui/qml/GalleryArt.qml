// src/ui/qml/GalleryArt.qml —— 程序化占位画（UI.jsx:198-220 的 Art）
//
// 视口 160×100 / preserveAspectRatio="xMidYMid slice"：按 cover 缩放并居中，
// 溢出部分裁掉。种子决定圆心横坐标与两条山形路径的拐点，与设计稿同一套算式：
//   sx   = 30 + ((seed*37) % 40)
//   山一 = M0 78 Q (20+seed%20) 58 (45+seed%15) 74 T 100 70 T 160 76 V100 H0 Z
//   山二 = M0 88 Q (35-seed%18) 72 (70+seed%12) 86 T 160 84 V100 H0 Z
//
// ⚠️ 唯一的偏离：**配色**。设计稿的 ART_PAL 是 12 组写死的十六进制，
// 与主题无关；本页禁止硬编码色值，所以这里换成「每颗种子一组主题 token」，
// 保留「种子决定配色与构图」这件事本身（切换主题时占位画也跟着换色）。
//
// ⚠️ `.art` 是 r-sm 圆角 + overflow:hidden。QML 的 clip 只认矩形框，
// 四角 6px 圆角裁不掉，这里按直角裁切处理。
import QtQuick
import QtQuick.Shapes
import Shine 1.0

Ctl {
    id: root

    property int seed: 0

    implicitWidth: 92
    implicitHeight: 60

    radius: root.rSm
    color: ThemeBridge.colors["fill.muted"]
    clip: true

    // 12 组 token 三角（替代设计稿写死的 ART_PAL）
    readonly property var artPal: [
        ["bg.void",          "accent.primary",   "accent.secondary"],
        ["bg.void",          "accent.info",      "status.busy"],
        ["bg.void",          "accent.secondary", "accent.primary"],
        ["bg.void",          "status.pending",   "text.primary"],
        ["bg.void",          "status.warn",      "status.danger"],
        ["bg.void",          "status.ok",        "status.warn"]
    ]
    readonly property var pal: artPal[Math.abs(root.seed) % artPal.length]
    readonly property color cDark: ThemeBridge.colors[pal[0]]
    readonly property color c1: ThemeBridge.colors[pal[1]]
    readonly property color c2: ThemeBridge.colors[pal[2]]

    readonly property int sx: 30 + ((root.seed * 37) % 40)

    readonly property string p1: "M0 78 Q " + (20 + (root.seed % 20)) + " 58 " + (45 + (root.seed % 15))
                                 + " 74 T 100 70 T 160 76 V100 H0 Z"
    readonly property string p2: "M0 88 Q " + (35 - (root.seed % 18)) + " 72 " + (70 + (root.seed % 12))
                                 + " 86 T 160 84 V100 H0 Z"
    readonly property string p3: "M0 94 Q 50 86 100 92 T 160 90 V100 H0 Z"

    // cover：scale = max(w/160, h/100)，居中裁切
    readonly property real k: Math.max(root.width / 160, root.height / 100)

    Item {
        x: (root.width - 160 * root.k) / 2
        y: (root.height - 100 * root.k) / 2
        width: 160
        height: 100
        transformOrigin: Item.TopLeft
        scale: root.k

        // 天空：linear-gradient(dark → c1 @ 0.55)
        Rectangle {
            width: 160
            height: 100
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: root.cDark }
                GradientStop { position: 1.0; color: Qt.alpha(root.c1, 0.55) }
            }
        }

        // 圆日：r13 @ .9 + 外晕 r20 @ .25
        Rectangle {
            x: root.sx - 13; y: 34 - 13
            width: 26; height: 26; radius: 13
            color: Qt.alpha(root.c2, 0.9)
        }
        Rectangle {
            x: root.sx - 20; y: 34 - 20
            width: 40; height: 40; radius: 20
            color: Qt.alpha(root.c2, 0.25)
        }

        Shape {
            anchors.fill: parent
            // ⚠️ ShapePath 没有 opacity 属性（它不是 QML 可见的 QQuickItem 透明度），
            //    所以 SVG 的 fill-opacity 折算进 fillColor 的 alpha 里 ——
            //    纯色填充下两者渲染结果完全一致。
            // ⚠️ 而且**必须显式关掉描边**：ShapePath 的默认描边是「不透明白色、
            //    宽 1」，只设 fillColor 会在每座山脊上留一圈白色轮廓
            //    （实测：单层不透明填充时边缘 alpha 冲到 87）。
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
