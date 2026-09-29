// src/ui/qml/GalleryFlow.qml —— 可换行的横排（views.css:401-406 的 .gal-row）
//
//   .gal-row { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
//
// ⚠️ `flex-wrap: wrap` 是设计稿明确写的，所以横排**放不下必须换行，不能溢出卡片**。
//    QML 的 `Row` 不换行（`Flow` 会重排子项从而和子项自己的 x/y 打架，而 Button
//    的 :active scale 之类又要求子项位置稳定），所以这里自己做一遍两趟布局：
//    第一趟按宽度切行并记每行高度，第二趟把每个子项按 align-items:center 放进行内。
//
//    位置是**赋值**而不是绑定 —— 行内元素没有 hover 位移，不构成 binding loop。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // .gal-row gap 10
    property int gap: 10

    default property alias content: box.data

    // relayout() 从 JS 里写它，所以不能是 readonly
    property real layoutH: 0

    implicitWidth: box.childrenRect.width
    implicitHeight: layoutH

    color: "transparent"

    Item {
        id: box
        width: parent.width
    }

    onWidthChanged: defer.restart()
    onGapChanged: defer.restart()
    Component.onCompleted: defer.restart()

    // 子项的 implicitWidth 在 onCompleted 那一轮往往还没算完，
    // 所以延到事件循环之后再排一遍。
    Timer {
        id: defer
        interval: 0
        onTriggered: root.relayout()
    }

    function relayout() {
        var kids = box.children
        var n = kids.length
        if (n === 0 || root.width <= 0) {
            root.layoutH = 0
            return
        }
        var lines = []
        var cur = []
        var x = 0
        var lineH = 0
        for (var i = 0; i < n; ++i) {
            var c = kids[i]
            var w = c.width > 0 ? c.width : (c.implicitWidth || 0)
            var h = c.height > 0 ? c.height : (c.implicitHeight || 0)
            if (cur.length > 0 && x + w > root.width) {
                lines.push({ items: cur, h: lineH })
                cur = []
                x = 0
                lineH = 0
            }
            cur.push({ c: c, w: w, h: h })
            x += w + root.gap
            lineH = Math.max(lineH, h)
        }
        if (cur.length > 0) lines.push({ items: cur, h: lineH })

        var y = 0
        for (var li = 0; li < lines.length; ++li) {
            var L = lines[li]
            var cx = 0
            for (var j = 0; j < L.items.length; ++j) {
                var it = L.items[j]
                it.c.x = cx
                it.c.y = y + Math.round((L.h - it.h) / 2)     // align-items: center
                cx += it.w + root.gap
            }
            y += L.h + root.gap
        }
        root.layoutH = y - root.gap
    }
}
