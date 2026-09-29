// src/ui/qml/GalleryDot.qml —— 状态点（ui.css:164-172 的 .dot / .dot.run）
//
// 7×7 圆点 + 满强度 tone 色。`run` 态复刻 base.css:143-146 的 @keyframes pulse-dot：
// 那条关键帧动的是 box-shadow 的 spread（accent-glow 0px → 5px 扩散并淡出），
// QML 没有 box-shadow，用一圈 border.width 0→5 且同步淡出的同心环近似。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // tone → token 点分名（与 Tag.qml 同一条映射，accent 在 CSS 里是 --accent）
    property string tone: "idle"
    property bool run: false

    // 显式透明：Rectangle.color 默认是白色，不写就是一个白色圆角方块
    color: "transparent"

    implicitWidth: 7
    implicitHeight: 7

    readonly property string toneToken: tone === "accent" ? "accent.primary"
                                        : tone === "info"  ? "accent.info"
                                                              : "status." + tone

    // pulse-dot 的 spread（px）。0→5 对应 box-shadow 0 0 0 0 → 0 0 0 5px
    property real spread: 0
    readonly property bool pulse: root.run && !root.reduce

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        visible: root.pulse
        border.width: root.spread
        border.color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)   // --accent-glow
        opacity: 1 - root.spread / 5
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: ThemeBridge.colors[root.toneToken]
    }

    // 1.6s 一轮（base.css pulse-dot 1.6s）：0→5→0，InOutQuad
    SequentialAnimation {
        running: root.pulse
        loops: Animation.Infinite
        NumberAnimation { target: root; property: "spread"; to: 5; duration: 800; easing.type: Easing.InOutQuad }
        NumberAnimation { target: root; property: "spread"; to: 0; duration: 800; easing.type: Easing.InOutQuad }
    }
}
