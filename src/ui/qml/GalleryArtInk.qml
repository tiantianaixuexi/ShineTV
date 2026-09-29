// src/ui/qml/GalleryArtInk.qml —— 水墨装饰（UI.jsx:223-250 的 ArtInk）
//
// ⚠️ 这是**设计稿有、Widgets 侧完全没有**的一块（对 src/ui 全仓 grep
//    `ArtInk|远山|淡墨|焦墨|印章` 无任何匹配），本页补齐。
//
// 视口 800×260 / preserveAspectRatio="xMidYMax slice"（UI.jsx:226），
// 五层山 + 飞白小舟 + 印章，几何逐值照抄：
//   远山淡墨  M0 208 Q 90 120 190 176 T 400 158 T 620 178 T 800 150 V260 H0 Z  α .08 blur7
//   远山淡墨  M0 226 Q 130 150 250 200 T 520 186 T 800 196 V260 H0 Z        α .13 blur7
//   中景浓墨  M0 240 Q 110 178 230 222 T 470 210 T 720 226 T 800 214 V260 H0 Z  α .22
//   近景焦墨  M0 254 Q 160 216 330 244 T 660 238 T 800 246 V260 H0 Z        α .42
//   飞白小舟  M(520+seed%40) 236 q 16 8 34 0                                α .50 sw2
//             M(536+seed%40) 224 l 3 -9 m 0 0 l 8 2                          α .40 sw1.4
//   印章      translate(700+seed%30, 40) 26×26 r4 = --accent-2；内框与十字 = --accent-fg
//
// 颜色全部取自主题 token（--text-primary / --accent-2 / --accent-fg）。
//
// ── 四个必须记住的坑（全部实测得出，不是推测）──
//
// 1. **ShapePath 的默认描边是「不透明白色、宽 1」**，不是黑色、也不是透明。
//    只设 fillColor 的话，每一片山脊上都会留一圈白色轮廓；7 片模糊层各留一圈，
//    叠起来 alpha 冲到 **172/255（67%）** —— 裁图上就是那 6~7 道亮白弧线。
//    ⚠️ 这**不是**低 alpha 填充的边缘台阶：把 7 片换成单层不透明填充，边缘仍是
//    alpha 87；而只要显式写 `strokeColor: "transparent"`（或 `strokeWidth: 0`），
//    峰值立刻回到设计值 20~21。**所以每一个纯填充的 ShapePath 都必须显式关掉描边。**
//    （这条同样适用于 GalleryArt.qml 的三座山 —— 那里是同一个缺陷。）
//
// 2. **Shape 里不能用 Repeater**。`ShapePath` 派生自 QQuickPath（QObject），
//    不是 QQuickItem，Repeater 直接报 `Delegate must be of Item type`。
//    所以两层远山的 7 片模糊偏移是**显式展开**成 14 个 ShapePath 的。
//
// 3. **模糊是「平移」不是「前缀」**。feGaussianBlur stdDeviation=7 用 7 片
//    圆周偏移的等 alpha 叠印近似（ShapePath 没有描边模糊，layer.effect 在
//    software 场景图后端下会吞掉整个 item）。偏移是把路径里**每一个坐标**
//    都加上 (dx,dy)，不是简单在前面拼一个 M —— 单独一个 M 只是移动当前点，
//    图形根本不跟着动。
//
// 4. **transformOrigin 必须是 TopLeft**。设计稿是 `slice`（取 max，必然溢出
//    再由 overflow 裁掉），配 TopLeft 原点时：内容铺到 [px, px+800k] ×
//    [py, py+260k]，px=(w-800k)/2 横向居中（xMid）、py=h-260k 底边对齐（YMax），
//    溢出部分由 clip 裁掉。之前用 BottomLeft 导致内容整体下移 110px 冲出卡片。
import QtQuick
import QtQuick.Shapes
import Shine 1.0

