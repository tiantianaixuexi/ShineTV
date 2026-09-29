// src/ui/qml/Spinner.qml —— 加载圈（ui.css:456-470 的 .spin / .spin.sm）
//
// ================================ 本文件是共享套件的参考实现 ================================
// 其余共享组件的写法、注释密度、token 纪律都以本文件为准。**先读本文件再动别的组件。**
// ==========================================================================================
//
// 设计稿：
//   .spin     { width 14 / height 14 / border-radius 50% / border 2px solid --line-normal
//               border-top-color --accent / animation spin .7s linear infinite }
//   .spin.sm  { width 11 / height 11 / border-width 1.5px }
//   @keyframes spin { to { transform: rotate(360deg) } }   base.css:147-149
//
// ⚠️⚠️ 这里曾经写错过一次，务必记住：**不能用 Rectangle 叠一条横杠模拟 border-top-color。**
// 那画出来是一根**直的弦**（贴在顶部的窄条），不是弧 —— 用户一眼就能看出转圈是直的。
// 三份重复实现里两份犯了这个错（GallerySpinner 的窄条、StoryboardSpin 的弧帽矩形）。
//
// 正确做法 = QtQuick.Shapes：整圈一条 PathAngleArc（360°）走 --line-normal，
// 顶部四分之一再叠一条 90° 的 PathAngleArc 走 --accent，整体旋转。
//
// 几何按 CSS 的 border-box 盒模型推：
//   14px 盒 + 2px 边 → 描边**向内**收，路径半径 = box/2 - ring/2 = 6。
//   ⚠️ 不要写成 box/2，那样描边有一半落在盒外，视觉上整圈大了一圈。
//
// 角度约定（Qt 的 PathAngleArc，y 轴向下）：
//   0° = 3 点钟方向，正角度**在屏幕上顺时针**走（90° = 6 点，180° = 9 点，270° = 12 点）。
//   CSS 的 border-top-color 覆盖的是 10:30–1:30 这一段，即以 12 点为中的 90°：
//   → startAngle: -135、sweepAngle: 90。（写反了会看到弧在底部，务必出图确认。）
//
// capStyle 用 FlatCap（平头）而不是 RoundCap：CSS 的边框颜色交界在圆角盒上是一条
// 从圆心向外的**斜接**（miter）缝，平头切出来的正是这条半径缝；圆头会在 10:30 / 1:30
// 两处鼓出两个小圆头，14px 下反而更假。
import QtQuick
import QtQuick.Shapes
import Shine 1.0

Ctl {
    id: root

    // —— 配色槽位：绑的是 **token 名**（不是颜色字面量），换主题自动跟着变 ——
    property string trackToken: "line.normal"     // .spin 的 border: 2px solid var(--line-normal)
    property string accentToken: "accent.primary" // .spin 的 border-top-color: var(--accent)
    // 第三条合法路径（契约第 2 条）：直接给颜色。默认**透明 = 不启用**，启用后盖过
    // 上面两个 token —— 供 `currentColor` 场景用（Button 的 loading 转圈，
    // UI.jsx:11 写的是 `border: 2px solid currentColor`，颜色跟按钮字色走）。
    // 取值要用 ThemeBridge.colors[...]，不要写颜色字面量。
    property color tint: "transparent"

    // —— 尺寸档 —— 只有设计稿存在的两档，多的别加 ——
    property bool sm: false                       // .spin.sm：11px + 1.5px 边
    // 唯一的例外口：Button 的 loading 转圈（UI.jsx:11 把 13 写死在行内样式里，
    // 不属于 .spin 的两档）。0 = 跟随 sm，别在其他地方用。
    property int sizePx: 0
    readonly property real box: root.sizePx > 0 ? root.sizePx : (sm ? 11 : 14)
    readonly property real ring: sm ? 1.5 : 2

    // —— 动效槽位 —— @keyframes spin .7s linear infinite ——
    property bool running: true                   // 调用方可以停转（加载结束就 false）
    property int period: 700

    // 显式透明：Rectangle.color 默认是白色，不写就是一个白色方块
    color: "transparent"

    implicitWidth: box
    implicitHeight: box

    // 描边中线半径 = 盒半宽 - 边宽的一半（CSS 边框向内收）
    readonly property real r: box / 2 - ring / 2

    Item {
        id: rotor
        width: root.box
        height: root.box

        // @keyframes spin { to { transform: rotate(360deg) } } —— Linear，不带缓动
        RotationAnimation on rotation {
            from: 0
            to: 360
            duration: root.period
            loops: Animation.Infinite
            // 「减少动效」下退化为静止弧（语义仍是「在加载」），不是空圈
            running: root.running && !root.reduce
        }

        Shape {
            anchors.fill: parent

            // 底圈：整圈 360°，--line-normal（tint 启用时为 currentColor 的 30%）
            ShapePath {
                strokeColor: root.tint.a > 0 ? Qt.alpha(root.tint, 0.3)
                                             : ThemeBridge.colors[root.trackToken]
                strokeWidth: root.ring
                capStyle: ShapePath.FlatCap
                // ⚠️ 必须显式透明：PathAngleArc 是**开放**路径，ShapePath 会隐式闭合后填充，
                // 不关掉就会在圆心糊出一坨色块。不设 strokeWidth 也会吃到默认的
                // 「不透明白色、宽 1」描边（见 docs ⑩）。
                fillColor: "transparent"
                PathAngleArc {
                    centerX: root.box / 2
                    centerY: root.box / 2
                    radiusX: root.r
                    radiusY: root.r
                    startAngle: -180
                    sweepAngle: 360
                }
            }

            // 顶弧：12 点为中的 90°，--accent（tint 启用时为 currentColor）
            ShapePath {
                strokeColor: root.tint.a > 0 ? root.tint
                                             : ThemeBridge.colors[root.accentToken]
                strokeWidth: root.ring
                capStyle: ShapePath.FlatCap
                fillColor: "transparent"
                PathAngleArc {
                    centerX: root.box / 2
                    centerY: root.box / 2
                    radiusX: root.r
                    radiusY: root.r
                    // 12 点 = -90°；向左让 45°、向右让 45° = 覆盖 10:30–1:30
                    startAngle: -135
                    sweepAngle: 90
                }
            }
        }
    }
}
