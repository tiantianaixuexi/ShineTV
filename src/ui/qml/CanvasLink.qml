// src/ui/qml/CanvasLink.qml —— 流程画布的连线（**共享件**）
//
// 对照 webui FlowCanvas.jsx:16-19 + 142-169 的连线。
// 2026-09-30 起由出图（ImageFlow.qml）与出片（VideoFlow.qml）两个页面共用；
// 设计稿两页画的是同一种连线，去掉 ImageFlow 前缀（同 CanvasNode.qml）。
//
// 设计稿一条连线画两遍：
//   ① 5px --line-normal 光晕 @ opacity .4（打底）
//   ② 1.8px 芯线，两端节点都不是 todo 时用 --accent → --info 的渐变虚线，
//      stroke-dasharray: 6 6 + @keyframes flow-dash（dashoffset 0 → -24 / 0.7s 线性循环）
//
// 为什么是 Shape 而不是 Rectangle：这是**曲线 + 渐变描边 + 虚线流动**，
// 矩形只能画轴对齐直线段，没有描边渐变也没有虚线。节点卡片/面板/行这些实心块面
// 才用 Rectangle/Ctl。
//
// ⚠️⚠️ 12 段芯线是**字面量展开**的，不是 Repeater：
//   ShapePath 派生自 QQuickPath（QObject），**不是 QQuickItem**；而 Repeater 的
//   delegate 必须是 Item。两者语义直接冲突，运行期必报
//   `Delegate must be of Item type`（一个 delegate 都创建不出来）。
//   qmllint 查不出来 —— Shape 能解析、ShapePath 能解析，只有运行期组合才炸。
//   同款坑见 verify 侧的 ArtInk.qml（14 层模糊同样字面量展开）。
//
// ⚠️ Qt 6.11 的 ShapePath **没有** strokeGradient，也没有 opacity：
//   `fillGradient` 只作用于填充。所以渐变靠「把三次贝塞尔切成 12 段弦、每段
//   染一档插值色」实现（颜色在两枚 token 色之间插值，不引入新色值）。
//
// 虚线相位的连续性（展开成 12 段后最容易丢的一条）：
//   dashOffset 的定义是「从虚线图案的哪个位置开始画」，一段子路径要复现整条
//   路径的图案，它自己的偏移必须加上**段首**的累计弧长：
//       dashOffset = 相位 + s0        （不是 - s0，也不是用段末弧长）
//   s0/s1 都在 segs 里现算。相位 0 → -24*zoom 走满两个虚线周期（6+6=12），回到 0 无缝。
//
// ⚠️ 坐标系：**ax/ay/bx/by 传进来的是视图坐标**（宿主已把取景变换算好），
//   不是世界坐标。本组件自己**不再**吃任何父级 transform，也不在组件内部写
//   anchors.fill —— 尺寸与位置一律由宿主显式给。节点那边（CanvasNode 的
//   x/y + scale）用的是同一组 viewX / viewY / zoom，两边因此必然重合。
//   踩过的坑：曾经把连线放在带 transform 的容器里、用世界坐标，而节点走另一条
//   路径，结果整束连线画到画外左上方、汇聚成一个画外公共点，节点端口却都对。
//   现在两边共用同一份显式公式，这类错位**不可能**再发生。
//   zoom 同时缩放：描边宽（5 / 1.8）、虚线节距（6 6）、dx 的 36 底线。
//
// ⚠️ PathCubic 端点约定（幽灵曲线的根因，见光晕处注释）：
//   QML 的 PathCubic 只有 x/y（起点）+ relativeX/relativeY（终点位移），
//   **没有 startX/startY**。漏写 relativeX/Y，终点就是 (0,0)。
//   这一类「隐式默认值不是你想的那个点」的坑，本页已中过两次
//   （Gallery 侧的 M dx dy、这里的光晕），写路径元素时一律显式给全两端。
import QtQuick
import QtQuick.Shapes
import Shine 1.0

