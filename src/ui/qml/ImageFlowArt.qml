// src/ui/qml/ImageFlowArt.qml —— 对照 webui ui.css:1075-1089 的 .art
// （ui.css:1075 radius --r-sm / overflow hidden / 底 --fill-muted）
//
// ⚠️ 这里**没有**逐像素复刻设计稿的占位画面：Art（UI.jsx:184-220）与
// ReviewPanel 内联 SVG 用的是一张写死的十六进制调色板（既不在 tokens.css 里，
// 也不在 ColorToken 里），搬进 QML 就等于新增第二套颜色真值。所以只保留它的
// **构图**（天光渐变 + 光球 + 三道山脊 + preserveAspectRatio slice），
// 颜色全部换成同一批 token：--bg-void / --accent-primary / --status-warn。
// 组件用法与几何（盒高由调用方给、150 / 190）保持不变。
import QtQuick
import Shine 1.0

Ctl {
    id: art

    property bool dimmed: false          // 设计稿：未评审时整块 opacity .25
    readonly property int artW: 160      // 设计稿 SVG 的 viewBox
    readonly property int artH: 100

    radius: art.rSm                     // --r-sm
    color: ThemeBridge.colors["fill.muted"]
    clip: true

    Canvas {
        id: scene
        anchors.fill: parent
        // Immediate：onPaint 读 ThemeBridge，Cooperative 会把它挪到渲染线程
        renderStrategy: Canvas.Immediate
        opacity: art.dimmed ? 0.25 : 1.0
        Behavior on opacity {
            NumberAnimation { duration: ThemeBridge.reduceMotion ? 0 : ThemeBridge.durations.slow }
        }

        // 尺寸 / 主题色任一变化都只靠**这一个**处理器重画。
        // ⚠️ 不用 onWidthChanged / onHeightChanged 单表达式体（QQml 会把它当属性
        // 赋值，见 ImageFlow.qml 文件头的纪律），也不叠 Connections 重复重画。
        readonly property string paintKey: Math.round(width) + "x" + Math.round(height)
                                           + "|" + ThemeBridge.colors["bg.void"]
        onPaintKeyChanged: { requestPaint() }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            // preserveAspectRatio="xMidYMid slice"：按 cover 缩放后居中裁切
            var s = Math.max(width / art.artW, height / art.artH)
            ctx.translate((width - art.artW * s) / 2, (height - art.artH * s) / 2)
            ctx.scale(s, s)

            var void_ = ThemeBridge.colors["bg.void"]
            var glow = ThemeBridge.colors["accent.primary"]
            var warm = ThemeBridge.colors["status.warn"]

            // 天空：底色 → 主强调色 55% 透明
            var sky = ctx.createLinearGradient(0, 0, 0, art.artH)
            sky.addColorStop(0, void_)
            sky.addColorStop(1, Qt.alpha(glow, 0.55))
            ctx.fillStyle = sky
            ctx.fillRect(0, 0, art.artW, art.artH)

            // 光球 + 外晕
            ctx.fillStyle = Qt.alpha(warm, 0.25)
            ctx.beginPath(); ctx.arc(60, 34, 20, 0, Math.PI * 2); ctx.fill()
            ctx.fillStyle = Qt.alpha(warm, 0.9)
            ctx.beginPath(); ctx.arc(60, 34, 13, 0, Math.PI * 2); ctx.fill()

            // 三道山脊（前景压暗，与设计稿的层次关系一致）
            ctx.fillStyle = Qt.alpha(void_, 0.75)
            ctx.beginPath(); ctx.moveTo(0, 78)
            ctx.quadraticCurveTo(30, 58, 55, 74)
            ctx.quadraticCurveTo(80, 86, 110, 70)
            ctx.lineTo(art.artW, 76); ctx.lineTo(art.artW, art.artH); ctx.lineTo(0, art.artH)
            ctx.closePath(); ctx.fill()

            ctx.fillStyle = Qt.alpha(glow, 0.3)
            ctx.beginPath(); ctx.moveTo(0, 88)
            ctx.quadraticCurveTo(40, 72, 75, 86)
            ctx.quadraticCurveTo(120, 96, art.artW, 84)
            ctx.lineTo(art.artW, art.artH); ctx.lineTo(0, art.artH)
            ctx.closePath(); ctx.fill()

            ctx.fillStyle = Qt.alpha(void_, 0.9)
            ctx.beginPath(); ctx.moveTo(0, 94)
            ctx.quadraticCurveTo(50, 86, 100, 92)
            ctx.quadraticCurveTo(130, 96, art.artW, 90)
            ctx.lineTo(art.artW, art.artH); ctx.lineTo(0, art.artH)
            ctx.closePath(); ctx.fill()
        }
    }
}
