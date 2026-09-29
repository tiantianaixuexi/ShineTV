// src/ui/qml/Gallery/Button.qml —— 对照 webui ui.css:6-29 的 .btn
//
// 几何逐值照抄设计稿：h30 / p0 14 / r-sm(6) / f13 / w600；
// :active 的 scale(0.97) 在 Widgets 侧是自绘（QSS 无 transform，需缓存背景
// 再用 QStyle 缩放重绘），在 QML 侧是一行 scale —— 这正是 QML 的直接优势。
import QtQuick
import QtQuick.Effects  // MultiEffect（primary 的 accent 辉光）
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    property bool primary: true
    // ⚠️ **不要**再声明 `property bool enabled` —— Item 已经有内置的 enabled，
    // 重复声明会覆盖它并触发
    //   "Member enabled of the object Button_QMLTYPE_1 overrides a member of the base object"
    // 直接用内置的：它顺带把整棵子树（含 MouseArea）一起禁用，正是 .btn:disabled 想要的。

    implicitHeight: 30          // ui.css .btn height: 30px
    implicitWidth: label.implicitWidth + 28  // padding: 0 14px
    opacity: enabled ? 1.0 : 0.45             // .btn:disabled { opacity: 0.45 }

    // 底色直接画在自身（Ctl 已是 Rectangle，不再套一层）
    radius: root.rSm
    color: root.primary ? ThemeBridge.colors["accent.primary"]
                        : (mouse.containsMouse ? ThemeBridge.colors["fill.hover"]
                                               : ThemeBridge.colors["bg.elevated"])
    border.width: 1
    border.color: root.primary ? ThemeBridge.colors["accent.primary"] : ThemeBridge.colors["line.normal"]

    // :active { transform: scale(0.97) }
    scale: mouse.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    // primary 的 accent 辉光：Widgets 侧用 QGraphicsDropShadowEffect，
    // QML 侧用 layer.effect 原生阴影
    layer.enabled: root.shadows && root.primary
    layer.effect: MultiEffect {
        shadowEnabled: root.primary && !root.reduce
        shadowBlur: 1.0
        shadowScale: 1.0
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.family: ThemeBridge.fontFamily
        font.pixelSize: ThemeBridge.baseFontPx // 13 —— 与 QSS 同为整数档
        font.weight: Font.DemiBold       // 600
        color: root.primary ? ThemeBridge.colors["accent.primary.fg"]
                            : ThemeBridge.colors["text.primary"]
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
    }
}
