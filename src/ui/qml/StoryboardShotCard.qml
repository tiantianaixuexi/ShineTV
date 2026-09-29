// src/ui/qml/StoryboardShotCard.qml —— views.css:951-1003 的 .timeline / .tl-card
//
// .tl-card      { w128 · r-md · 1px line-normal 边 · fill-muted 底 · overflow hidden }
// .tl-card:hover{ accent-glow 边 + shadow-1 + translateY(-2px) }（transition dur-2）
// .tl-card.dragging { opacity .45 }
// 选中（Storyboard.jsx:154 inline）：accent-glow 边 + 0 0 0 3px accent-dim 光环
// .tthumb h72 ｜ .tinfo { padding: 7px 9px 9px }
// .tcode { mono f11 w700 accent } ｜ .taction { f11.5→11 text-secondary · mt2 · 省略号 }
// .tdur  { mt6 · flex · gap 6 · f10.5→10 text-muted } = .prog.thin(最小 30) + 时长
//
// ⚠️ 三处替代，都不是色值真值的偏离：
//  1. accent-glow（accent 30% alpha）→ ThemeBridge.toneEdge["accent"]（35% 叠底，不透明）。
//     bridge 没有 30% 这一档，与冻结的 Tag.qml 取同一档。
//  2. accent-dim（accent 14% alpha）→ ThemeBridge.toneBg["accent"]（12% 叠底）。
//  3. 3px 外扩光环用**自绘外框**（外扩 3px 的描边矩形）而不是 box-shadow：
//     software 场景图后端下 layer.effect 会把 item 整个吞掉（ThemeBridge 已探测）。
//
// ⚠️ 定位纪律：translateY(-2px) 挂在**外层 Item** 上（`lift`），不能挂到 anchors.fill
// 的 Rectangle 上 —— 锚定与 y 互斥，挂了整块不显示（docs 第五之二节纪律 4）。
import QtQuick
import QtQuick.Effects        // MultiEffect（.tl-card:hover 的 shadow-1）
import Shine 1.0

// MultiEffect 里要用外层 id（root / hover），显式声明 Bound 才合法
pragma ComponentBehavior: Bound

