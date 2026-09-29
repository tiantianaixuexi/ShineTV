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
            // 数据源 = AssetPageModel 的 Page.deriveChain，每项带 absPath（真实产物
            // 绝对路径，仅当产物落盘就绪时才非空）。就绪画真图，没就绪画占位 ——
            // 绝不用 Art 的 seed 占位图冒充真实产物。
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
                Image {
                    anchors.fill: parent
                    visible: node.modelData.ready === true && node.modelData.absPath !== ""
                    source: node.modelData.absPath !== ""
                           ? "file:///" + node.modelData.absPath : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: Math.round(node.width * 2)
                    sourceSize.height: Math.round(root.thumbH * 2)
                    // 无产物时的说明位（缺层文案：已登记未落盘 / 尚未生成）
                    Text {
                        anchors.centerIn: parent
                        visible: !node.modelData.ready
                        width: parent.width - 8
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: node.modelData.state === "run" ? "已登记\n未落盘" : "未生成"
                        color: ThemeBridge.colors["text.muted"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 9
                        lineHeight: 1.2
                    }
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
                Spinner {                            // .spin.sm：11px / 1.5px 边 / 0.7s linear
                    x: 0
                    y: (dlabel.height - height) / 2
                    visible: node.modelData.state === "run"
                    sm: true
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
