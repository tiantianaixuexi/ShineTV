pragma ComponentBehavior: Bound
// src/ui/qml/AssetsBtn.qml —— 页面私有：.btn（三个尺寸/变体组合都在本页用到）
//
// 对照 webui/src/styles/ui.css:6-85：
//   .btn            h30 / p0 14 / r-sm 6 / f13 / w600 / gap 6
//   .btn.sm         h24 / p0 10 / f12 / gap 4      ← 本页的 5 个头部按钮 + 行内按钮全是 sm
//   .btn .icon      15px（sm 13px）
//   :active         scale(0.97)
//   :disabled       opacity 0.45
//   .btn-primary    accent 底 + accent-fg 字；hover → accent-h + shadow-accent
//   .btn-secondary  bg-elevated 底 + line-normal 边；hover → fill-hover + line-strong
//   .btn-ghost      透明底 + 透明边；hover → fill-hover + text-primary
//
// ⚠️ 冻结的 Button.qml 只暴露 `text` + `primary`（固定 h30 / f13 / 无图标 / 无 ghost），
// 而本页设计稿用的是 `.btn.sm` 与 ghost，所以 sm + ghost 在这里复刻。
// 几何与配色仍逐值抄 ui.css，不新增任何 token。
import QtQuick
import QtQuick.Effects  // MultiEffect（primary 的 accent 辉光）
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    property string glyph: ""              // 设计稿用 <Icon>，QML 侧用等宽字符位图占位
    property string variant: "secondary"   // primary | secondary | ghost
    property bool sm: false
    signal picked()

    readonly property bool primaryVariant: variant === "primary"
    readonly property bool ghostVariant: variant === "ghost"
    readonly property int padH: sm ? 10 : 14

    // ⚠️ 图标与文字**显式定位**，不套 Row：Row 的 spacing 会对**不可见**子项也留一份
    // 间距（只看 visible 的话，纯图标按钮会凭空多出 6px 宽），而设计稿的
    // `padding: 0 14px` + `gap` 是固定值。
    readonly property real gap: sm ? 4 : 6
    readonly property real glyphW: glyph === "" ? 0 : glyphText.implicitWidth
    readonly property real labelW: text === "" ? 0 : labelText.implicitWidth

    implicitHeight: sm ? 24 : 30
    implicitWidth: padH * 2 + glyphW + labelW + (glyphW > 0 && labelW > 0 ? gap : 0)
    radius: root.rSm                       // --r-sm 6
    opacity: enabled ? 1.0 : 0.45          // .btn:disabled { opacity: 0.45 }
    antialiasing: true

    Text {
        id: glyphText
        x: root.padH
        y: (root.height - height) / 2
        visible: root.glyph !== ""
        text: root.glyph
        color: root.primaryVariant ? ThemeBridge.colors["accent.primary.fg"]
                                   : ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.sm ? 13 : 15   // .btn.sm .icon 13px / .btn .icon 15px
    }
    Text {
        id: labelText
        x: root.padH + root.glyphW + (root.labelW > 0 ? root.gap : 0)
        y: (root.height - height) / 2
        visible: root.text !== ""
        text: root.text
        color: root.primaryVariant ? ThemeBridge.colors["accent.primary.fg"]
              : root.ghostVariant ? (area.containsMouse ? ThemeBridge.colors["text.primary"]
                                                        : ThemeBridge.colors["text.secondary"])
                                  : ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.sm ? 12 : ThemeBridge.baseFontPx  // 12 / 13
        font.weight: Font.DemiBold
    }

    color: {
        if (root.primaryVariant)
            return area.containsMouse ? ThemeBridge.colors["accent.primary.hover"]
                                      : ThemeBridge.colors["accent.primary"]
        if (root.ghostVariant)
            return area.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent"
        return area.containsMouse ? ThemeBridge.colors["fill.hover"]
                                  : ThemeBridge.colors["bg.elevated"]
    }
    border.width: 1
    border.color: {
        if (root.primaryVariant)
            return ThemeBridge.colors["accent.primary"]
        if (root.ghostVariant)
            return "transparent"
        return area.containsMouse ? ThemeBridge.colors["line.strong"]
                                  : ThemeBridge.colors["line.normal"]
    }
    Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }
    Behavior on border.color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }

    // :active { transform: scale(0.97) }（reduce 下不位移）
    scale: area.pressed && !root.reduce ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: root.reduce ? 0 : root.durFast } }

    // primary 的 shadow-accent
    layer.enabled: root.shadows && root.primaryVariant
    layer.effect: MultiEffect {
        shadowEnabled: root.primaryVariant && !root.reduce
        shadowBlur: 1.0
        shadowScale: 1.0
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.picked()
    }
}
