// src/ui/qml/Field.qml —— 表单项（UI.jsx:103-111 的 Field）
//
// column 布局 gap 6；label f12 w600 text-secondary；help 走 .tiny.dim（f12 muted）。
// 高度按盒子模型算死：label 14 + gap 6 + 控件高。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string label: ""
    property string help: ""
    property int controlH: 30

    default property alias content: slot.data

    readonly property int labelH: 14          // 12px 行高取整
    readonly property int helpH: root.help !== "" ? 14 + 6 : 0

    implicitHeight: (root.label !== "" ? labelH + 6 : 0) + root.controlH + root.helpH
    implicitWidth: 160

    color: "transparent"

    Text {
        id: lbl
        visible: root.label !== ""
        x: 0; y: 0
        height: root.labelH
        verticalAlignment: Text.AlignVCenter
        text: root.label
        color: ThemeBridge.colors["text.secondary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    Item {
        id: slot
        x: 0
        y: root.label !== "" ? root.labelH + 6 : 0
        width: parent.width
        height: root.controlH
    }

    Text {
        visible: root.help !== ""
        x: 0
        y: slot.y + root.controlH + 6
        height: 14
        verticalAlignment: Text.AlignVCenter
        text: root.help
        color: ThemeBridge.colors["text.muted"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }
}
