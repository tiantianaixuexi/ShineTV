// src/ui/qml/ImageFlowNode.qml —— 对照 webui views.css:1035-1099 的 .fnode
// （几何取自 FlowCanvas.jsx 的 NODE_W=150 / portPos 的 +38）
//
// ⚠️ 设计稿内部有一处自相矛盾，这里**照抄不改**：
//   views.css 的 .fnode 高度是内容自适应（≈64px），而 FlowCanvas.jsx:9-13 的
//   NODE_H=76 只用在 fit() 的包围盒，连线端点又硬编码 +38。两者对不上，
//   所以设计稿里连线端口落在卡片垂直中心**下方约 4px**。这里保留 +38，
//   fit() 仍用 76 的包围盒 —— 即逐点复刻设计稿的取景，不替它修。
//
// 色值纪律：设计稿用 color-mix(... transparent) 的三处（sel 环 / run 环 /
// done 边 / fail 边）一律换成 Qt.alpha(主题色, 原比例) —— 语义完全等价
// （alpha 合成到画布底色 = 与透明混合），且不引入第二个颜色真值。
import QtQuick
import QtQuick.Effects  // MultiEffect（.fnode 的 --shadow-1）
import Shine 1.0

Item {
    id: root

    property string title: ""
    property string sub: ""
    property string iconGlyph: ""
    // ⚠️ 属性名不能叫 state —— QQuickItem 已有同名的 state（状态机），
    // 重名会报 `property-override` 且破坏状态机语义。
    property string nodeState: "todo"     // todo / run / done / fail
    property bool selected: false
    signal picked()

    readonly property int nodeW: 150     // .fnode width 150px
    readonly property int portOffset: 38 // FlowCanvas.jsx:13 portPos 的 node.y + 38
    readonly property bool trailing: nodeState === "run" || nodeState === "done"

    width: nodeW
    height: 49 + subText.implicitHeight   // 7+15+7 + 1 + 7 + sub + 9 + 1.5*2

    // —— 选中 / 运行态的 3px 外圈（box-shadow: 0 0 0 3px …）——
    // 描边画不到卡外，所以用一张外扩 3px 的同圆角矩形垫在卡片**下面**。
    Rectangle {
        id: ring
        visible: root.selected || root.nodeState === "run"
        x: -3
        y: -3
        width: root.width + 6
        height: root.height + 6
        radius: ThemeBridge.radii.md + 3
        color: "transparent"
        border.width: 3
        border.color: root.selected
            ? Qt.alpha(ThemeBridge.colors["accent.primary"], 0.14)  // --accent-dim
            : Qt.alpha(ThemeBridge.colors["status.busy"], 0.18)    // mix(busy 18%)
    }

    Rectangle {
        id: card
        anchors.fill: parent
        color: ThemeBridge.colors["bg.panel"]
        radius: ThemeBridge.radii.md
        border.width: 1.5
        border.color: hit.containsMouse ? ThemeBridge.colors["line.strong"] : baseEdge
        Behavior on border.color {
            ColorAnimation {
                duration: ThemeBridge.reduceMotion ? 0 : ThemeBridge.durations.base
                easing.type: Easing.OutCubic
            }
        }

        // .fnode { box-shadow: var(--shadow-1) }
        layer.enabled: ThemeBridge.layerEffectsAvailable && !ThemeBridge.reduceMotion
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            shadowScale: 1.0
        }

        // 状态边色（.fnode.run / .done / .fail；todo 走 --line-normal）
        readonly property color baseEdge: root.nodeState === "run" ? ThemeBridge.colors["status.busy"]
                                  : root.nodeState === "done" ? Qt.alpha(ThemeBridge.colors["status.ok"], 0.45)
                                  : root.nodeState === "fail" ? Qt.alpha(ThemeBridge.colors["status.danger"], 0.5)
                                  : ThemeBridge.colors["line.normal"]

        // —— fhead：p7 10 · gap 7 · 12px w700 · 下边线 1px --line-subtle ——
        Item {
            id: head
            x: 10
            y: 7
            width: parent.width - 20
            height: 15

            Text {
                id: headIcon
                x: 0
                y: 1
                width: 13                 // .fhead .icon 13px
                text: root.iconGlyph
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                id: headText
                x: 20                    // 13 + gap 7
                y: 1
                width: head.width - 20 - (root.trailing ? 19 : 0)
                text: root.title
                elide: Text.ElideRight
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
                font.weight: Font.Bold   // font-weight: 700
                color: ThemeBridge.colors["text.primary"]
            }

            // 尾标：run → spin sm（11px）；done → 12px ✔（--ok）
            ImageFlowSpin {
                visible: root.nodeState === "run"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                sm: true
            }
            Text {
                visible: root.nodeState === "done"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "✔"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
                color: ThemeBridge.colors["status.ok"]
            }
        }

        Rectangle {
            x: 0
            y: 29       // 7 + 15 + 7 = fhead 高度
            width: parent.width
            height: 1
            color: ThemeBridge.colors["line.subtle"]
        }

        // —— fbody：p7 10 9 · 11px mono · --text-muted ——
        Text {
            id: subText
            x: 10
            y: 37       // 29 + 1 + 7
            width: parent.width - 20
            text: root.sub
            elide: Text.ElideRight
            font.family: root.monoFont
            font.pixelSize: 11
            color: ThemeBridge.colors["text.muted"]
        }

        // —— port：9px 圆 · 2px 边 · 底 --bg-panel ——
        Rectangle {
            x: -5.5                     // .port.in left -5.5px
            y: root.portOffset - 4.5
            width: 9; height: 9; radius: 4.5
            color: ThemeBridge.colors["bg.panel"]
            border.width: 2
            // in 口取状态色（FlowCanvas.jsx:179 的 STATE_COLOR）
            border.color: root.nodeState === "done" ? ThemeBridge.colors["status.ok"]
                          : root.nodeState === "run" ? ThemeBridge.colors["status.busy"]
                          : root.nodeState === "fail" ? ThemeBridge.colors["status.danger"]
                          : ThemeBridge.colors["text.muted"]
        }
        Rectangle {
            x: card.width - 9 + 5.5     // .port.out right -5.5px
            y: root.portOffset - 4.5
            width: 9; height: 9; radius: 4.5
            color: ThemeBridge.colors["bg.panel"]
            border.width: 2
            border.color: root.nodeState === "run" ? ThemeBridge.colors["accent.primary"]
                                                   : ThemeBridge.colors["line.strong"]
        }

        MouseArea {
            id: hit
            anchors.fill: parent
            hoverEnabled: true
            onClicked: { root.picked() }
        }
    }

    // ⚠️ ThemeBridge 只桥出了每主题的 UI 字体族，没有 --font-mono 那一档
    // （tokens.css:31 的 Cascadia Code / JetBrains Mono / Consolas 链）。
    // 这里取 Windows 自带的 Consolas 作为等宽替身，是本页唯一一处非桥取值。
    readonly property string monoFont: "Consolas"
}
