// src/ui/qml/StoryboardChip.qml —— views.css:670-699 的 .chip（本页只用于连续性 C1–C12）
//
// .chip     { h26 p0 11 r-pill · line-normal 边 · text-secondary 字 · f12 w600 }
// .chip:hover { line-strong 边 + text-primary 字 }
// 连续性卡（Storyboard.jsx:131）inline 覆盖：height 24、color var(--ok)、
//   borderColor color-mix(in srgb, var(--ok) 30%, transparent)，**没有**底色。
//
// ⚠️ 30% 混色边在桥上没有对应档：ThemeBridge.toneEdge 是 35%（且是叠在 bg.surface
// 上的不透明值，不是 alpha），这里取 toneEdge["ok"]，与冻结的 Tag.qml 同一档。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string label: ""
    property bool showCheck: true

    height: 24                             // Storyboard.jsx:131 的 inline 覆盖
    radius: rPill
    color: "transparent"                   // .chip 本身无底色
    border.width: 1
    border.color: ThemeBridge.toneEdge["ok"]
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
    }

    Row {
        anchors.centerIn: parent
        spacing: 6                          // .chip { gap: 6px }
        Text {
            visible: root.showCheck
            text: "✓"                       // Icon name="check" 11×11
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 10
            color: ThemeBridge.colors["status.ok"]
        }
        Text {
            text: root.label
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: ThemeBridge.colors["status.ok"]   // inline color: var(--ok)
        }
    }
}