Ctl {
    id: root

    property int seed: 2              // Gallery.jsx 用的是 seed=2

    implicitWidth: 300
    implicitHeight: 150

    radius: root.rSm
    color: "transparent"              // .art 在这一张上被显式改成透明底
    clip: true

    readonly property color ink: ThemeBridge.colors["text.primary"]
    readonly property color sealFill: ThemeBridge.colors["accent.secondary"]   // --accent-2
    readonly property color sealLine: ThemeBridge.colors["accent.primary.fg"]

    // —— 路径（照抄 UI.jsx:233-246）——
    readonly property string far1: "M0 208 Q 90 120 190 176 T 400 158 T 620 178 T 800 150 V260 H0 Z"
    readonly property string far2: "M0 226 Q 130 150 250 200 T 520 186 T 800 196 V260 H0 Z"
    readonly property string mid:  "M0 240 Q 110 178 230 222 T 470 210 T 720 226 T 800 214 V260 H0 Z"
    readonly property string near: "M0 254 Q 160 216 330 244 T 660 238 T 800 246 V260 H0 Z"
    readonly property string boat: "M" + (520 + (root.seed % 40)) + " 236 q 16 8 34 0"
    readonly property string mast: "M" + (536 + (root.seed % 40)) + " 224 l 3 -9 m 0 0 l 8 2"
    readonly property int sealX: 700 + (root.seed % 30)
    readonly property int sealY: 40

    // slice = cover：取 max 保证铺满，溢出交给 clip
    readonly property real k: Math.max(root.width / 800, root.height / 260)

    Item {
        x: (root.width - 800 * root.k) / 2      // xMid：横向居中
        y: root.height - 260 * root.k          // YMax：底边对齐
        width: 800
        height: 260
        transformOrigin: Item.TopLeft
        scale: root.k

        Shape {
            anchors.fill: parent

            // —— 远山淡墨第 1 层（α .08，7 片模糊偏移）——
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M0 208 Q 90 120 190 176 T 400 158 T 620 178 T 800 150 V260 H0 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M4.9 210.5 Q 94.9 122.5 194.9 178.5 T 404.9 160.5 T 624.9 180.5 T 804.9 152.5 V262.5 H4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M4.9 205.5 Q 94.9 117.5 194.9 173.5 T 404.9 155.5 T 624.9 175.5 T 804.9 147.5 V257.5 H4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M0 213 Q 90 125 190 181 T 400 163 T 620 183 T 800 155 V265 H0 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M-4.9 210.5 Q 85.1 122.5 185.1 178.5 T 395.1 160.5 T 615.1 180.5 T 795.1 152.5 V262.5 H-4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M-4.9 205.5 Q 85.1 117.5 185.1 173.5 T 395.1 155.5 T 615.1 175.5 T 795.1 147.5 V257.5 H-4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.08 / 7)
                PathSvg { path: "M0 203 Q 90 115 190 171 T 400 153 T 620 173 T 800 145 V255 H0 Z" }
            }

            // —— 远山淡墨第 2 层（α .13，7 片模糊偏移）——
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M0 226 Q 130 150 250 200 T 520 186 T 800 196 V260 H0 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M4.9 228.5 Q 134.9 152.5 254.9 202.5 T 524.9 188.5 T 804.9 198.5 V262.5 H4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M4.9 223.5 Q 134.9 147.5 254.9 197.5 T 524.9 183.5 T 804.9 193.5 V257.5 H4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M0 231 Q 130 155 250 205 T 520 191 T 800 201 V265 H0 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M-4.9 228.5 Q 125.1 152.5 245.1 202.5 T 515.1 188.5 T 795.1 198.5 V262.5 H-4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M-4.9 223.5 Q 125.1 147.5 245.1 197.5 T 515.1 183.5 T 795.1 193.5 V257.5 H-4.9 Z" }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.13 / 7)
                PathSvg { path: "M0 221 Q 130 145 250 195 T 520 181 T 800 191 V255 H0 Z" }
            }

            // —— 中景浓墨 / 近景焦墨（无模糊）——
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.22)
                PathSvg { path: root.mid }
            }
            ShapePath {
                strokeColor: "transparent"; strokeWidth: 0
                fillColor: Qt.alpha(root.ink, 0.42)
                PathSvg { path: root.near }
            }

            // —— 飞白小舟（这两条要真描边，fill 必须 transparent）——
            ShapePath {
                strokeColor: Qt.alpha(root.ink, 0.5)
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathSvg { path: root.boat }
            }
            ShapePath {
                strokeColor: Qt.alpha(root.ink, 0.4)
                strokeWidth: 1.4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathSvg { path: root.mast }
            }
        }

        // —— 印章（整体 opacity .9）——
        Item {
            x: root.sealX
            y: root.sealY
            width: 26
            height: 26
            opacity: 0.9

            Rectangle {
                width: 26; height: 26; radius: 4
                color: root.sealFill
            }
            Rectangle {
                x: 5; y: 5
                width: 16; height: 16; radius: 2
                color: "transparent"
                border.width: 1.6
                border.color: root.sealLine
            }
            // 十字：M9 13h8M13 9v8（stroke 1.6 round cap）
            Rectangle {
                x: 9; y: 13 - 0.8
                width: 8; height: 1.6
                color: root.sealLine
            }
            Rectangle {
                x: 13 - 0.8; y: 9
                width: 1.6; height: 8
                color: root.sealLine
            }
        }
    }
}
