// src/ui/qml/GallerySpinner.qml —— 加载圈（ui.css:456-470 的 .spin / .spin.sm）
//
// 2px line-normal 边 + accent 顶边，0.7s 匀速一圈（base.css:147-149 spin）。
// QML 的 RotationAnimator 直接转 rotation，不需要按钮那样缓存背景再重绘。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // .spin.sm = 11px + 1.5px 边
    property bool small: false

    // 显式透明：Rectangle.color 默认是白色，不写就是一个白色方块
    color: "transparent"

    readonly property int box: small ? 11 : 14
    readonly property real ring: small ? 1.5 : 2

    implicitWidth: box
    implicitHeight: box

    // border-top-color 单独一层：四个边在 Rectangle 上没法分色，用一个
    // 只画顶边的窄条叠在完整边框上。
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: root.ring
        border.color: ThemeBridge.colors["line.normal"]
    }
    Rectangle {
        x: parent.width / 2 - width / 2
        y: -root.ring / 2
        width: root.box / 2
        height: root.ring
        color: ThemeBridge.colors["accent.primary"]
    }

    RotationAnimator on rotation {
        from: 0; to: 360
        duration: 700
        loops: Animation.Infinite
        running: !root.reduce
    }
}
