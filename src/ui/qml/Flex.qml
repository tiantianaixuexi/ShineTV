// src/ui/qml/Flex.qml —— 可换行横排（CSS `display:flex; flex-wrap:wrap`）
//
// ================================ 为什么这个组件存在 ================================
// 旧的页私有 `GalleryFlow.qml` 已经做对了「按宽度切行 + 行内 align-items:center」，
// 但它是**页面私有**的，而且只支持居中一种对齐、没有水平对齐（justify），
// 末行永远是左对齐挂在边上。本组件是它的共享化 + 泛化版：
//
//   .gal-row { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
//
// 三条对外能力（设计稿里都真实用到了）：
//   * `align`   —— 行内**垂直**对齐：start / center / stretch（默认 center）
//   * `justify` —— 每行**水平**对齐：start / center / end（默认 start = CSS 默认值）
//   * 自适应高度 —— 换行后高度由排版结果决定，调用方**不需要声明行高**
//
// 为什么不用 Qt 自带的 `Flow`：Qt 的 `Flow` 只有 `spacing`，没有 justify / align，
// 且行内子项的位置在换行时**不居中**。本页「末行 2 个缩略图挂在左边、右边空一大块」
// 就是这个差别。Qt 的 `Flow` 仍然适合「纯左对齐 + 换行」，两者按需选。
//
// ⚠️ 位置是**赋值**而不是绑定（`it.c.x = …` / `it.c.y = …`）。行内元素不做 hover
//    位移，所以不构成 binding loop。**但子项自己绑了 y 就会打架** —— 别给
//    本组件的子项写 `y:` 或 `anchors.verticalCenter`。
//
// ⚠️ 重排的触发条件写在 `_key` 里而不是散落一串 onWidthChanged：子项的
//    **尺寸**变化要能触发重排（字体落定、inner Loader 加载完、hover 改宽度…），
//    而子项的**位置**变化不能（否则自己写 x 就把自己再触发一次，死循环）。
//    所以 key 只读 width/height，不读 x/y。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // .gal-row gap 10
    property int gap: 10
    // 行内垂直对齐
    property string align: "center"      // "start" | "center" | "stretch"
    // 每行水平对齐
    property string justify: "start"     // "start" | "center" | "end"

    default property alias content: box.data

    // 排版结果高度，由 relayout() 写 —— 不能是 readonly
    property real layoutH: 0

    implicitWidth: box.childrenRect.width
    implicitHeight: layoutH

    color: "transparent"

    Item {
        id: box
        width: parent.width
    }

    // —— 重排触发：宽度 / 间距 / 对齐 / 子项增删 / 子项尺寸 ——
    // 只读尺寸不读位置。子项尺寸写的是 `width > 0 ? width : implicitWidth`，
    // 与 relayout() 里取宽高的口径完全一致，否则 key 和排版结果会对不上。
    readonly property string _key: {
        var kids = box.children
        var s = Math.round(root.width) + "|" + root.gap + "|" + root.align + "|"
              + root.justify + "|" + kids.length
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            var w = c.width > 0 ? c.width : (c.implicitWidth || 0)
            var h = c.height > 0 ? c.height : (c.implicitHeight || 0)
            s += ";" + Math.round(w) + "x" + Math.round(h)
        }
        return s
    }
    on_KeyChanged: defer.restart()

    // 子项在 onCompleted 那一轮的 implicitWidth 往往还没算完，延到事件循环之后再排。
    // 这也是「上一帧宽高全是 0 → 排出一行 → 下一帧尺寸落定 → key 变 → 再排」的自愈路径。
    Timer {
        id: defer
        interval: 0
        onTriggered: root.relayout()
    }

    Component.onCompleted: defer.restart()

    function relayout() {
        var kids = box.children
        var n = kids.length
        if (n === 0 || root.width <= 0) {
            root.layoutH = 0
            return
        }

        // —— 第一趟：按宽度切行 ——
        var lines = []
        var cur = []
        var x = 0
        var lineH = 0
        for (var i = 0; i < n; ++i) {
            var c = kids[i]
            var w = c.width > 0 ? c.width : (c.implicitWidth || 0)
            var h = c.height > 0 ? c.height : (c.implicitHeight || 0)
            if (cur.length > 0 && x + w > root.width) {
                lines.push({ items: cur, h: lineH, w: x - root.gap })
                cur = []
                x = 0
                lineH = 0
            }
            cur.push({ c: c, w: w, h: h })
            x += w + root.gap
            lineH = Math.max(lineH, h)
        }
        if (cur.length > 0)
            lines.push({ items: cur, h: lineH, w: x - root.gap })

        // —— 第二趟：行内摆放 ——
        var y = 0
        for (var li = 0; li < lines.length; ++li) {
            var L = lines[li]
            var used = L.w
            var cx = 0
            if (root.justify === "center")
                cx = Math.max(0, (root.width - used) / 2)
            else if (root.justify === "end")
                cx = Math.max(0, root.width - used)

            for (var j = 0; j < L.items.length; ++j) {
                var it = L.items[j]
                it.c.x = cx
                if (root.align === "stretch") {
                    it.c.y = y
                    it.c.height = L.h          // align-items: stretch
                } else if (root.align === "start") {
                    it.c.y = y
                } else {
                    it.c.y = y + Math.round((L.h - it.h) / 2)   // center（设计稿默认）
                }
                cx += it.w + root.gap
            }
            y += L.h + root.gap
        }
        root.layoutH = y - root.gap
    }
}
