// src/ui/qml/ImageFlowSpin.qml —— 对照 webui ui.css:457-470 的 .spin / .spin.sm
// 14px（sm 11px）圆环，2px（sm 1.5px）--line-normal、border-top 用 --accent，
// 配 base.css:147-149 的 @keyframes spin（0.7s linear infinite）。
//
// ⚠️ Qt 6.11 的 Rectangle **没有分边 border**（border.topColor 已被移除），
// 所以这个「缺一段的圆环」用 Canvas 画：整环 line-normal + 顶部 90° accent 头，
// 再整体旋转。线宽/半径都取自 .spin 的 CSS 盒模型（线宽向内收）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property bool sm: false

    width: sm ? 11 : 14
    height: width
    color: "transparent"
    implicitWidth: width
    implicitHeight: height

    Canvas {
        id: ring
        anchors.fill: parent
        renderStrategy: Canvas.Immediate   // onPaint 读 ThemeBridge → 留在 GUI 线程
        // 尺寸 / 主题色任一变化都只靠**这一个**处理器重画（块体写法，
        // 不挂 onWidthChanged / onHeightChanged 单表达式体，见 ImageFlow.qml 文件头）
        readonly property string paintKey: Math.round(width) + "x" + Math.round(height)
                                           + "|" + ThemeBridge.colors["accent.primary"]
        onPaintKeyChanged: { requestPaint() }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var lw = root.sm ? 1.5 : 2
            var r = (width - lw) / 2
            var cx = width / 2, cy = height / 2
            ctx.lineWidth = lw
            ctx.lineCap = "butt"
            ctx.strokeStyle = ThemeBridge.colors["line.normal"]
            ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.stroke()
            ctx.strokeStyle = ThemeBridge.colors["accent.primary"]
            ctx.beginPath(); ctx.arc(cx, cy, r, -Math.PI, 0); ctx.stroke()
        }
    }

    RotationAnimation on rotation {
        from: 0; to: 360
        duration: 700          // animation: spin 0.7s linear infinite
        loops: Animation.Infinite
        running: !root.reduce
    }
}
