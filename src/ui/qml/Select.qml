// src/ui/qml/Select.qml —— 下拉选择（ui.css:323-329 的 .select）
//
// 几何与 .input 完全一致（h30 / p0 10 / r-sm / fill-muted + line-normal），
// 只是右侧 p-right 26 留给 10×6 的 chevron 箭头（ui.css:325 的 data-uri SVG）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property var options: []
    property int current: 0

    implicitHeight: 30
    implicitWidth: 160

    radius: root.rSm
    color: hover.hovered ? ThemeBridge.colors["fill.hover"] : ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: hover.hovered ? ThemeBridge.colors["line.strong"] : ThemeBridge.colors["line.normal"]
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 26          // background-position: right 10px center
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: root.options.length > 0 ? root.options[root.current] : ""
        color: ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: ThemeBridge.baseFontPx
    }

    // 10×6 箭头：M1 1l4 4 4-4，stroke 1.5 round cap
    Item {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 10
        height: 6

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: 6; height: 1.5
            color: ThemeBridge.colors["text.muted"]
            rotation: 45
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: 6; height: 1.5
            color: ThemeBridge.colors["text.muted"]
            rotation: -45
        }
    }

    HoverHandler { id: hover }
    TapHandler {
        onTapped: root.current = (root.current + 1) % Math.max(1, root.options.length)
    }
}
