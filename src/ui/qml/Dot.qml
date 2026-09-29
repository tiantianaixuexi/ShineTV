// src/ui/qml/Dot.qml —— 共享状态点（ui.css:164-172 的 .dot / .dot.run）
//
// ================================ 合并来源：两份手搓实现收成一份 ================================
//   GalleryDot.qml   : tone: string（语义档，主路径）—— 7×7 + border.width 0→5 近似 pulse
//   ImageFlowDot.qml : tone: color（直接给色，逃生舱）—— 7×7 + scale 1→1.53 近似 pulse
//   → 差异只有一处「tone 怎么给」：统一成语义档主路径（tone: string），保留
//     `toneColor` 作为「调用方整体覆盖颜色」的逃生舱；两个近似方案取 border.width 那份。
// =================================================================================================
//
// 设计稿（webui/src/styles/ui.css:164-172 + webui/src/styles/base.css:143-145）：
//   .dot      { width:7px; height:7px; border-radius:50%; flex:none }        ← ui.css:164-169
//   .dot.run  { animation: pulse-dot 1.6s var(--ease) infinite }            ← ui.css:170-172
//   @keyframes pulse-dot {                                                  ← base.css:143-145
//     0%, 100% { box-shadow: 0 0 0 0   var(--accent-glow) }
//     50%      { box-shadow: 0 0 0 5px transparent } }
//   @keyframes 的三个数值与 CSS 变量一一对应：spread 0 → 5px、1.6s 周期（去 800 回 800）、
//   环色 = --accent-glow（tokens.css:65/106/147/188/241，恒为 accent 色 30% alpha）。
//
// 几何：7×7 正圆（radius = width/2），满强度 tone 色 —— .dot 本身不带背景色，
//      颜色由调用方给的 currentColor 决定（UI.jsx:36 就是 style={{background:'currentColor'}}）。
//
// ⚠️ 近似选型（两份旧实现给了两种，挑了 border.width 那份）：
//    CSS 动的是 box-shadow 的 **spread**（0 → 5px），本质是「一圈同色阴影从 0 半径
//    扩散到 5px 并淡出」。QML 没有 box-shadow：
//      A. border.width 0→5 + opacity 1→0   ← **采用**。spread 与 CSS 的 spread 是同一个
//         数值（0/2.5/5 直接对得上 0%/50%/100%），复核时逐帧可对。
//      B. scale 1→1.53 + opacity 1→0        ← 换掉。1.53 是「(7+2×5)/7」的硬凑比例，
//         数字与设计稿里任何一个值都对不上，复核时无法说明 1.53 从哪来（且旧
//         ImageFlowSeg 的选中段宽度也踩了同一个「凭几何凑数」的错误）。
//    环画在 7×7 的盒内（anchors.fill + border.width），所以是**向内**扩散；CSS 的
//    box-shadow 是内外各一半。QML 侧没有向外扩的等价物，此处选了 Ring 完整可见的一侧，
//    视觉半径与 CSS 终点一致（7+2×5=17 → 直径 17 的环），只是内圈被实心点盖住。
//
// ⚠️ 环色必须走 Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)：
//    .qml 里禁止颜色字面量，而本仓的 colors map 没有 accent.glow 这个 token
//    （ThemeBridge.cpp:36-58），所以用 accent.primary 手工压 30% 对齐 --accent-glow。
//
// ⚠️ 「减少动效」下整环 visible:false（停在一个不可见态），实心点仍在 —— 语义仍是「运行中」，
//    不是「没有状态」。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 语义档（主路径）：accent / info / ok / warn / danger / busy / pending / idle ——
    property string tone: "idle"
    // 逃生舱：默认跟着 tone 走；调用方要整体覆盖颜色时直接赋这个（ImageFlowDot 的旧用法）
    property color toneColor: ThemeBridge.colors[root.toneToken]
    property bool run: false        // .dot.run 的 pulse-dot

    // 显式透明：Ctl 是 Rectangle，color 默认白，不写就是一个白色圆角方块
    color: "transparent"

    implicitWidth: 7
    implicitHeight: 7

    // tone → token 点分名（与 Tag.qml 同一条映射：accent 在 CSS 里是 --accent，
    // 本仓的 token 名是 accent.primary；info → accent.info）
    readonly property string toneToken: tone === "accent" ? "accent.primary"
                                            : tone === "info"  ? "accent.info"
                                                              : "status." + tone

    // pulse-dot 的 spread（px）：0→5 对应 box-shadow 0 0 0 0 → 0 0 0 5px
    property real spread: 0
    readonly property bool pulse: root.run && !root.reduce

    // 脉冲环：同尺寸 7×7 的透明圆，border 由 spread 撑开并同步淡出
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        visible: root.pulse
        border.width: root.spread
        border.color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)   // --accent-glow
        opacity: 1 - root.spread / 5
    }

    // 实心点：满强度 tone 色（.dot 的背景由 currentColor 决定）
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.toneColor
    }

    // 1.6s 一轮（base.css:143-145 的 pulse-dot 1.6s）：去 800 + 回 800。
    // InOutQuad 近似 tokens.css:27 的 --ease cubic-bezier(0.2, 0, 0, 1)。
    SequentialAnimation on spread {
        running: root.pulse
        loops: Animation.Infinite
        NumberAnimation { to: 5; duration: 800; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 0; duration: 800; easing.type: Easing.InOutQuad }
    }
}
