pragma ComponentBehavior: Bound
// src/ui/qml/AssetsChip.qml —— 页面私有：.chip + .cnt（资产类型筛选胶囊 / 时间线镜头胶囊）
//
// 对照 webui/src/styles/views.css:670-706：
//   .chip            h26 / p0 11 / r-pill / line-normal 边 / text-secondary / f12 w600 / gap 6
//   .chip:hover      边 line-strong + 字 text-primary
//   .chip.on         底 accent-dim / 边 accent-glow / 字 accent
//   .chip .cnt       f10.5 / p0 6 / r-pill / 底 fill-muted / 字 text-muted
//   .chip.on .cnt    底 = accent 18% 透明混色
//   transition: all var(--dur-1)
//
// ⚠️ 字号：font.pixelSize 是 int（QFont::setPixelSize），本仓一律取整（规则见
// docs/90-reference/ui-design-parity-gaps.md 第〇节）。设计稿原值 10.5 → 11，
// 与 QssBuilder.cpp:729 的 `.chip .cnt`（f10.5 → 11px）同值，QML 与 QSS 同像素。
//
// ✅ 审计缺口：Widgets 侧 src/ui/pages/assets/AssetWorkspace.cpp:323 的注释声称
// 类型筛选 chip「h26 / r-pill / f12 w600 + 尾部 .cnt 计数」，实现里只传了空的计数
// （`new widgets::Chip(filterLabels[i], "", header)`），徽标根本没画出来。
// 这里按 views.css 的取值把 .cnt 补齐 —— 数量是真实计数（总数 / 该 kind 的实体数）。
//
// ⚠️ accent-dim(0.14) / accent-glow(0.3) 在 ThemeBridge 里没有对应 token
// （colors 表只有实色与 --line-* / --shadow-* 两组透明色），所以按 tokens.css
// 注释里的 alpha 用 Qt.alpha() 从 accent.primary 派生，**不是硬编码色值**。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    property string glyph: ""
    property int count: -1          // < 0 = 不画 .cnt
    property bool active: false
    signal picked()

    readonly property color accent: ThemeBridge.colors["accent.primary"]
    readonly property color dimBg: Qt.alpha(root.accent, 0.14)    // --accent-dim
    readonly property color glowEdge: Qt.alpha(root.accent, 0.3)  // --accent-glow
    // ⚠️ 内容**显式定位**，不套 Row：定位器对不可见子项是否留 spacing 不确定，
    // 而设计稿的 `padding: 0 11px` + `gap: 6px` 是固定值（.cnt 只在有计数时占位）。
    readonly property real padH: 11
    readonly property real gap: 6
    readonly property real glyphW: glyph === "" ? 0 : glyphText.implicitWidth
    readonly property real cntW: count < 0 ? 0 : cntText.implicitWidth + 12

    implicitHeight: 26             // .chip height: 26px
    implicitWidth: padH * 2 + glyphW + labelText.implicitWidth + cntW
                    + (glyphW > 0 ? gap : 0) + (cntW > 0 ? gap : 0)
    radius: root.rPill
    antialiasing: true

    color: root.active ? root.dimBg : "transparent"
    border.width: 1
    border.color: root.active ? root.glowEdge
                    : (area.containsMouse ? ThemeBridge.colors["line.strong"]
                                          : ThemeBridge.colors["line.normal"])
    Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }
    Behavior on border.color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }

    Text {
        id: glyphText
        x: root.padH
        y: (root.height - height) / 2
        visible: root.glyph !== ""
        text: root.glyph
        color: root.active ? root.accent
                           : (area.containsMouse ? ThemeBridge.colors["text.primary"]
                                                 : ThemeBridge.colors["text.secondary"])
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 11          // tl-below 里的 .chip 内 Icon 11px
    }
    Text {
        id: labelText
        x: root.padH + root.glyphW + (root.glyphW > 0 ? root.gap : 0)
        y: (root.height - height) / 2
        text: root.text
        color: root.active ? root.accent
                           : (area.containsMouse ? ThemeBridge.colors["text.primary"]
                                                 : ThemeBridge.colors["text.secondary"])
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12          // .chip font-size: 12px
        font.weight: Font.DemiBold
    }

    // .cnt —— 行高 1.6 × 11 ≈ 18（设计稿没写 height，按继承的 line-height 算）
    Rectangle {
        id: cntPill
        visible: root.count >= 0
        x: labelText.x + labelText.implicitWidth + (root.cntW > 0 ? root.gap : 0)
        y: (root.height - height) / 2
        width: root.cntW                 // padding: 0 6px
        height: 18
        radius: root.rPill
        color: root.active ? Qt.alpha(root.accent, 0.18)   // .chip.on .cnt
                           : ThemeBridge.colors["fill.muted"]
        Text {
            id: cntText
            anchors.centerIn: parent
            text: "" + root.count
            color: ThemeBridge.colors["text.muted"]   // .cnt 选中态不改字色
            font.family: ThemeBridge.fontFamily
            // ⚠️ font.pixelSize 是 **int**（QFont::setPixelSize），写 10.5 运行时报
            // "Invalid property assignment: int expected"。按本仓取整规则取
            // QSS 已落地的同元素值（QssBuilder.cpp:729 `.chip .cnt` f10.5 → 11px）。
            font.pixelSize: 11
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.picked()
    }
}
