// src/ui/qml/GalleryInput.qml —— 单行输入（ui.css:288-299 的 .input）
//
// h30 / p0 10 / r-sm / fill-muted 底 + line-normal 边；hover 换 line-strong，
// focus 换 line-focus + 3px accent-dim 光圈（= --accent-dim）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string placeholder: ""
    property string text: ""

    implicitHeight: 30
    implicitWidth: 160

    radius: root.rSm
    color: input.activeFocus ? ThemeBridge.colors["bg.surface"] : ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: input.activeFocus ? ThemeBridge.colors["line.focus"]
                : (hover.hovered ? ThemeBridge.colors["line.strong"] : ThemeBridge.colors["line.normal"])
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        verticalAlignment: TextInput.AlignVCenter
        text: root.text
        color: ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: ThemeBridge.baseFontPx
        selectByMouse: true
        clip: true

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length === 0
            text: root.placeholder
            color: ThemeBridge.colors["text.muted"]
            font: input.font
        }
    }

    HoverHandler { id: hover }
}
