// src/ui/qml/ImageFlowBtn.qml —— 对照 webui ui.css:6-49 / 51-76 的
// .btn、.btn.sm、.btn-primary / -secondary / -ghost
//
// 为什么不直接用冻结的 Button.qml：本页设计稿（ImageFlow.jsx）的按钮**全是
// size="sm"**（工具栏 4 个 + 面板 5 个），而冻结的 Button 只有 md 档
// （h30 / p0 14 / f13）且没有 icon 位。冻结层不动，sm + icon 档在本页私有件里
// 按 CSS 逐值复刻；色值仍全部取自同一批 token，所以与 QSS / Button.qml 必然同色。
//
// ⚠️ 字符图标：设计稿是内联 SVG，Qt 侧按 kit 的既有约定（P03 不引外部图标资源，
// 见 pages/imageflow/ImageFlowWorkspace.cpp「与 kit 字符图标约定一致」）用字符，
// 字号取 CSS 的 icon 盒尺寸（.btn .icon 15px / .btn.sm .icon 13px）。
import QtQuick
import QtQuick.Effects  // MultiEffect（.btn-primary:hover 的 accent 辉光）
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    property string variant: "secondary"    // primary / secondary / ghost
    property bool sm: false
    property string icon: ""
    signal clicked()

    readonly property int padH: sm ? 10 : 14        // .btn.sm p0 10 / .btn p0 14
    readonly property int iconBox: sm ? 13 : 15    // .btn.sm .icon 13 / .btn .icon 15
    readonly property int iconGap: sm ? 4 : 6      // .btn.sm gap 4 / .btn gap 6

    implicitHeight: root.sm ? 24 : 30              // .btn.sm 24 / .btn 30
    implicitWidth: root.padH * 2 + (root.icon !== "" ? root.iconBox + root.iconGap : 0)
                   + label.implicitWidth
    radius: root.rSm                              // --r-sm
    opacity: enabled ? 1.0 : 0.45                 // .btn:disabled { opacity: .45 }

    // .btn-primary 底 = --accent；secondary 底 = --bg-elevated；ghost 透明。
    // hover 统一走 --fill-hover（.btn-secondary:hover / .btn-ghost:hover）。
    color: root.variant === "primary" ? ThemeBridge.colors["accent.primary"]
         : (mouse.containsMouse ? ThemeBridge.colors["fill.hover"]
         : (root.variant === "ghost" ? "transparent" : ThemeBridge.colors["bg.elevated"]))
    border.width: 1
    border.color: root.variant === "primary" ? ThemeBridge.colors["accent.primary"]
                 : root.variant === "ghost" ? "transparent"
                 : (mouse.containsMouse ? ThemeBridge.colors["line.strong"]
                                         : ThemeBridge.colors["line.normal"])
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    // .btn:active { transform: scale(0.97) } —— CSS 默认 transform-origin 是中心，
    // QML 的 scale 也是绕中心，所以直接挂在自身即可（不涉及 anchors 抢 y）。
    scale: mouse.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    // .btn-primary:hover { box-shadow: var(--shadow-accent) }。
    // ⚠️ 基态那层 1px 内高光（inset 白 12%）在 QML 无法表达（MultiEffect 只做
    // 外阴影），只落 hover 段。
    layer.enabled: ThemeBridge.layerEffectsAvailable && root.variant === "primary"
                   && mouse.containsMouse && !root.reduce
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowBlur: 1.0
        shadowScale: 1.0
        shadowColor: ThemeBridge.colors["shadow.accent"]
    }

    Row {
        anchors.centerIn: parent
        spacing: root.iconGap
        Text {
            visible: root.icon !== ""
            width: root.icon !== "" ? root.iconBox : 0
            text: root.icon
            font.family: ThemeBridge.fontFamily
            font.pixelSize: root.iconBox
            color: root.variant === "primary" ? ThemeBridge.colors["accent.primary.fg"]
                                              : ThemeBridge.colors["text.secondary"]
        }
        Text {
            id: label
            text: root.text
            font.family: ThemeBridge.fontFamily
            font.pixelSize: root.sm ? 12 : ThemeBridge.baseFontPx   // .btn.sm 12 / .btn 13
            font.weight: Font.DemiBold                            // font-weight: 600
            color: root.variant === "primary" ? ThemeBridge.colors["accent.primary.fg"]
                 : (root.variant === "ghost" && mouse.containsMouse)
                   ? ThemeBridge.colors["text.primary"]           // .btn-ghost:hover
                 : ThemeBridge.colors["text.primary"]            // secondary 本色即 text-primary
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { root.clicked() }
    }
}
