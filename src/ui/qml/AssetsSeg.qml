pragma ComponentBehavior: Bound
// src/ui/qml/AssetsSeg.qml —— 页面私有：.seg（详情 / 总览 切换）
//
// 对照 webui/src/styles/ui.css:206-242：
//   .seg               p3 / gap2 / fill-muted 底 / line-subtle 边 / r-sm 6
//   .seg > button      h26 / p0 13 / r-xs 4 / f12.5 w600 / text-muted
//   .seg > button:hover 字 text-primary
//   .seg > button.on   底 bg-elevated / 字 text-primary / shadow-1
//   .seg > button.on::after  4px accent 圆点（margin-left 6 / vertical-align 2）
import QtQuick
import QtQuick.Effects  // MultiEffect（选中段的 shadow-1）
import Shine 1.0

Ctl {
    id: root

    // options: [{ value: "detail", label: "详情" }, …]
    property var options: []
    property string value: ""
    signal picked(string v)

    implicitHeight: 34        // 3 + 26 + 3 + 上下各 1px 边
    implicitWidth: inner.implicitWidth + 8
    radius: root.rSm          // --r-sm 6
    color: ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: ThemeBridge.colors["line.subtle"]
    antialiasing: true

    Row {
        id: inner
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: root.options
            delegate: Rectangle {
                id: seg
                required property var modelData
                width: seg.modelData.value === root.value
                       ? segLabel.implicitWidth + 6 + 4 + 26   // padding 0 13 + ::after(margin 6 + 4)
                       : segLabel.implicitWidth + 26
                height: 26
                radius: ThemeBridge.radii.xs
                color: seg.modelData.value === root.value ? ThemeBridge.colors["bg.elevated"]
                                                          : "transparent"
                border.width: 0
                scale: segMouse.pressed && !root.reduce ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: root.reduce ? 0 : root.durFast }
                }
                Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }

                // 选中段的 shadow-1（software 后端下 ThemeBridge 会关掉 layer）
                layer.enabled: root.shadows && seg.modelData.value === root.value
                layer.effect: MultiEffect {
                    shadowEnabled: seg.modelData.value === root.value && !root.reduce
                    shadowBlur: 1.0
                    shadowScale: 1.0
                }

                // ⚠️ 选中圆点显式定位（.on::after 只在选中时存在）；套 Row 的话
                // 未选中段会凭空多出 6 + 4 的宽度。
                Text {
                    id: segLabel
                    x: 13
                    y: (seg.height - height) / 2
                    text: seg.modelData.label
                    color: seg.modelData.value === root.value
                           ? ThemeBridge.colors["text.primary"]
                           : (segMouse.containsMouse ? ThemeBridge.colors["text.primary"]
                                                     : ThemeBridge.colors["text.muted"])
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12      // .seg > button f12.5px → 取整 12（QssBuilder 的 seg 同值）
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    visible: seg.modelData.value === root.value
                    x: segLabel.x + segLabel.implicitWidth + 6      // margin-left: 6px
                    y: (seg.height - height) / 2 + 2              // vertical-align: 2px
                    width: 4
                    height: 4
                    radius: 2
                    color: ThemeBridge.colors["accent.primary"]
                }

                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.picked(seg.modelData.value)
                }
            }
        }
    }
}