Shape {
    id: root

    // 连线两端。**已经是视图坐标**（宿主把取景变换算好传进来了）：
    //   ax = viewX + zoom * (node.x + 151)、ay = viewY + zoom * (node.y + 38) …
    // 节点那边用同一组 viewX / viewY / zoom 自己算 x/y + scale，所以两端必然重合。
    property real ax: 0
    property real ay: 0
    property real bx: 0
    property real by: 0
    property bool active: false        // nb.state !== 'todo' && na.state !== 'todo'
    // 画布缩放：描边宽、虚线节距、dx 都要跟着缩（等价于容器 scale 时的行为）
    property real zoom: 1

    // linkPath：dx = max(36, |bx-ax| * 0.55)，控制点在两端水平外推 dx。
    // 这里三个量都是视图坐标，所以 36 的底线要一起乘 zoom 才与设计稿等价。
    readonly property real dx: Math.max(36 * root.zoom, Math.abs(bx - ax) * 0.55)
    readonly property int segCount: 12
    property real dashPhase: 0          // 0 → -24 * zoom（见文件头）

    // ⚠️ 这里**不写** anchors.fill: parent：组件内部的锚点绑定在 Repeater 委托里
    // 容易和宿主的定位打架（这一页踩过一次），尺寸一律由宿主显式给 x/y/width/height。
    antialiasing: true
    // 不写 preferredRendererType：Shape 在非 RHI 场景图后端（software）下
    // 会自己切到 SoftwareRenderer，写死反而会盖掉这层判断。

    // —— 光晕：stroke 5 · --line-normal @40% · round cap（单条三次贝塞尔）——
    //
    // ⚠️⚠️ PathCubic 的端点约定（踩过的坑，幽灵曲线的根因）：
    //   PathCubic 继承 QQuickCurve，QML 侧只有
    //       x / y          = **起点**
    //       relativeX/Y    = **终点相对起点的位移**（默认 0,0）
    //   没有 startX / startY，也没有 endPoint。
    //   只写 x/y 不写 relativeX/Y 的话，终点会落在 (0,0) —— 于是每条光晕都从
    //   自己的端口一路画到页面原点，在左上角扫出一片幽灵曲线。
    //   （芯线那 12 段用 PathPolyline 传两个绝对点，天然免疫，所以当时只有光晕错。）
    //   SVG 里的 `M ax ay C …` 在 QML 要写成「起点 = ax,ay，终点 = ax+relativeX …」。
    ShapePath {
        strokeColor: Qt.alpha(ThemeBridge.colors["line.normal"], 0.4)   // opacity .4
        strokeWidth: 5 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        // ⚠️⚠️ 路径起点必须设在 **ShapePath（本路径）** 上，不能只设 PathCubic 的 x/y。
        //
        // ShapePath 派生自 QQuickPath，路径起点是它的 startX/startY；
        // 而 QQuickCurve::x() 在「本元素是路径里第一个元素」时返回的是
        // **path->startPoint()**，不是它自己的 x 属性。于是只写
        // `PathCubic { x: ax; y: ay }` 会被旁路，曲线一律从 (0,0) 起步，
        // 终点也变成 `0 + relativeX` —— 表现为页面上多出一整簇从画布左上角
        // 扇向各节点端口的长斜线（一度被误判成「陈旧几何残留」）。
        // 两个都写：startX/startY 决定实际起点，x/y 保持与文档一致的显式表达。
        startX: root.ax
        startY: root.ay
        PathCubic {
            x: root.ax; y: root.ay
            relativeX: root.bx - root.ax
            relativeY: root.by - root.ay
            control1X: root.ax + root.dx; control1Y: root.ay
            control2X: root.bx - root.dx; control2Y: root.by
        }
    }

    // —— 芯线 12 段（字面量，见文件头：ShapePath 不能做 Repeater 的 delegate）——
    // 非 active 段：单色 --line-strong 实线（无虚线）；active 段：插值色 + 6/6 虚线。
    // 每段的 PathPolyline 给**两个**实算端点，不是 moveto，所以不存在 Gallery
    // 那边「M dx dy 只移动当前点、图形原地不动」的坑。
    // i = 0
    ShapePath {
        strokeColor: root.active ? root.segs[0].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[0].s0
        PathPolyline {
            path: [Qt.point(root.segs[0].x1, root.segs[0].y1),
                   Qt.point(root.segs[0].x2, root.segs[0].y2)]
        }
    }
    // i = 1
    ShapePath {
        strokeColor: root.active ? root.segs[1].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[1].s0
        PathPolyline {
            path: [Qt.point(root.segs[1].x1, root.segs[1].y1),
                   Qt.point(root.segs[1].x2, root.segs[1].y2)]
        }
    }
    // i = 2
    ShapePath {
        strokeColor: root.active ? root.segs[2].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[2].s0
        PathPolyline {
            path: [Qt.point(root.segs[2].x1, root.segs[2].y1),
                   Qt.point(root.segs[2].x2, root.segs[2].y2)]
        }
    }
    // i = 3
    ShapePath {
        strokeColor: root.active ? root.segs[3].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[3].s0
        PathPolyline {
            path: [Qt.point(root.segs[3].x1, root.segs[3].y1),
                   Qt.point(root.segs[3].x2, root.segs[3].y2)]
        }
    }
    // i = 4
    ShapePath {
        strokeColor: root.active ? root.segs[4].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[4].s0
        PathPolyline {
            path: [Qt.point(root.segs[4].x1, root.segs[4].y1),
                   Qt.point(root.segs[4].x2, root.segs[4].y2)]
        }
    }
    // i = 5
    ShapePath {
        strokeColor: root.active ? root.segs[5].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[5].s0
        PathPolyline {
            path: [Qt.point(root.segs[5].x1, root.segs[5].y1),
                   Qt.point(root.segs[5].x2, root.segs[5].y2)]
        }
    }
    // i = 6
    ShapePath {
        strokeColor: root.active ? root.segs[6].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[6].s0
        PathPolyline {
            path: [Qt.point(root.segs[6].x1, root.segs[6].y1),
                   Qt.point(root.segs[6].x2, root.segs[6].y2)]
        }
    }
    // i = 7
    ShapePath {
        strokeColor: root.active ? root.segs[7].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[7].s0
        PathPolyline {
            path: [Qt.point(root.segs[7].x1, root.segs[7].y1),
                   Qt.point(root.segs[7].x2, root.segs[7].y2)]
        }
    }
    // i = 8
    ShapePath {
        strokeColor: root.active ? root.segs[8].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[8].s0
        PathPolyline {
            path: [Qt.point(root.segs[8].x1, root.segs[8].y1),
                   Qt.point(root.segs[8].x2, root.segs[8].y2)]
        }
    }
    // i = 9
    ShapePath {
        strokeColor: root.active ? root.segs[9].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[9].s0
        PathPolyline {
            path: [Qt.point(root.segs[9].x1, root.segs[9].y1),
                   Qt.point(root.segs[9].x2, root.segs[9].y2)]
        }
    }
    // i = 10
    ShapePath {
        strokeColor: root.active ? root.segs[10].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[10].s0
        PathPolyline {
            path: [Qt.point(root.segs[10].x1, root.segs[10].y1),
                   Qt.point(root.segs[10].x2, root.segs[10].y2)]
        }
    }
    // i = 11
    ShapePath {
        strokeColor: root.active ? root.segs[11].color : ThemeBridge.colors["line.strong"]
        strokeWidth: 1.8 * root.zoom
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        fillColor: "transparent"
        dashPattern: root.active ? [6 * root.zoom, 6 * root.zoom] : []
        dashOffset: root.dashPhase + root.segs[11].s0
        PathPolyline {
            path: [Qt.point(root.segs[11].x1, root.segs[11].y1),
                   Qt.point(root.segs[11].x2, root.segs[11].y2)]
        }
    }

    // @keyframes flow-dash { to { stroke-dashoffset: -24 } } · 0.7s linear infinite
    NumberAnimation on dashPhase {
        from: 0; to: -24 * root.zoom
        duration: 700
        loops: Animation.Infinite
        running: root.active && !ThemeBridge.reduceMotion
    }

    // 采样：三次贝塞尔 → segCount 段弦。每段带
    //   s0/s1 = 段首 / 段末的累计弧长（虚线相位对齐用）
    //   color = 两枚 token 色在 t1 处的插值（不引任何硬编码色值）
    // ⚠️ Qt.tint 只有 **两个** 参数，没有三参重载 —— 传第三个会在运行期报
    //    "Too many arguments"（qmllint 查不出来），所以这里逐分量插值。
    //    （代价：本文件会命中「硬编码色值」自查正则的 rgb 一项。它不是写死的
    //      色值，而是两枚主题色按 t 算出来的值。）
    readonly property var segs: {
        var out = []
        var p0x = root.ax, p0y = root.ay
        var p1x = root.ax + root.dx, p1y = root.ay
        var p2x = root.bx - root.dx, p2y = root.by
        var p3x = root.bx, p3y = root.by
        var c0 = ThemeBridge.colors["accent.primary"]
        var c1 = ThemeBridge.colors["accent.info"]
        var acc = 0, px = p0x, py = p0y
        for (var i = 0; i < root.segCount; ++i) {
            var t0 = i / root.segCount
            var t1 = (i + 1) / root.segCount
            var u0 = 1 - t0
            var qx = u0 * u0 * u0 * p0x + 3 * u0 * u0 * t0 * p1x + 3 * u0 * t0 * t0 * p2x + t0 * t0 * t0 * p3x
            var qy = u0 * u0 * u0 * p0y + 3 * u0 * u0 * t0 * p1y + 3 * u0 * t0 * t0 * p2y + t0 * t0 * t0 * p3y
            var s1 = acc + Math.sqrt((qx - px) * (qx - px) + (qy - py) * (qy - py))
            out.push({
                x1: px, y1: py, x2: qx, y2: qy, s0: acc, s1: s1,
                color: Qt.rgba(c0.r + (c1.r - c0.r) * t1,
                               c0.g + (c1.g - c0.g) * t1,
                               c0.b + (c1.b - c0.b) * t1,
                               0.9)
            })
            acc = s1
            px = qx; py = qy
        }
        return out
    }
}
