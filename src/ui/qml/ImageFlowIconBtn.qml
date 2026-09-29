// src/ui/qml/ImageFlowIconBtn.qml —— 对照 webui ui.css:88-117 的 .icon-btn / .icon-btn.sm
// 画布工具（放大/缩小/适应视图）与面板折叠钮都用它。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string icon: ""
    property bool sm: false
    signal clicked()

    readonly property int box: sm ? 22 : 28          // .icon-btn.sm 22 / .icon-btn 28
    readonly property int iconBox: sm ? 13 : 16      // .icon-btn.sm .icon 13 / 16

    width: root.box
    height: root.box
    radius: root.rSm                                // --r-sm
    // .icon-btn 常态 --text-secondary；hover → --fill-hover + --text-primary
    color: mouse.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent"
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    Text {
        anchors.centerIn: parent
        text: root.icon
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.iconBox
        color: mouse.containsMouse ? ThemeBridge.colors["text.primary"]
                                   : ThemeBridge.colors["text.secondary"]
        Behavior on color {
            ColorAnimation { duration: root.reduce ? 0 : root.durFast }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { root.clicked() }
    }
}
