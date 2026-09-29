// src/ui/qml/Icon.qml —— 线性图标（复刻 webui/src/components/Icon.jsx）
//
// 逐条照抄 Icon.jsx 的 P 表：同一份 24 视口路径、同一 stroke-width 1.6、
// 同一 round cap / round join。
//
// ⚠️ **路径必须整体缩放，描边宽不要单独换算**（2026-09-29 修的真 bug）。
//    `PathSvg` 只解析 path 串，**没有 viewBox**：路径里的 24 单位坐标会按 1:1 设备
//    像素画进本组件的盒子。原来只把 strokeWidth 乘了 width/24 就算「换算」过了，
//    几何原封不动 —— 于是 15×15 的盒子里画着一张 24×24 的图，往右下溢出 9px：
//    出图表现就是「图标比文字大、吊在文字下面、没居中」（.btn 里的 play/eye/alert
//    三个最明显）。正确做法是给承载的 Shape 一个缩放，让 0..24 映射到 0..width，
//    描边随缩放一起变小，就该用设计稿原值 1.6，不要再乘 width/24（那是乘两遍）。
//
// ⚠️ 查不到名字时回落到 P.info —— Gallery.jsx 的 icon="flow" 就不在表里，
// 设计稿本身走的就是这条回落路径。
import QtQuick
import QtQuick.Shapes
import Shine 1.0

Ctl {
    id: root

    property string name: ""
    property color glyphColor: ThemeBridge.colors["text.primary"]
    // 设计稿的 stroke-width（Icon.jsx），写在 24 视口坐标里 —— 不要在这里换算，
    // 换算由下面 Shape 的 scale 统一做。
    property real strokeWidth: 1.6

    // ⚠️ 必须显式给 transparent：Ctl 的基类是 Rectangle，而 Rectangle.color
    //    的默认值是**白色**。不写这一行就是一个不透明白方块打底，
    //    上面再叠描边路径 —— 表现为「图标是个白底方块」。
    color: "transparent"

    implicitWidth: 16
    implicitHeight: 16

    readonly property string d: iconPaths[root.name] !== undefined ? iconPaths[root.name] : iconPaths["info"]
    // 24 视口 → 本组件盒子。设计稿的 .icon 恒为正方形（15 / sm 13），所以两轴同值；
    // 万一被塞进非正方形的盒子，取小的那个至少保证不溢出（宁可留白也不画出去）。
    readonly property real viewScale: Math.min(root.width, root.height) / 24

    // ⚠️ 属性名不能以大写字母开头（QML 把它当类型名），所以是 iconPaths 不是 PATHS
    readonly property var iconPaths: ({
        "alert": "M12 8.5V13m0 3.2h.01M10.3 4.2 2.8 17a1.6 1.6 0 0 0 1.4 2.4h15.6a1.6 1.6 0 0 0 1.4-2.4L13.7 4.2a1.6 1.6 0 0 0-2.8 0Z",
        "check": "M4.5 12.5 10 18 19.5 6.5",
        "chevdown": "M5.5 9 12 15.5 18.5 9",
        "download": "M12 4v10m0 0 4-4m-4 4-4-4M5 19h14",
        "eye": "M12 5.5c5 0 8.5 4.2 9.5 6.5-1 2.3-4.5 6.5-9.5 6.5S3.5 14.3 2.5 12c1-2.3 4.5-6.5 9.5-6.5Zm0 9a2.5 2.5 0 1 0 0-5 2.5 2.5 0 0 0 0 5Z",
        "folder": "M3.5 7A1.5 1.5 0 0 1 5 5.5h4l2 2.5h8A1.5 1.5 0 0 1 20.5 9.5v8A1.5 1.5 0 0 1 19 19H5a1.5 1.5 0 0 1-1.5-1.5V7Z",
        "grid": "M4 4h7v7H4V4Zm9 0h7v7h-7V4ZM4 13h7v7H4v-7Zm9 0h7v7h-7v-7Z",
        "image": "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11Zm0 8.2 4.2-4.2 5 5m1.6-1.6 1.7-1.7L20 14.7M15 9.5h.01",
        "info": "M12 11v5m0-8.5h.01M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z",
        "layers": "m12 3 9 5-9 5-9-5 9-5Zm9 9-9 5-9-5m18 4.5-9 5-9-5",
        "list": "M9 6h11M9 12h11M9 18h11M4 6h.01M4 12h.01M4 18h.01",
        "palette": "M12 21a9 9 0 1 1 9-9c0 2-1.5 3-3 3h-2a2 2 0 0 0-1.5 3.3c.4.5.4 1.2-.2 1.5-.7.2-1.5.2-2.3.2ZM7.5 11h.01M10 7.8h.01M14.5 7.5h.01",
        "play": "M8 5.8v12.4a.6.6 0 0 0 .92.5l9.6-6.2a.6.6 0 0 0 0-1L8.92 5.3a.6.6 0 0 0-.92.5Z",
        "refresh": "M19 12a7 7 0 1 1-2-4.9M19 4v4h-4",
        "settings": "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm7.4-3a7.4 7.4 0 0 0-.1-1.2l2-1.5-2-3.4-2.3 1a7.4 7.4 0 0 0-2.1-1.3L14.5 3h-5l-.4 2.6a7.4 7.4 0 0 0-2 1.2l-2.4-1-2 3.5 2 1.5a7.4 7.4 0 0 0 0 2.4l-2 1.5 2 3.4 2.3-1a7.4 7.4 0 0 0 2.1 1.3l.4 2.6h5l.4-2.6a7.4 7.4 0 0 0 2-1.2l2.4-1 2-3.5-2-1.5c.1-.4.1-.8.1-1.2Z",
        "sparkles": "M12 4.5 13.8 9l4.7 1.8-4.7 1.8L12 17l-1.8-4.4L5.5 10.8 10.2 9 12 4.5ZM19 15l.9 2.1L22 18l-2.1.9L19 21l-.9-2.1L16 18l2.1-.9L19 15ZM5.5 3l.7 1.8L8 5.5l-1.8.7L5.5 8l-.7-1.8L3 5.5l1.8-.7L5.5 3Z",
        "target": "M12 13.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Zm0 4.5a6 6 0 1 0 0-12 6 6 0 0 0 0 12Zm0-16.5v3m0 17v-3M3 12h3m12 0h3",
        "x": "M6 6l12 12M18 6 6 18",
        "zap": "M13 3 5 13.5h5.5L11 21l8-10.5h-5.5L13 3Z"
    })

    // ⚠️ 不能 `anchors.fill: parent` —— 那等于声明「路径就在 0..width 里」，
    //    而路径实际是 0..24，框再大也拦不住。改成固定 24×24 的画布 + 整体缩放。
    //    transformOrigin 必须是 TopLeft：缩放要绕左上角才能把 0..24 映到 0..width，
    //    取 Center 会让整幅画向左上平移 w/2×(k-1)（同 Art.qml 那条坑）。
    Shape {
        x: 0
        y: 0
        width: 24
        height: 24
        transformOrigin: Item.TopLeft
        scale: root.viewScale
        antialiasing: true
        ShapePath {
            strokeColor: root.glyphColor
            // 设计稿原值 1.6：随上面的 scale 一起变小，15px 下等效 1.0px
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.d }
        }
    }
}
