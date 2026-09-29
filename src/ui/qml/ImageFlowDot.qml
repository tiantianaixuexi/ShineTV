// src/ui/qml/ImageFlowDot.qml —— 对照 webui ui.css:164-172 的 .dot / .dot.run
// （.dot.run 走 base.css:143-146 的 @keyframes pulse-dot）
//
// pulse-dot 在 CSS 里是 box-shadow 0 0 0 0 → 0 0 0 5px transparent，
// QML 没有 box-shadow 扩散，用一圈「外扩 + 淡出」的同尺寸环近似：
// 半径 0 → 5px 对应 scale 1 → 1.53，透明度 0.3 → 0 对应 opacity 1 → 0。
// 环色取 --accent-glow（accent 30%），与 base.css 的 pulse-dot 同源。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property color tone: ThemeBridge.colors["status.idle"]
    property bool run: false

    width: 7      // .dot 7px
    height: 7
    radius: 3.5
    color: "transparent"

    Rectangle {
        anchors.centerIn: parent
        width: 7; height: 7
        radius: 3.5
        color: root.tone
    }

    // 脉冲环：只用 opacity / scale 两条绑定 + 一段 NumberAnimation，
    // 「减少动效」下 running:false 短路（停在不透明度 0，即完全不可见）。
    Rectangle {
        id: ring
        anchors.centerIn: parent
        width: 7; height: 7
        radius: 3.5
        color: "transparent"
        border.width: 5
        border.color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)
        opacity: 0
        visible: root.run
        SequentialAnimation on scale {
            running: root.run && !root.reduce
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.53; duration: 800; easing.type: Easing.InOutQuad }
        }
        SequentialAnimation on opacity {
            running: root.run && !root.reduce
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.0; duration: 800; easing.type: Easing.InOutQuad }
        }
    }
}
