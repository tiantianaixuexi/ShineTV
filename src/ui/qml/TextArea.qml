// src/ui/qml/TextArea.qml —— 多行输入（ui.css:300-305 的 .textarea）
//
// p8 10 / r-sm / fill-muted + line-normal 边 / line-height 1.6；
// 设计稿里它是可拖拽调高的，本页只静态复刻 rows=2 的高度。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string placeholder: ""
    property string text: ""
    property int rows: 2

    readonly property int lineH: Math.round(ThemeBridge.baseFontPx * 1.6)   // 21

    implicitHeight: 16 + root.rows * lineH          // p8 上下
    implicitWidth: 160

    radius: root.rSm
    color: input.activeFocus ? ThemeBridge.colors["bg.surface"] : ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: input.activeFocus ? ThemeBridge.colors["line.focus"]
                : (hover.hovered ? ThemeBridge.colors["line.strong"] : ThemeBridge.colors["line.normal"])
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
    }

    TextEdit {
        id: input
        anchors.fill: parent
        anchors.margins: 8
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        text: root.text
        color: ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: ThemeBridge.baseFontPx
        wrapMode: TextEdit.Wrap
        selectByMouse: true

        Text {
            visible: input.text.length === 0
            text: root.placeholder
            color: ThemeBridge.colors["text.muted"]
            font: input.font
        }
    }

    HoverHandler { id: hover }
}
