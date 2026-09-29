// src/ui/qml/GalleryIconBtn.qml —— 纯图标钮（ui.css:88-117 的 .icon-btn）
//
// 28×28 / r-sm，文字色 text-secondary；hover 走 fill-hover + text-primary，
// active 走 fill-selected + accent。
//
// 设计稿靠 [data-tip] 的 ::after 伪元素做 tooltip；本页 tooltip 由宿主
// 承担（WidgetGalleryView 侧同名控件已有），这里只复刻钮本体几何。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string icon: ""
    property bool active: false
    property string tip: ""

    implicitWidth: 28
    implicitHeight: 28

    radius: root.rSm
    color: active ? ThemeBridge.colors["fill.selected"]
         : (mouse.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent")
    Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }

    GalleryIcon {
        anchors.centerIn: parent
        width: 16
        height: 16
        name: root.icon
        glyphColor: root.active ? ThemeBridge.colors["accent.primary"]
                 : (mouse.containsMouse ? ThemeBridge.colors["text.primary"] : ThemeBridge.colors["text.secondary"])
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.active = !root.active
    }
}
