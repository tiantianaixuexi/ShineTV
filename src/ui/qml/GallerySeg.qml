// src/ui/qml/GallerySeg.qml —— 分段控件（ui.css:207-242 的 .seg）
//
// 外框 p3 / gap2 / fill-muted / line-subtle / r-sm；选项 h26 / p0 13 / r-xs。
// 选中项除换底色外还有一枚 4px accent 圆点（.seg > button.on::after），
// 用 Item 里的 4px 圆点复刻。
import QtQuick
import Shine 1.0

pragma ComponentBehavior: Bound

Ctl {
    id: root

    property var options: []
    // 当前选中下标（Gallery.jsx 的 useState('列表') → 0，由页面给定）
    property int current: 0

    implicitHeight: 26 + 6 + 2      // 选项 26 + 上下 p3 + 上下 1px 边
    implicitWidth: row.implicitWidth + 6 + 2

    radius: root.rSm
    color: ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: ThemeBridge.colors["line.subtle"]

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: root.options
            delegate: Rectangle {
                id: seg
                required property int index
                required property string modelData

                width: Math.max(26, segText.implicitWidth + 26)   // p0 13 两侧
                height: 26
                radius: ThemeBridge.radii.xs
                color: root.current === seg.index ? ThemeBridge.colors["bg.elevated"] : "transparent"

                Text {
                    id: segText
                    anchors.left: parent.left
                    anchors.leftMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    text: seg.modelData
                    color: root.current === seg.index ? ThemeBridge.colors["text.primary"]
                                                      : (segMouse.containsMouse ? ThemeBridge.colors["text.primary"]
                                                                                 : ThemeBridge.colors["text.muted"])
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12          // 12.5px → 就近取整
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    visible: root.current === seg.index
                    anchors.left: segText.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4; height: 4; radius: 2
                    color: ThemeBridge.colors["accent.primary"]
                }
                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.current = seg.index
                }
            }
        }
    }
}
