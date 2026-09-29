// src/ui/qml/Chip.qml —— 共享胶囊：views.css:675-706 的 .chip / .chip.on / .chip .cnt
//
// ================================ 合并来源（2 份 → 1 份） ================================
//   · AssetsChip.qml     .chip 本体 + `.chip.on`（active）+ `.chip .cnt`（count）+ glyph 位
//                        + accent-dim / accent-glow 的 Qt.alpha 派生
//   · StoryboardChip.qml 勾选标记（checkable）+ ok 色调的边/字 + h24 那一档
// 差异 → 属性：StoryboardChip 的 ok 配色与 h24 **不是**另两个开关，而是「连续性胶囊」这种
// 胶囊的固有形态，所以合成一个 `checkable` 布尔；它原本的 `label` 属性改叫 `text`
// （与 Button 统一），`showCheck` 被 `checkable` 吸收，`height: 24` 变成
// `checkable ? 24 : 26`。其余行为（.cnt 徽标、.on 底/边/字、hover 边/字）两边同构，直接保留。
// ======================================================================================
//
// 设计稿（webui/src/styles/views.css，行号即真值）：
//   .chip          h26 / p0 11 / gap 6 / r-pill / border 1px --line-normal /
//                  字 --text-secondary / f12 / w600 / transition: all var(--dur-1)   :675-687
//   .chip:hover    边 --line-strong / 字 --text-primary                               :688-691
//   .chip.on       底 --accent-dim / 边 --accent-glow / 字 --accent                   :692-696
//   .chip .cnt     f10.5 / p0 6 / r-pill / 底 --fill-muted / 字 --text-muted         :697-703
//   .chip.on .cnt  底 = color-mix(--accent 18%, transparent)（字色不变）                :704-706
//   勾选形态：Storyboard.jsx:131-132 的 inline 覆盖 —— height 24、color var(--ok)、
//            borderColor color-mix(--ok 30%, transparent)、**无底色**，前导 Icon 11 × 11。
//
// 几何推导（`.chip` 的 h26 / p0 11 / gap 6 → implicitWidth）：
//   左右 padding 恒定 → 宽 = 11*2 + 排版；gap 同 .btn 的处理，只在相邻两项都非空时计入。
//   `.cnt` 的宽度 = 文字宽 + p0 6px 两侧（views.css:699），所以 +12。
//   `.cnt` 的**高度**设计稿没写（span 吃父级的 line-height），base.css:16 是 1.6，
//   按本仓取整后的 11px 字算 11 × 1.6 = 17.6 → 18。
//   ⚠️ `.cnt` 的字号设计稿是 10.5px，font.pixelSize 是 **int**（QFont::setPixelSize），
//   写 10.5 运行时报 "int expected"；按本仓就近取整规则 10.5 → 11，与 QSS 已落地的同元素值
//   （QssBuilder.cpp 的 `.chip .cnt` f10.5 → 11px）同像素，QML 与 QSS 一致。
//
// ⚠️ accent-dim / accent-glow 在 ThemeBridge 里**没有对应 token**（colors 表只有实色与
//   line.* / shadow.* 两组透明色），所以按 tokens.css 注释里的 alpha 从 accent.primary 派生
//   （tokens.css:65-66：glow 0.3 / dim 0.14），**不是硬编码色值**。⚠️ 不同主题的 alpha
//   不同（薄暮 0.3/0.14、水墨 0.22/0.1、纸墨 0.2/0.08），这里取默认主题（深空）那一档 ——
//   切主题时 `.chip.on` 的浓淡会与设计稿有差，属已知缺口，要出图确认。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 对外 API ——
    property string text: ""
    // 前导字符图标（设计稿是内联 SVG，本仓不引外部图标资源，kit 约定用字符）。
    // ⚠️ `checkable: true` 时这个槽位被 ✓ 占用 —— 设计稿里勾选标记就是 chip 的前导 Icon
    // （Storyboard.jsx:132），位置和间距与普通 glyph 完全一样（gap 6），所以不另开排版分支。
    property string glyph: ""
    property int count: -1                   // < 0 = 不画 .cnt 徽标
    property bool active: false              // .chip.on
    property bool checkable: false           // 勾选形态（连续性胶囊那一种）
    signal picked()

    // —— 派生色 ——
    readonly property color accent: ThemeBridge.colors["accent.primary"]
    readonly property color dimBg: Qt.alpha(root.accent, 0.14)     // --accent-dim
    readonly property color glowEdge: Qt.alpha(root.accent, 0.3)   // --accent-glow
    readonly property color cntOnBg: Qt.alpha(root.accent, 0.18)   // .chip.on .cnt

    readonly property string leadGlyph: root.checkable ? "✓" : root.glyph
    // 11px 两处同值：`.chip` 里的普通 Icon 11px，连续性胶囊的 check 也是 11 × 11
    // （Storyboard.jsx:132）。旧 StoryboardChip.qml:35 写的是 10，那是「字形在盒内」的
    // 二次缩放、没有设计稿依据，按盒尺寸改回 11。
    readonly property int glyphPx: 11

    // ⚠️ 内容**显式定位**，不套 Row：定位器对不可见子项是否留 spacing 不确定（旧
    // AssetsChip.qml:39-40 的原话），而设计稿的 `padding: 0 11px` + `gap: 6px` 是固定值，
    // `.cnt` 也只在 count >= 0 时占位。
    readonly property real padH: 11
    readonly property real gap: 6
    readonly property real glyphW: root.leadGlyph === "" ? 0 : glyphText.implicitWidth
    readonly property real labelW: root.text === "" ? 0 : labelText.implicitWidth
    readonly property real cntW: root.count < 0 ? 0 : cntText.implicitWidth + 12   // .cnt p0 6

    // h24 来自 Storyboard.jsx:131 的 inline 覆盖（连续性胶囊那一行），通用 .chip 是 26。
    // ⚠️ 冻结的 API 没有高度槽位，所以这一档挂在 checkable 上；将来若要独立的
    // 「小号胶囊」，应加属性而不是再加一份文件。
    implicitHeight: root.checkable ? 24 : 26
    implicitWidth: root.padH * 2 + root.glyphW + root.labelW + root.cntW
                   + (root.glyphW > 0 ? root.gap : 0) + (root.cntW > 0 ? root.gap : 0)

    radius: root.rPill                     // --r-pill（views.css:681）
    // ⚠️ Ctl 是 Rectangle，color 默认**白色**；下面两个分支都显式给了值。
    // .chip 本身**无底色**（只有 .chip.on 才有底），Storyboard.jsx:131 的 inline 覆盖也没加底。
    color: root.active ? root.dimBg : "transparent"
    border.width: 1
    // .chip 的边是 `1px solid var(--line-normal)`（views.css:682）—— 注意**不是** transparent，
    // 与 .btn 的默认边框不同。
    border.color: root.active   ? root.glowEdge                        // .chip.on（views.css:694）
                  // ⚠️ 30% 透明混色在桥上没有对应档：ThemeBridge.toneEdge 是 35%，而且它
                  // 混的是 **bg.surface 的不透明值**（不是 alpha），比设计稿的边略重一档。
                  // 这里取 toneEdge["ok"]，与冻结的 Tag.qml 同一档 —— 宁可与 Tag 齐，
                  // 也不要在这里单开第二套混色算法（旧 StoryboardChip.qml:8-9 记的是同一条）。
                  : root.checkable ? ThemeBridge.toneEdge["ok"]
                  : (area.containsMouse ? ThemeBridge.colors["line.strong"]   // :hover :689
                                             : ThemeBridge.colors["line.normal"])   // :682

    // 字色：`.chip.on { color: var(--accent) }`（:695）、`:hover { color: var(--text-primary) }`（:690）、
    // 勾选形态是 inline 的 `var(--ok)`（Storyboard.jsx:131）。三档合成一条，图标与文字共用。
    readonly property color fgColor: root.active   ? root.accent
                                    : root.checkable ? ThemeBridge.colors["status.ok"]
                                    : (area.containsMouse ? ThemeBridge.colors["text.primary"]
                                                               : ThemeBridge.colors["text.secondary"])
    // ⚠️ `.chip` 写的是 `transition: all var(--dur-1)`（views.css:686），字色与徽标底色理论上
    // 也在内；但两份旧实现都只给胶囊底色与边框加了 Behavior，这里保持一致（保持瞬时）。
    // 真要补齐得先出图看是不是能察觉 —— 见交付说明里的「需要出图确认」。
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    Text {
        id: glyphText
        x: root.padH
        y: (root.height - height) / 2
        visible: root.leadGlyph !== ""
        text: root.leadGlyph
        color: root.fgColor
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.glyphPx        // 11
    }
    Text {
        id: labelText
        x: root.padH + root.glyphW + (root.glyphW > 0 ? root.gap : 0)
        y: (root.height - height) / 2
        text: root.text
        color: root.fgColor
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12                  // .chip font-size: 12px（views.css:684）
        font.weight: Font.DemiBold          // font-weight: 600（:685）
    }

    // .chip .cnt（views.css:697-703）—— count < 0 时整块不画。
    Rectangle {
        id: cntPill
        visible: root.count >= 0
        x: labelText.x + labelText.implicitWidth + (root.cntW > 0 ? root.gap : 0)
        y: (root.height - height) / 2
        width: root.cntW                    // padding: 0 6px → 文字宽 + 12
        height: 18                          // line-height 1.6 × 11 ≈ 17.6 → 18
        radius: root.rPill
        color: root.active ? root.cntOnBg                        // .chip.on .cnt（:704-706）
                           : ThemeBridge.colors["fill.muted"]    // .chip .cnt（:701）
        Text {
            id: cntText
            anchors.centerIn: parent
            text: "" + root.count
            color: ThemeBridge.colors["text.muted"]   // :702，⚠️ .chip.on 不改徽标字色（:704-706）
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 11               // f10.5 → 11（int，见文件头说明）
        }
    }

    // ⚠️ 可交互组件必须**发信号**（kit 契约第 3 条）。旧 StoryboardChip.qml 整份**没有**
    // MouseArea —— 那是它的调用点（Storyboard.qml:633-644）自己在外面套的点击区。
    // 合并后点击区收回到组件里，调用点可以改成 `onPicked:`。
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.picked()
    }
}
