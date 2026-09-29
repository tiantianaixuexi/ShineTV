// src/ui/qml/GalleryProgress.qml —— 进度条（ui.css:429-454 的 .prog）
//
// h6 / r-pill / fill-muted 槽 + --grad-accent 条；`run` 态叠一条 1.4s
// 来回扫的高光（base.css:150-153 shimmer）。
//
// ⚠️ 两处 QML 表达力受限，按最接近的方式落地：
//   · --grad-accent 是 120deg 斜向渐变，QML 的 Gradient 只认水平/垂直，
//     这里取水平（accent.primary → accent.info），端点色与顺序完全一致。
//   · shimmer 是 100deg 的白色扫光，这里同样降级成水平扫光带。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property real value: 0            // 0–100
    property bool run: false
    property bool thin: false

    // 显式透明：Rectangle.color 默认是白色，不写整条进度槽就是白的
    color: "transparent"

    readonly property int barH: thin ? 4 : 6
    implicitHeight: barH
    implicitWidth: 160

    // shimmer：高光带从左扫到右再扫回左，1.4s 线性。
    // 动画打在 sweep 上而不是 sheen.x —— 直接对有绑定的属性做动画会把绑定打死。
    property real sweep: 0

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: ThemeBridge.colors["fill.muted"]
        clip: true

        Rectangle {
            width: track.width * Math.max(0, Math.min(100, root.value)) / 100
            height: track.height
            radius: height / 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: ThemeBridge.colors["accent.primary"] }
                GradientStop { position: 1.0; color: ThemeBridge.colors["accent.info"] }
            }
            Behavior on width {
                NumberAnimation { duration: root.reduce ? 0 : root.durSlow; easing.type: Easing.OutCubic }
            }
        }

        // shimmer：高光带
        Rectangle {
            id: sheen
            width: track.width
            height: track.height
            x: root.sweep * track.width
            visible: root.run && !root.reduce
            opacity: 0.35
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: ThemeBridge.colors["text.primary"] }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    SequentialAnimation {
        running: root.run && !root.reduce
        loops: Animation.Infinite
        NumberAnimation { target: root; property: "sweep"; from: -1; to: 1; duration: 700; easing.type: Easing.Linear }
        NumberAnimation { target: root; property: "sweep"; from: 1; to: -1; duration: 700; easing.type: Easing.Linear }
    }
}
