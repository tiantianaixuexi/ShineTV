// src/ui/qml/ImageFlowProg.qml —— 对照 webui ui.css:429-454 的 .prog / .prog.thin
// （.prog > i 底色是 --grad-accent，.prog.run 叠 base.css:150-153 的 shimmer）
//
// ⚠️ 两处降级，都只影响观感不影响结构：
// 1. `linear-gradient(120deg, ...)` 在 QML 只能取 6 个正交朝向，这里取 Horizontal
//    （120° 的轴向偏角只有 30°，在 4px 高的条上与水平渐变肉眼无差）。
// 2. shimmer 的 `background-size: 200%` + background-position 位移，等价成一条
//    等宽的「透明 → 亮 → 透明」横带在进度条内横扫。
//    高光色用 text.primary 35%（CSS 里是纯白 35%，白在深色底上
//    与 text.primary 等效，且这样切主题不会留一块纯白）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property int value: 0
    property bool run: false
    property bool thin: false

    implicitWidth: 90      // .q-prog width 90（只作默认，实际由 .grow 撑开）
    height: thin ? 4 : 6  // .prog.thin 4 / .prog 6
    radius: height / 2    // --r-pill
    color: ThemeBridge.colors["fill.muted"]
    clip: true

    Rectangle {
        id: fill
        x: 0
        height: parent.height
        width: parent.width * Math.max(0, Math.min(100, root.value)) / 100
        radius: parent.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: ThemeBridge.colors["accent.primary"] }
            GradientStop { position: 1.0; color: ThemeBridge.colors["accent.info"] }
        }
        Behavior on width {
            NumberAnimation { duration: root.reduce ? 0 : ThemeBridge.durations.slow; easing.type: Easing.OutCubic }
        }

        // .prog.run > i::after —— shimmer
        Rectangle {
            id: shimmer
            anchors.fill: parent
            visible: root.run
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.alpha(ThemeBridge.colors["text.primary"], 0.35) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            // x 初始 0；动画只在自己 running 时接管 x（减少动效下即静止）
            SequentialAnimation on x {
                running: root.run && !root.reduce
                loops: Animation.Infinite
                NumberAnimation { from: -shimmer.width; to: shimmer.width; duration: 1400; easing.type: Easing.Linear }
            }
        }
    }
}
