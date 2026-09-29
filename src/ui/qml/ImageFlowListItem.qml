// src/ui/qml/ImageFlowListItem.qml —— 对照 webui views.css:293-326 的 .dlist / .drow
// 发丝线分隔的密集行（无卡片框）：p8 2 / gap 8 / 1px --line-subtle 下边线
// （:last-child 无）/ hover --fill-hover / 12.5px --text-secondary。
//
// 行内容交给调用方绝对定位：inner 是一个填满行内容区的 Item（不是定位器），
// 这样 hover 只改 color、不会和定位器抢 y。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property bool last: false            // .dlist .drow:last-child { border-bottom: none }
    property bool clickable: false
    signal clicked()

    default property alias content: inner.data
    implicitHeight: 36                   // 8 + 20 + 8，单行内容时的默认高度
    radius: 0
    color: mouse.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent"
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        visible: !root.last
        color: ThemeBridge.colors["line.subtle"]
    }

    Item {
        id: inner
        anchors {
            left: parent.left; leftMargin: 2     // padding: 0 2px
            right: parent.right; rightMargin: 2
            top: parent.top; topMargin: 8
            bottom: parent.bottom; bottomMargin: 8
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: root.clickable ? Qt.LeftButton : Qt.NoButton
        onClicked: { root.clicked() }
    }
}
