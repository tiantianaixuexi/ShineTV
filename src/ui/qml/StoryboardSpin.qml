// src/ui/qml/StoryboardSpin.qml —— ui.css:457-470 的 .spin
//
// .spin { 14px 圆 / 2px 边 / border-top-color: accent / spin .7s linear infinite }
// .spin.sm { 11px 圆 / 1.5px 边 }
//
// ⚠️ CSS 只有 border-top-color，QML 的 border.color 是整圈单色。做法是：
// 本体画整圈 line.normal 的环，再叠一个跟着容器旋转的 accent 短弧帽
// （容器的 SequentialAnimation 就是 @keyframes spin）。短弧帽是**近似**，
// 环与帽的接缝在 14px 下不可见。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property bool sm: false

    readonly property int box: sm ? 11 : 14
    readonly property real ring: sm ? 1.5 : 2   // .spin.sm 的 1.5px 边 → real

    width: box
    height: box
    radius: box / 2                  // .spin { border-radius: 50% }
    color: "transparent"
    border.width: ring
    border.color: ThemeBridge.colors["line.normal"]

    Item {
        width: root.box
        height: root.box
        // @keyframes spin { to { transform: rotate(360deg) } }
        SequentialAnimation on rotation {
            running: !ThemeBridge.reduceMotion     // 「减少动效」退化为静止环
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 360; duration: 700; easing.type: Easing.Linear }
        }
        Rectangle {
            // 顶部的 accent 弧帽：宽度取弦长上限（0.8×直径），避免超出圆周
            width: root.box * 0.8
            height: root.ring
            x: (root.box - width) / 2
            y: 0
            radius: height / 2
            color: ThemeBridge.colors["accent.primary"]
        }
    }
}
