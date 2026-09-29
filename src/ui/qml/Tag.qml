// src/ui/qml/Tag.qml —— 共享胶囊标签（ui.css:120-145 的 .tag / .tag.sm）
//
// ================================ 合并来源：三份手搓实现收成一份 ================================
//   旧冻结版 Tag.qml（重写前）: 只有默认档 h20 / p0 8 / f11，没有 sm、没有 dot
//   AssetsTag.qml            : sm 档 h17 / p0 6 / f10（该页设计稿里所有 Tag 都带 sm）
//   StoryboardTag.qml        : sm 档 + 前置 7px 状态点（UI.jsx:36），tone=busy 时脉冲
//   → 两处真实差异全部下沉成 `sm` / `dot` 两个 bool 档；底/边/字三条线的映射三份本来
//     就是同一份（同一套 ThemeBridge 调用），逐字照抄，不做任何"顺手优化"。
// =================================================================================================
//
// 设计稿（webui/src/styles/ui.css:120-145，另有 webui/src/components/UI.jsx:32-40）：
//   .tag     { display:inline-flex; align-items:center; gap:5px; height:20px; padding:0 8px;
//              border-radius:var(--r-pill); font-size:11.5px; font-weight:600;
//              border:1px solid var(--line-normal); color:var(--text-secondary);
//              background:var(--fill-muted); white-space:nowrap }                 ← ui.css:120-133
//   .tag.sm  { height:17px; padding:0 6px; font-size:10.5px }                    ← ui.css:134-138
//   .tag.ok / .warn / .danger / .busy / .info / .accent                          ← ui.css:139-144
//              color:        var(--tone)                     ← 满强度
//              border-color: color-mix(in srgb, var(--tone) 35%, transparent)
//              background:   color-mix(in srgb, var(--tone) 12%, transparent)
//   .tag.idle { color: var(--text-muted) }   ← ui.css:145：只改字色，底/边仍走 .tag 的中性档
//   UI.jsx:36 {dot && <i class={"dot" + (tone==='busy' ? ' run' : '')} style={{background:'currentColor'}} />}
//
// 几何按 CSS 的 border-box 盒模型推（1px 边画在盒内，不额外占位）：
//   宽 = [dot 7 + gap 5] + 文字 advance + 左右 padding（默认 8×2 / sm 6×2）
//   高 = 20 / 17 —— .tag 的 height 已经含 1px 边，不要再减 2
//   字号：11.5→11、10.5→10（半像素按仓库既有规则就近取整，与 QssBuilder 的 Tag 同档）
//
// 颜色为什么必须拆成三条线（底/边/字）：
//   底 = ThemeBridge.toneBg[tone]    = tone 与 bg.surface 的 12% color-mix
//   边 = ThemeBridge.toneEdge[tone]  = tone 与 bg.surface 的 35% color-mix
//   字 = ThemeBridge.colors[toneToken] = tone **满强度**
//   两条 mix 走 ToneMix.h，与 QssBuilder::FillToneMixes 是同一份算法，所以本组件与
//   Widgets 侧的 Tag 在同一主题下必然同色。**不要在这里自己重算混色** —— 那会立刻长出
//   第二套颜色真值，QML 与 QSS 从此不再同色。
//
// ⚠️ 底色 12%、**文字满强度**（设计稿写的是 `color: var(--tone)`，不是反白字）。
//    搞反的话胶囊变成一块纯色、文字与底同色 → 看起来"有胶囊但没字"。三份旧实现
//    都在文件头写了这一条，别再写错。
//
// ⚠️ tone → token 名不是简单的 "status." + tone：CSS 里 accent 是 --accent、info 是 --info，
//    本仓的 token 名却是 accent.primary / accent.info。直接拼会给 accent/info 取到
//    不存在的名字 → ThemeBridge.colors[...] 拿到 undefined → 颜色退化成黑。
//
// ⚠️ 状态点脉冲环用 accent-glow 30%（--accent-glow），**不是**当前 tone 色：
//    base.css:143-145 的 @keyframes pulse-dot 里写死 var(--accent-glow)，与被点亮的主色无关。
//    旧 StoryboardTag 用了 tone 色（= status.busy），是偏离设计稿的一处，已统一到设计稿。
//    QML 没有 box-shadow，用一圈 border.width 0→5px + 同步淡出近似（取舍理由见 Dot.qml 头注）。
//
// ⚠️ 属性名避开 Item 内建的 enabled / visible（重复声明会被 QML 静默吞掉赋值），
//    本文件所有对外属性一律小写开头。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    // accent / info / ok / warn / danger / busy / pending / idle；空串 = 默认态
    property string tone: ""
    property bool sm: false   // .tag.sm：h17 / p0 6 / f10
    property bool dot: false  // UI.jsx:36 的前置 7px 状态点；tone === "busy" 时脉冲

    // tone → token 点分名。⚠️ 见文件头：accent / info 不能拼 "status." + tone
    readonly property string toneToken: tone === "accent" ? "accent.primary"
                                            : tone === "info"  ? "accent.info"
                                                              : "status." + tone
    // .tag 默认态（中性底+边）与 .tag.idle（ui.css:145 只改字色）共用中性底/边
    readonly property bool plain: root.tone === "" || root.tone === "idle"

    readonly property color bgColor: root.plain ? ThemeBridge.colors["fill.muted"]
                                                : ThemeBridge.toneBg[root.tone]
    readonly property color fgColor: root.tone === ""  ? ThemeBridge.colors["text.secondary"]
                                    : root.tone === "idle" ? ThemeBridge.colors["text.muted"]
                                    : ThemeBridge.colors[root.toneToken]
    readonly property color edgeColor: root.plain ? ThemeBridge.colors["line.normal"]
                                                  : ThemeBridge.toneEdge[root.tone]

    // —— 尺寸档：只有设计稿存在的两档，多的别加 ——
    readonly property int boxH: root.sm ? 17 : 20          // .tag height
    readonly property int padX: root.sm ? 6 : 8            // padding: 0 8 / 0 6

    implicitHeight: boxH
    implicitWidth: row.implicitWidth + padX * 2
    radius: root.rPill                                    // --r-pill

    color: root.bgColor
    border.width: 1
    border.color: root.edgeColor

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5                                  // .tag { gap: 5px }（ui.css:123）

        Item {
            width: 7
            height: 7
            visible: root.dot

            // UI.jsx:36 style={{ background: 'currentColor' }} → currentColor = 字色
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.fgColor
            }

            // .dot.run（ui.css:170-172）→ base.css:143-145 的 pulse-dot 1.6s。
            // CSS 动的是 box-shadow 的 spread 0→5px，QML 没有 box-shadow，用一圈
            // border.width 0→5 + 同步淡出的同心环近似（理由见 Dot.qml 头注）。
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                visible: root.pulse
                border.width: root.spread
                border.color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)  // --accent-glow
                opacity: 1 - root.spread / 5
            }
        }

        Text {
            id: label
            text: root.text
            font.family: ThemeBridge.fontFamily
            font.pixelSize: root.sm ? 10 : 11   // 10.5 / 11.5 就近取整
            font.weight: Font.DemiBold         // font-weight: 600
            color: root.fgColor
        }
    }

    // pulse-dot 的 spread（px）。0→5 对应 base.css:144-145 的
    //   0%,100% { box-shadow: 0 0 0 0     var(--accent-glow) }
    //   50%     { box-shadow: 0 0 0 5px   transparent }
    // 1.6s 一轮 = 去 800 + 回 800。InOutQuad 近似 tokens.css:27 的 --ease
    // cubic-bezier(0.2, 0, 0, 1)。
    property real spread: 0
    // 「减少动效」下退化为静态实心点（语义仍是「运行中」），不是没有点
    readonly property bool pulse: root.dot && root.tone === "busy" && !root.reduce

    SequentialAnimation on spread {
        running: root.pulse
        loops: Animation.Infinite
        NumberAnimation { to: 5; duration: 800; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 0; duration: 800; easing.type: Easing.InOutQuad }
    }
}
