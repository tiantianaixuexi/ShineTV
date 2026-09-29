// src/ui/qml/StoryboardTag.qml —— ui.css:120-145 的 .tag / .tag.sm + UI.jsx:32-40 的点
//
// 为什么不用冻结的 Tag.qml：本页三处 Tag 都带设计稿已有、而 Tag.qml 未暴露的变体
// —— 头部 Tag 要 `dot`（UI.jsx:36，busy 时带脉冲）、阶段卡与连续性卡要 `sm`
// （h17 / p0 6 / f10）。tone → token 的映射与 toneBg/toneEdge 取值**照抄**冻结的
// Tag.qml（同一个 ThemeBridge、同一个真值），几何补齐 sm/dot 两档。
//
// .tag     { h20 p0 8 r-pill f11.5 w600 · line-normal 边 · fill-muted 底 · text-secondary 字 }
// .tag.sm  { h17 p0 6 f10.5 }
// tone 变体: color: tone · border: tone 35% · background: tone 12%
// .tag.idle{ color: text-muted }（无混色底/边）
// .dot     { 7px 正圆, background: currentColor } .dot.run { pulse-dot 1.6s }
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    property string tone: ""          // accent/info/ok/warn/danger/busy/pending/idle，空 = 默认
    property bool sm: false
    property bool dot: false          // UI.jsx:36 的前置状态点

    // tone → token 点分名（与 Tag.qml 同一条规则：accent 在 CSS 里是 --accent，
    // 本仓的 token 名却是 accent.primary）
    readonly property string toneToken: tone === "accent" ? "accent.primary"
                                            : tone === "info"  ? "accent.info"
                                                              : "status." + tone
    readonly property bool plain: tone === "" || tone === "idle"

    readonly property color bgColor: plain ? ThemeBridge.colors["fill.muted"]
                                           : ThemeBridge.toneBg[tone]
    readonly property color fgColor: tone === ""    ? ThemeBridge.colors["text.secondary"]
                                    : tone === "idle" ? ThemeBridge.colors["text.muted"]
                                    : ThemeBridge.colors[toneToken]
    readonly property color edgeColor: plain ? ThemeBridge.colors["line.normal"]
                                             : ThemeBridge.toneEdge[tone]

    implicitHeight: sm ? 17 : 20
    implicitWidth: row.implicitWidth + (sm ? 12 : 16)
    radius: rPill

    color: bgColor
    border.width: 1
    border.color: edgeColor

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5                      // .tag { gap: 5px }
        Item {
            width: 7
            height: 7
            visible: root.dot
            Rectangle {
                anchors.fill: parent
                radius: 3.5
                color: root.fgColor    // UI.jsx:36 style={{ background: 'currentColor' }}
            }
            // .dot.run { animation: pulse-dot 1.6s } —— box-shadow 0→5px 扩散。
            // CSS 的 box-shadow 在 QML 只能自绘：叠一圈同色环，scale 1→2 + 透明度 0.35→0。
            // 「减少动效」下退化为静态实心点（语义仍是「运行中」）。
            Rectangle {
                id: pulse
                anchors.centerIn: parent
                width: 7
                height: 7
                radius: 3.5
                color: "transparent"
                border.width: 1
                border.color: root.fgColor
                SequentialAnimation on scale {
                    running: root.dot && root.tone === "busy" && !ThemeBridge.reduceMotion
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 2; duration: 800; easing.type: Easing.OutCubic }
                }
                SequentialAnimation on opacity {
                    running: root.dot && root.tone === "busy" && !ThemeBridge.reduceMotion
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.35; to: 0; duration: 800; easing.type: Easing.OutCubic }
                }
            }
        }
        Text {
            text: root.text
            font.family: ThemeBridge.fontFamily
            font.pixelSize: root.sm ? 10 : 11   // 10.5 / 11.5 就近取整（docs 〇节字号档）
            font.weight: Font.DemiBold
            color: root.fgColor
        }
    }
}
