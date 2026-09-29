// src/ui/qml/ImageFlowSeg.qml —— 对照 webui ui.css:207-242 的 .seg
// padding 3 / gap 2 / --fill-muted 底 / 1px --line-subtle 边 / --r-sm；
// 按钮 h26 · p0 13 · --r-xs · 12.5px w600，on 态是 --bg-elevated 底 +
// 标签右侧 4px 的 --accent 圆点（.seg > button.on::after，margin-left 6）。
//
// ⚠️ 12.5px 在 QML 取 12：与既有约定一致（kit/controls 的 .vsec-h 13.5px → 13px，
// Tag.qml 的 11.5px → 11px），半像素在灰度上不可见。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects  // MultiEffect（.seg > button.on 的 --shadow-1）
import Shine 1.0

Ctl {
    id: root

    // [{ value: "bind", label: "绑定" }, ...]
    property var options: []
    property string value: ""
    signal picked(string v)

    implicitHeight: 26 + 8   // 按钮 26 + padding 3*2 + 边 1*2
    implicitWidth: segRow.width + 8
    radius: root.rSm
    color: ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: ThemeBridge.colors["line.subtle"]

    Row {
        id: segRow
        x: 4      // 边 1 + padding 3
        y: 4
        spacing: 2

        Repeater {
            model: root.options
            delegate: Rectangle {
                id: segBtn
                required property var modelData
                readonly property bool on_: root.value === modelData.value
                width: segText.implicitWidth + (segBtn.on_ ? 6 + 4 + 6 : 0) + 26
                height: 26
                radius: ThemeBridge.radii.xs
                color: segBtn.on_ ? ThemeBridge.colors["bg.elevated"] : "transparent"
                Behavior on color {
                    ColorAnimation {
                        duration: ThemeBridge.reduceMotion ? 0 : ThemeBridge.durations.fast
                        easing.type: Easing.OutCubic
                    }
                }

                // .seg > button.on { box-shadow: var(--shadow-1) }
                layer.enabled: segBtn.on_ && ThemeBridge.layerEffectsAvailable
                               && !ThemeBridge.reduceMotion
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowBlur: 1.0
                    shadowScale: 1.0
                }

                Row {
                    anchors.centerIn: parent
                    spacing: segBtn.on_ ? 6 : 0   // ::after 的 margin-left 6
                    Text {
                        id: segText
                        text: segBtn.modelData.label
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12     // 12.5 → 12
                        font.weight: Font.DemiBold
                        color: segBtn.on_ ? ThemeBridge.colors["text.primary"]
                             : (segMouse.containsMouse ? ThemeBridge.colors["text.primary"]
                                                        : ThemeBridge.colors["text.muted"])
                        Behavior on color {
                            ColorAnimation {
                                duration: ThemeBridge.reduceMotion ? 0 : ThemeBridge.durations.fast
                            }
                        }
                    }
                    Rectangle {
                        visible: segBtn.on_
                        width: 4; height: 4
                        radius: 2
                        color: ThemeBridge.colors["accent.primary"]
                    }
                }

                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { root.picked(segBtn.modelData.value) }
                }
            }
        }
    }
}
