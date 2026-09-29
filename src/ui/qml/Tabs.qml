// src/ui/qml/Tabs.qml —— 下划线标签页（ui.css:245-275 的 .tabs）
//
// gap2 + line-subtle 下边线；标签 p8 12 / f13 w600，选中项走 accent 色
// 并在 bottom: -1px 处长一条 2px accent 圆头下划线（两侧内缩 10px）。
import QtQuick
import Shine 1.0

pragma ComponentBehavior: Bound

Ctl {
    id: root

    property var tabs: []
    property int current: 0

    implicitHeight: 8 + 16 + 8 + 1       // p8 + 13px 行高取整 + p8 + 1px 下边线
    implicitWidth: row.implicitWidth

    color: "transparent"

    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 1
        color: ThemeBridge.colors["line.subtle"]
    }

    Row {
        id: row
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        spacing: 2

        Repeater {
            model: root.tabs
            delegate: Item {
                id: tab
                required property int index
                required property string modelData
                width: tabText.implicitWidth + 24        // p0 12
                height: parent.height

                Text {
                    id: tabText
                    anchors.centerIn: parent
                    text: tab.modelData
                    color: root.current === tab.index ? ThemeBridge.colors["accent.primary"]
                                     : (tabMouse.containsMouse ? ThemeBridge.colors["text.primary"]
                                                              : ThemeBridge.colors["text.muted"])
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: ThemeBridge.baseFontPx      // 13
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    visible: root.current === tab.index
                    anchors { bottom: parent.bottom; left: parent.left; leftMargin: 10; right: parent.right; rightMargin: 10 }
                    height: 2
                    radius: 1
                    color: ThemeBridge.colors["accent.primary"]
                }
                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.current = tab.index
                }
            }
        }
    }
}
