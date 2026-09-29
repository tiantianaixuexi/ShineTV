// src/ui/qml/StoryboardButton.qml —— ui.css:6-85 的 .btn / .btn.sm / 三种 variant
//
// 为什么不用冻结的 Button.qml：本页 9 个按钮里 7 个要 `sm`（h24 / p0 10 / f12）
// 或 `ghost`（透明底 + text.secondary 字），而 Button.qml 只暴露 primary/secondary；
// 且它没有 `onClicked`，本页每个按钮都有动作（跑阶段、开弹窗、落库回执……）。
// 几何与配色逐条照抄设计稿，底座仍继承 Ctl（几何 token 与「减少动效」共用一份真值）。
//
// .btn          { h30 p0 14 r-sm f13 w600 · :active scale(0.97) · :disabled opacity .45 }
// .btn.sm       { h24 p0 10 f12 · gap 4 }   .btn .icon 15  .btn.sm .icon 13
// .btn-primary  { accent 底 / accent-fg 字 / hover: accent-h + shadow-accent }
// .btn-secondary{ bg-elevated 底 / line-normal 边 / text-primary 字
//                 hover: fill-hover 底 + line-strong 边 }
// .btn-ghost    { 透明底 / 透明边 / text-secondary 字 / hover: fill-hover + text-primary }
//
// ⚠️ `.btn-primary` 那层白色 inset 高光**未画**：它的白色带 alpha 值不在任何 token 里，
// 硬编码会变成第二套颜色真值。冻结的 Button.qml 同样未画
// （docs/90-reference/ui-design-parity-gaps.md 第四节已把这条记为已知缺口）。
import QtQuick
import QtQuick.Effects        // MultiEffect（primary 的 accent 辉光）
import Shine 1.0

// MultiEffect 里要用外层 id（root / mouse），显式声明 Bound 才合法
pragma ComponentBehavior: Bound

Ctl {
    id: root

    property string text: ""
    // primary / secondary / ghost（设计稿的 variant）
    property string variant: "secondary"
    property bool sm: false
    property string glyph: ""        // 设计稿的 icon（clapper/wand/check/play…），无图标资源时用字形
    signal clicked()

    readonly property bool primary: variant === "primary"
    readonly property bool ghost: variant === "ghost"

    readonly property color fgColor: primary ? ThemeBridge.colors["accent.primary.fg"]
                                             : (ghost ? ThemeBridge.colors["text.secondary"]
                                                      : ThemeBridge.colors["text.primary"])

    implicitHeight: sm ? 24 : 30                     // .btn / .btn.sm height
    implicitWidth: row.implicitWidth + (sm ? 20 : 28)  // padding: 0 14px / 0 10px
    opacity: enabled ? 1.0 : 0.45                    // .btn:disabled { opacity: .45 }
    radius: rSm

    color: primary ? (mouse.containsMouse ? ThemeBridge.colors["accent.primary.hover"]
                                          : ThemeBridge.colors["accent.primary"])
         : ghost   ? (mouse.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent")
         :          (mouse.containsMouse ? ThemeBridge.colors["fill.hover"]
                                        : ThemeBridge.colors["bg.elevated"])
    border.width: 1
    border.color: primary ? ThemeBridge.colors["accent.primary"]
                : ghost   ? "transparent"
                : (mouse.containsMouse ? ThemeBridge.colors["line.strong"]
                                       : ThemeBridge.colors["line.normal"])

    scale: mouse.pressed ? 0.97 : 1.0                // .btn:active { transform: scale(.97) }
    Behavior on scale {
        NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on color {
        NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    // .btn-primary:hover { box-shadow: var(--shadow-accent) }
    // ⚠️ 必须走 ThemeBridge.layerEffectsAvailable：software 场景图后端下
    // layer.effect 会把**整个 item 吞掉**（不是阴影没画出来，是按钮不显示）。
    layer.enabled: root.shadows && root.primary && mouse.containsMouse
    layer.effect: MultiEffect {
        shadowEnabled: root.primary && mouse.containsMouse && !root.reduce
        shadowBlur: 1.0
        shadowScale: 1.0
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: root.sm ? 4 : 6                       // .btn.sm { gap: 4px } / .btn { gap: 6px }
        Text {
            visible: root.glyph !== ""
            text: root.glyph
            font.family: ThemeBridge.fontFamily
            font.pixelSize: root.sm ? 9 : 10            // 图标 13 / 15 盒内的字形
            color: root.fgColor
        }
        Text {
            text: root.text
            font.family: ThemeBridge.fontFamily
            font.pixelSize: root.sm ? 12 : ThemeBridge.baseFontPx  // 12 / 13
            font.weight: Font.DemiBold                  // 600
            color: root.fgColor
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
