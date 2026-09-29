// src/ui/qml/AutoGrid.qml —— 响应式栅格（CSS `grid-template-columns: repeat(auto-fit, minmax(N, 1fr))`）
//
// ⚠️ **名字不能叫 Grid.qml**：`import QtQuick` 里本来就有 Qt 的内建 `Grid` 类型
//    （columns / columnSpacing / rowSpacing），同目录的 `Grid.qml` 会被它盖掉 ——
//    症状是 `Could not find property "minCell"` + `Member "cellW" not found on
//    type "Grid"`，而 qmllint **不会**告诉你「你写的 Grid 不是你想的那个」。
//    和 `Button.qml` 撞 `QtQuick.Controls.Button` 是同一类坑。
//    同理 `Flex` 可以用：QtQuick 里没有 `Flex` 这个类型。
//
// ================================ 为什么这个组件存在 ================================
// 2026-09-29 之前，Gallery.qml 自己手搓了一套：一张 `cards` 数组给每张卡写死一个
// `ch`（内容高度常数），再用一个 `gridSlots()` 函数把 x/y/w/h 全算出来。
// **那是错的**：高度是猜的，组件一改尺寸就跟内容对不上——内容比 `ch` 高就溢出、
// 压住下一行（实测：Button 卡溢出压住 Field 卡标题，中间一道黑带），比 `ch` 矮
// 就留一块死白。共享组件换版后这种错只会更多。
//
// 本组件把两件事一起解决：
//   1. **列数与列宽随宽度自适应**（auto-fit + minmax + 1fr 的等价物），不写死；
//   2. **行高由该行最高的子项决定**，子项自己报 `implicitHeight` / 显式 `height`，
//      页面**不需要再声明任何高度常数**。这正是 `align-items: start` 的语义：
//      同一行的卡片顶边对齐，行高取最高者，矮的那张下面留白而不是被拉伸。
//
// ⚠️ 行高「取最高者」是 Qt `Grid` 的**原生**行为，不是本组件模拟的：Grid 按行分组，
//    每行高度 = 该行子项 implicitHeight 的最大值，子项在行内顶对齐。
//    CSS 想要 `align-items: stretch` 的话要让子项自己写 `height`，**本组件刻意不提供
//    stretch 开关** —— 加一个「拉齐」开关就得自己维护一份行高表，那正好是本轮
//    删掉的手写 `ch` 高度常数。同样的能力请用需要 stretch 的那类布局（`Flex` 有
//    `align: "stretch"`）。
//
// 用法：
//   AutoGrid {
//       minCell: 340              // minmax(340px, 1fr)
//       gap: 16
//       width: parent.width
//       Card { width: autoGrid.cellW }   ← 宽度由 cellW 给，**不要**自己算
//       Card { width: autoGrid.cellW }
//   }
//
// ⚠️ 子项**只由本组件的 Grid 接管 x/y**，所以子项里不要再写 `y:` 也不要再挂
//    `anchors.verticalCenter`（会打出 binding loop）。要行内垂直对齐，用子项自己
//    的 `height` + 内部布局，别动位置。
// ⚠️ 反过来：子项若为了 hover 位移自己绑了 `y`（如 Card 的 translateY(-2px)），
//    在本组件里会与 Grid 抢 y。**有 hover 位移的东西不要直接当子项**，
//    套一层不绑 y 的 Item（见 Gallery.qml 的做法）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // minmax(下界, 1fr) 的下界。列宽 = max(minCell, (可用宽 - 间距×(列数-1)) / 列数)
    property int minCell: 340
    // 0 = 不限列数（auto-fit）。给了正数就封顶（相当于 max-width 那种约束）。
    property int maxColumns: 0
    property int gap: ThemeBridge.spaces["4"]          // 16

    default property alias content: cells.data

    // —— 列数：CSS repeat(auto-fit, minmax(minCell, 1fr)) 的等价算式 ——
    // 能塞下几列 = floor((可用宽 + gap) / (minCell + gap))，最少 1 列。
    // ⚠️ 宽度未定时（宿主还没给尺寸）也必须返回 1 而不是 0/NaN —— 0 会让
    //    Grid 报 `columns cannot be negative`，NaN 会让 cellW 变 NaN 传播到子项宽度。
    readonly property int columns: {
        var w = root.width
        if (w <= 0)
            return 1
        var n = Math.max(1, Math.floor((w + root.gap) / (root.minCell + root.gap)))
        return root.maxColumns > 0 ? Math.min(n, root.maxColumns) : n
    }

    // 1fr 的实际像素值：可用宽扣掉列间距后平分。子项宽度一律读这个。
    readonly property real cellW: {
        var w = root.width
        if (w <= 0)
            return root.minCell
        return (w - root.gap * (root.columns - 1)) / root.columns
    }

    implicitWidth: root.minCell
    // 行高由 Grid 自己算（每行取最高者），本组件不参与
    implicitHeight: cells.implicitHeight

    color: "transparent"

    Grid {
        id: cells
        width: parent.width
        columns: root.columns
        columnSpacing: root.gap
        rowSpacing: root.gap
    }
}