Ctl {
    id: root

    property string code: ""
    property string action: ""
    property string dur: ""
    property real seconds: 0
    property string status: "todo"
    property bool selected: false
    property bool moved: false            // 按下后位移 > 4px 才算拖动中
    property real pressX: 0
    readonly property bool dragging: moved && hover.pressed

    signal picked()
    signal grabbedAt(real contentX)
    signal releasedAt(real contentX)

    readonly property int thumbH: 72              // .tthumb { height: 72px }
    readonly property int infoPadX: 9             // .tinfo { padding: 7px 9px 9px }
    readonly property real infoW: width - 2 - infoPadX * 2   // 减掉 1px 边框
    readonly property real progressW: Math.max(30, infoW - durText.implicitWidth - 6)

    width: 128
    height: thumbH + 7 + codeText.implicitHeight + 2 + actionText.implicitHeight + 6
           + Math.max(4, durText.implicitHeight) + 9
    opacity: dragging ? 0.45 : 1                  // .tl-card.dragging { opacity: .45 }
    color: "transparent"

    Item {
        id: lift
        width: parent.width
        height: parent.height
        y: hover.containsMouse && !root.reduce ? -2 : 0   // translateY(-2px)
        Behavior on y {
            NumberAnimation { duration: root.reduce ? 0 : root.durBase; easing.type: Easing.OutCubic }
        }

        // 选中光环：0 0 0 3px accent-dim（外扩一圈描边，不吃 layer）
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: root.rMd + 3
            color: "transparent"
            border.width: 3
            border.color: ThemeBridge.toneBg["accent"]
            visible: root.selected
        }

        Rectangle {
            id: plate
            anchors.fill: parent
            radius: root.rMd
            color: ThemeBridge.colors["fill.muted"]
            border.width: 1
            border.color: (hover.containsMouse || root.selected) ? ThemeBridge.toneEdge["accent"]
                                                              : ThemeBridge.colors["line.normal"]
            Behavior on border.color {
                ColorAnimation { duration: root.reduce ? 0 : root.durBase }
            }
            clip: true                                  // .tl-card { overflow: hidden }

            layer.enabled: root.shadows && hover.containsMouse
            layer.effect: MultiEffect {
                shadowEnabled: hover.containsMouse && !root.reduce
                shadowBlur: 1.0
                shadowScale: 1.0
            }

            // —— .tthumb h72 ——
            Item {
                id: thumb
                x: 1
                y: 1
                width: parent.width - 2
                height: root.thumbH

                // 已出图：设计稿是 <Art seed/>（SVG 占位画）。QML 侧没有资源，
                // 用一块中性底板占位，形状/高度与 Art 一致。
                Rectangle {
                    anchors.fill: parent
                    visible: root.status === "done"
                    color: ThemeBridge.colors["bg.elevated"]
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 4                        // inline gap: 4
                    visible: root.status !== "done"
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "▣"                      // Icon name="image" 20×20
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 16
                        color: ThemeBridge.colors["text.muted"]
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "待出图"                 // .tiny
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        color: ThemeBridge.colors["text.muted"]
                    }
                }
            }

            // —— .tinfo { padding: 7px 9px 9px } ——
            Item {
                id: info
                x: 1 + root.infoPadX
                y: root.thumbH + 7
                width: root.infoW
                height: parent.height - root.thumbH - 7 - 9

                Text {
                    id: codeText
                    x: 0
                    y: 0
                    text: root.code
                    font.family: "Consolas"                        // --font-mono 栈里必定存在的一项
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    lineHeight: 1.6
                    lineHeightMode: Text.FixedHeight
                    color: ThemeBridge.colors["accent.primary"]
                }

                Text {
                    id: actionText
                    x: 0
                    y: codeText.y + codeText.implicitHeight + 2      // .taction { margin-top: 2px }
                    width: parent.width
                    text: root.action
                    elide: Text.ElideRight
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 11            // 11.5 → 11
                    lineHeight: 1.6
                    lineHeightMode: Text.FixedHeight
                    color: ThemeBridge.colors["text.secondary"]
                }

                // .tdur { margin-top: 6px; display: flex; align-items: center; gap: 6px }
                Item {
                    id: durRow
                    x: 0
                    y: actionText.y + actionText.implicitHeight + 6
                    width: parent.width
                    height: durText.implicitHeight

                    // .prog.thin { height: 4px; r-pill; fill-muted 底 } + .prog > i { grad-accent }
                    Rectangle {
                        id: progTrack
                        x: 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.progressW
                        height: 4
                        radius: height / 2
                        color: ThemeBridge.colors["fill.muted"]
                        Rectangle {
                            width: Math.min(100, root.seconds / 6 * 100) / 100 * parent.width
                            height: parent.height
                            radius: height / 2
                            gradient: Gradient {
                                // --grad-accent: linear-gradient(120deg, accent, info)
                                // ⚠️ 降级：设计稿的 120° 斜向渐变在 QML 做不到 —— Gradient.orientation
                                // 是**枚举**（Horizontal / Vertical / Linear），不接受角度数值
                                // （写成 120 是运行期 "unknown enumeration"，整页加载失败）。
                                // 120deg 的主走向是「自左上向右下、偏横向」，这里取最接近的单轴
                                // Horizontal：两端的色（accent → info）与设计稿一致，只有倾角没了。
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: ThemeBridge.colors["accent.primary"] }
                                GradientStop { position: 1.0; color: ThemeBridge.colors["accent.info"] }
                            }
                            Behavior on width {
                                NumberAnimation { duration: root.reduce ? 0 : root.durSlow; easing.type: Easing.OutCubic }
                            }
                        }
                    }

                    Text {
                        id: durText
                        x: progTrack.width + 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.dur
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 10            // 10.5 → 10
                        lineHeight: 1.6
                        lineHeightMode: Text.FixedHeight
                        color: ThemeBridge.colors["text.muted"]
                    }
                }
            }
        }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onPressed: function (mouse) {
            root.pressX = mouse.x
            root.moved = false
            root.grabbedAt(root.x + mouse.x)
        }
        onPositionChanged: {
            if (pressed) {
                // MouseArea.mouseX 是 double（局部 x），不是 point —— 写 mouseX.x 是运行期错误
                if (Math.abs(hover.mouseX - root.pressX) > 4) {
                    root.moved = true              // HTML5 dragstart 的位移阈值
                }
                root.grabbedAt(root.x + hover.mouseX)
            }
        }
        onReleased: function (mouse) {
            root.releasedAt(root.x + mouse.x)
        }
        onClicked: {
            if (!root.moved) {
                root.picked()
            }
        }
    }
}
