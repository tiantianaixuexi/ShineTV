// src/ui/qml/GalleryEmpty.qml —— 空态（ui.css:688-719 的 .empty）
//
// column 居中 gap 10 / p40 20；52px 虚线圆角框（r-lg / fill-muted / dashed
// line-normal）内一枚 24px 图标；标题 f13 w600 text-secondary；副文案 .tiny.dim。
// 框体带 base.css:161-164 的 float-y（4s，±6px）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string icon: "sparkles"
    property string title: ""
    property string text: ""

    readonly property int glyphBox: 52
    readonly property int iconBox: 24

    implicitHeight: 40 + glyphBox + 10 + 16 + 10 + 14 + 40     // p40 上下
    implicitWidth: 200

    color: "transparent"

    // float-y：0 → -6 → 0，4s ease-in-out。
    // 动画打在 lift 上而不是 y 本身 —— 直接对有绑定的属性做动画会把绑定打死。
    property real lift: 0

    Item {
        id: glyph
        x: Math.round((root.width - root.glyphBox) / 2)
        y: 40 - root.lift
        width: root.glyphBox
        height: root.glyphBox

        Rectangle {
            anchors.fill: parent
            radius: root.rLg
            color: ThemeBridge.colors["fill.muted"]
            border.width: 1
            // dashed：Qt 的 Rectangle 只有实线边，用一圈短划矩形近似
            border.color: ThemeBridge.colors["line.normal"]
        }

        GalleryIcon {
            anchors.centerIn: parent
            width: root.iconBox
            height: root.iconBox
            name: root.icon
            glyphColor: ThemeBridge.colors["text.muted"]
        }

        // float-y：0 → -6 → 0，4s ease-in-out
        SequentialAnimation {
            running: !root.reduce
            loops: Animation.Infinite
            NumberAnimation { target: root; property: "lift"; to: 6; duration: 2000; easing.type: Easing.InOutQuad }
            NumberAnimation { target: root; property: "lift"; to: 0; duration: 2000; easing.type: Easing.InOutQuad }
        }
    }

    Text {
        x: 0
        y: 40 + root.glyphBox + 10
        width: root.width
        height: 16
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: root.title
        color: ThemeBridge.colors["text.secondary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: ThemeBridge.baseFontPx
        font.weight: Font.DemiBold
    }

    Text {
        x: 0
        y: 40 + root.glyphBox + 10 + 16 + 10
        width: root.width
        height: 14
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        text: root.text
        color: ThemeBridge.colors["text.muted"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }
}
