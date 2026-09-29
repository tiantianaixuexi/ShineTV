pragma ComponentBehavior: Bound
// src/ui/qml/AssetsDerive.qml —— 页面私有：V0 派生链
//
// 对照 webui/src/styles/views.css:849-895 + ui.css:456-470：
//   .derive              flex / align-items center / padding 6px 2px / overflow-x auto
//   .derive .dnode       r-md 10 / line-normal 边 1 / fill-muted 底 / hover → accent-glow 边 + shadow-1
//   .derive .dnode .dthumb   height 64px
//   .derive .dnode .dlabel   padding 6px 8px / f11 w700 / text-secondary / gap 5
//   .derive .dlink       22px × 1.5px / line-normal；.fill 时整条换成 accent
//   .spin.sm             11px / border 1.5px line-normal / border-top accent / 0.7s linear
//   .dot                 7px 圆点
//
// ⚠️ 两处尺寸来自 jsx 的行内覆盖（views.css 的 108 / 64 被覆盖成 92 / 50），
// 以 jsx 为准：Assets.jsx:223-224。
import QtQuick
import QtQuick.Effects  // MultiEffect（节点 hover 的 shadow-1）
import Shine 1.0

Ctl {
    id: root

    // items: [{ name: "正脸", state: "done" | "run" | "todo" }]
    property var items: []
    signal nodePicked(string name)

    readonly property real nodeW: 92    // jsx 行内 width: 92（覆盖 CSS 的 108）
    readonly property real thumbH: 50   // jsx 行内 height: 50（覆盖 CSS 的 64）
    readonly property real labelH: 18   // 11px × 1.6
    readonly property real linkW: 22    // .derive .dlink width: 22px
    readonly property real nodeH: thumbH + 6 + labelH + 6

    implicitHeight: 92     // padding 6 + node 80 + padding 6
    implicitWidth: 92
    color: "transparent"
    antialiasing: true

    function nodeX(i) {
        var x = 2      // .derive padding-left 2px
        for (var k = 0; k < i; ++k) {
            x += nodeW + linkW
        }
        return x
    }

    Repeater {
        model: root.items
        delegate: Rectangle {
            id: node
            required property var modelData
            required property int index
            x: root.nodeX(node.index)
            y: 6
            width: root.nodeW
            height: root.nodeH
            radius: root.rMd
            color: ThemeBridge.colors["fill.muted"]
            border.width: 1
            border.color: nodeMouse.containsMouse
                          ? Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)  // accent-glow
                          : ThemeBridge.colors["line.normal"]
            Behavior on border.color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }

            layer.enabled: root.shadows && nodeMouse.containsMouse
            layer.effect: MultiEffect {
                shadowEnabled: nodeMouse.containsMouse && !root.reduce
                shadowBlur: 1.0
                shadowScale: 1.0
            }

            // 缩略图（顶部圆角 + 自裁切，等价 .dnode 的 overflow hidden）
            Rectangle {
                id: dthumb
                x: 0
                y: 0
                width: node.width
                height: root.thumbH
                topLeftRadius: root.rMd
                topRightRadius: root.rMd
                color: "transparent"
                clip: true
                AssetsArt {
                    width: dthumb.width
                    height: dthumb.height
                    seed: 5 + node.index      // jsx: <Art seed={5 + i} />
                }
            }

            // .dlabel：状态点 / 转圈 + 名字
            // ⚠️ 点与转圈二选一，所以**显式定位**（套 Row 的话两者互不可见时
            // 仍可能留一份 gap 5）。状态位宽 7（点）/ 11（转圈），文字统一从 x=12 起。
            Item {
                id: dlabel
                x: 8
                y: root.thumbH + 6
                width: node.width - 16
                height: root.labelH

                Rectangle {                       // .dot 7px
                    x: 0
                    y: (dlabel.height - height) / 2
                    visible: node.modelData.state !== "run"
                    width: 7
                    height: 7
                    radius: 3.5
                    color: node.modelData.state === "done"
                           ? ThemeBridge.colors["status.ok"]      // <StatusDot tone="ok">
                           : ThemeBridge.colors["status.idle"]     // <StatusDot tone="idle">
                }
                Rectangle {                       // .spin.sm 11px / 1.5px 边（int → 1）/ 0.7s linear
                    x: 0
                    y: (dlabel.height - height) / 2
                    visible: node.modelData.state === "run"
                    width: 11
                    height: 11
                    radius: 5.5
                    color: "transparent"
                    border.width: 1
                    border.color: ThemeBridge.colors["line.normal"]
                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 700
                        loops: Animation.Infinite
                        running: node.modelData.state === "run" && !root.reduce
                    }
                    // border-top 单独换色：border.width 是 int，QML 没有 border-top-color，
                    // 用一条 1px 的 accent 顶边高亮表示「转圈起点」
                    Rectangle {
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 11
                        height: 1
                        color: ThemeBridge.colors["accent.primary"]
                    }
                }
                Text {
                    x: 12                                  // gap 5 + 状态位 7
                    y: 0
                    width: Math.max(0, dlabel.width - 12 - (node.modelData.state === "run" ? 10 : 5))
                    height: dlabel.height
                    verticalAlignment: Text.AlignVCenter
                    text: node.modelData.name
                    elide: Text.ElideRight
                    color: ThemeBridge.colors["text.secondary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            // 连接线：state !== todo 时整条换成 accent（.dlink.fill）
            Rectangle {
                visible: node.index > 0
                x: -root.linkW
                y: (node.height - 1.5) / 2
                width: root.linkW
                height: 1.5
                color: node.modelData.state !== "todo"
                       ? ThemeBridge.colors["accent.primary"]
                       : ThemeBridge.colors["line.normal"]
            }

            // 描边压在缩略图之上（见 AssetsAssetCard 里的同名说明）
            Rectangle {
                anchors.fill: parent
                radius: root.rMd
                color: "transparent"
                border.width: 1
                border.color: node.border.color
            }

            MouseArea {
                id: nodeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.nodePicked(node.modelData.name)
            }
        }
    }
}
