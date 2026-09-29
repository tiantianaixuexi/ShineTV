// src/ui/qml/StoryboardStage.qml —— ui.css:814-893 的 .stageflow / .snode / .slink
//
// .stageflow      { flex + align-items center + padding 4 2 + overflow-x auto }
// .stageflow .slink{ 18×1.5 线-normal }  ::after accent scaleX(0→1)（已完成的连线）
// .stageflow .snode{ h30 p0 11 r-pill · line-normal 边 · fill-muted 底 · f12 w600 · text-muted }
//   .code { mono f10.5→10 w700 opacity .8 }
//   :hover      { line-strong 边 + text-primary 字 }
//   .done       { text-primary + ok 边/底 + .code 用 ok }
//   .run        { text-primary + accent 边 + accent-dim 底 + 3px accent-dim 光环 + shadow-accent }
//   .fail       { danger 字 + danger 边/底 }
//   .skip       { opacity .45 + 虚线边 }
//
// 组件 = 一根连接线 + 一个节点（StageFlow.jsx:15-29 就是这两个元素成对渲染），
// 所以 `hasLink` 为真时宽度含那 18px，由外层 Row 自然串起来。
//
// ⚠️ 混色比例：设计稿的 ok 40% 边 / ok 10% 底 / accent-dim 14% 底在桥上没有对应档，
// ThemeBridge 只暴露 toneEdge（35%）与 toneBg（12%），这里统一取这两档并注明。
// ⚠️ .skip 的虚线边：QML 的 Rectangle 没有 dashed 边（要 QtQuick.Shapes），
// 本页 stateOf 也不会返回 skip，只落 opacity 0.45。
import QtQuick
import QtQuick.Effects        // MultiEffect（.run 的 accent 辉光）
import Shine 1.0

// MultiEffect 里要用外层 id（root），显式声明 Bound 才合法
pragma ComponentBehavior: Bound

Ctl {
    id: root

    property string code: ""
    property string stageName: ""
    property string stageState: "todo"   // todo / run / done / fail / skip
    property bool hasLink: false         // i > 0 时前置一根 .slink

    readonly property int linkW: hasLink ? 18 : 0
    readonly property int dotW: 7        // .dot 7px（.run 的 .spin.sm 11px 居中溢出 2px/侧，
                                         //  换来节点宽度在四种状态下一致，不随每次 tick 横跳）
    readonly property real nodeW: 22 + dotW + 7 + codeText.implicitWidth + 7 + nameText.implicitWidth

    height: 30
    width: linkW + nodeW
    color: "transparent"

    // link 在前一根节点「已完成」时才填色（StageFlow.jsx:16 st === 'done'）
    readonly property real fillRatio: root.hasLink && (root.stageState === "done" || root.stageState === "run") ? 1 : 0

    // —— .slink ——
    Rectangle {
        x: 0
        y: (root.height - height) / 2
        width: 18
        height: 1.5
        color: ThemeBridge.colors["line.normal"]
        visible: root.hasLink
        // .slink.fill::after { transform: scaleX(1) } —— 用宽度代替 scaleX
        Rectangle {
            width: parent.width * root.fillRatio
            height: parent.height
            color: ThemeBridge.colors["accent.primary"]
            Behavior on width {
                NumberAnimation { duration: root.reduce ? 0 : root.durSlow; easing.type: Easing.OutCubic }
            }
        }
    }

    // —— .snode ——
    Rectangle {
        id: ring
        x: root.linkW - 3
        y: -3
        width: root.nodeW + 6
        height: root.height + 6
        radius: height / 2
        color: "transparent"
        border.width: 3                  // .snode.run { box-shadow: 0 0 0 3px accent-dim }
        border.color: ThemeBridge.toneBg["accent"]
        visible: root.stageState === "run"
    }

    Rectangle {
        id: node
        x: root.linkW
        y: 0
        width: root.nodeW
        height: root.height
        radius: height / 2               // --r-pill
        opacity: root.stageState === "skip" ? 0.45 : 1
        Behavior on opacity {
            NumberAnimation { duration: root.reduce ? 0 : root.durFast }
        }
        color: root.stageState === "run"  ? ThemeBridge.toneBg["accent"]
             : root.stageState === "done" ? ThemeBridge.toneBg["ok"]
             : root.stageState === "fail" ? ThemeBridge.toneBg["danger"]
             : ThemeBridge.colors["fill.muted"]
        border.width: 1
        border.color: area.containsMouse ? ThemeBridge.colors["line.strong"]
                   : root.stageState === "run"  ? ThemeBridge.colors["accent.primary"]
                   : root.stageState === "done" ? ThemeBridge.toneEdge["ok"]
                   : root.stageState === "fail" ? ThemeBridge.toneEdge["danger"]
                   : ThemeBridge.colors["line.normal"]
        Behavior on border.color {
            ColorAnimation { duration: root.reduce ? 0 : root.durBase }
        }
        Behavior on color {
            ColorAnimation { duration: root.reduce ? 0 : root.durBase }
        }

        layer.enabled: root.shadows && root.stageState === "run"
        layer.effect: MultiEffect {
            shadowEnabled: root.stageState === "run" && !root.reduce
            shadowBlur: 1.0
            shadowScale: 1.0
        }

        // 状态点：run → .spin.sm，其余 → StatusDot（.dot 7px）
        StoryboardSpin {
            // .spin.sm 11px 居中在 7px 的状态点位上（向两侧各溢出 2px，仍在 11px 内边距内）
            x: 11 - (height - root.dotW) / 2
            y: (node.height - height) / 2
            visible: root.stageState === "run"
            sm: true
        }
        Rectangle {
            x: 11
            y: (node.height - height) / 2
            width: root.dotW
            height: root.dotW
            radius: width / 2
            visible: root.stageState !== "run"
            color: root.stageState === "done" ? ThemeBridge.colors["status.ok"]
                 : root.stageState === "fail" ? ThemeBridge.colors["status.danger"]
                 : ThemeBridge.colors["status.idle"]
        }

        Text {
            id: codeText
            x: 11 + root.dotW + 7
            y: (node.height - height) / 2
            text: root.code
            font.family: "Consolas"          // --font-mono 栈里必定存在的一项
            font.pixelSize: 10            // 10.5 → 10
            font.weight: Font.Bold
            lineHeight: 1.6
            lineHeightMode: Text.FixedHeight
            opacity: 0.8                  // .snode .code { opacity: .8 }
            color: root.stageState === "done" ? ThemeBridge.colors["status.ok"]
                 : root.stageState === "run"  ? ThemeBridge.colors["accent.primary"]
                 : (area.containsMouse ? ThemeBridge.colors["text.primary"]
                                        : ThemeBridge.colors["text.muted"])
        }

        Text {
            id: nameText
            x: codeText.x + codeText.implicitWidth + 7
            y: (node.height - height) / 2
            text: root.stageName
            visible: root.stageName !== "" && root.stageName !== root.code
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            lineHeight: 1.6
            lineHeightMode: Text.FixedHeight
            color: root.stageState === "todo" && !area.containsMouse
                   ? ThemeBridge.colors["text.muted"]
                   : ThemeBridge.colors["text.primary"]
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
