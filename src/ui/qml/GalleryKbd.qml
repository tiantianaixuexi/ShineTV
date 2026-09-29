// src/ui/qml/GalleryKbd.qml —— 快捷键键帽（ui.css:147-162 的 .kbd）
//
// min-width 18 / h18 / p0 5 / r-xs / 1px 边 + 2px 下边（模拟键帽厚度）
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string text: ""

    implicitHeight: 18
    implicitWidth: Math.max(18, label.implicitWidth + 10)

    radius: ThemeBridge.radii.xs          // --r-xs 4
    color: ThemeBridge.colors["bg.elevated"]
    border.width: 1
    border.color: ThemeBridge.colors["line.normal"]

    // border-bottom-width: 2px（键帽厚度）
    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 1
        color: ThemeBridge.colors["line.normal"]
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: ThemeBridge.colors["text.secondary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 10            // .kbd 10.5px → 就近取整
        font.weight: Font.DemiBold
    }
}
