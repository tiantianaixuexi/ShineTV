// src/ui/qml/Table.qml —— 紧凑表格（ui.css:721-768 的 .table.compact）
//
// 表头 f11 w600 text-muted + letter-spacing .03em + 1px line-normal 下边线；
// 单元格上下 9 / 左右 12，1px line-subtle 下边线；hover 走 fill-hover，
// 选中行走 fill-selected + 左侧 2px accent 内嵌条。`.num` 列走等宽字体。
//
// QML 没有 <table>，这里用「显式列坐标」复刻：列宽由 headerWidths 给，
// 末列吃掉剩余宽度（等价 CSS 的 auto/1fr），单元格文字落在各自列的 +12 处
// —— 也就是把 CSS 的左右 padding 原样搬过来，而不是靠对齐凑数。
import QtQuick
import Shine 1.0

pragma ComponentBehavior: Bound

Ctl {
    id: root

    property var headers: []          // ["#", "镜号", "情绪", "时长", "状态"]
    property var rows: []             // [{ cells: [...], tone: "ok", num: true, sel: false }, ...]
    property var headerWidths: []     // 末列自动填满
    property int selectedIndex: -1

    readonly property int thH: 31     // 8 + 14(11px 行高) + 8 + 1(下边线)
    readonly property int tdH: 35     // 9 + 16(13px 行高) + 9 + 1(下边线)
    readonly property int cellPadX: 12
    readonly property int bodyFont: ThemeBridge.baseFontPx

    implicitHeight: thH + rows.length * tdH
    implicitWidth: 360

    color: "transparent"

    // 各列 x 起点（表头与单元格共用）
    readonly property var colX: {
        var xs = []
        var x = 0
        for (var i = 0; i < headers.length; ++i) {
            xs.push(x)
            x += (i < headerWidths.length ? headerWidths[i] : 60) + cellPadX * 2
        }
        return xs
    }

    // —— 表头 ——
    Item {
        id: head
        x: 0; y: 0
        width: root.width
        height: root.thH

        Repeater {
            model: root.headers
            delegate: Text {
                required property int index
                required property string modelData
                x: root.colX[index] + root.cellPadX
                y: 8
                height: 14
                verticalAlignment: Text.AlignVCenter
                text: modelData
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1 * 0.03      // letter-spacing: .03em ≈ 0.33px
            }
        }
        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1
            color: ThemeBridge.colors["line.normal"]
        }
    }

    // —— 表体 ——
    Item {
        id: body
        x: 0
        y: root.thH
        width: root.width
        height: root.rows.length * root.tdH

        Repeater {
            model: root.rows
            delegate: Item {
                id: tr
                required property int index
                required property var modelData

                x: 0
                y: tr.index * root.tdH
                width: root.width
                height: root.tdH

                Rectangle {
                    anchors.fill: parent
                    color: tr.modelData.sel ? ThemeBridge.colors["fill.selected"]
                         : (trMouse.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent")
                    Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }
                }
                // .sel 的 inset 2px 0 0 accent
                Rectangle {
                    visible: tr.modelData.sel
                    x: 0; y: 0
                    width: 2
                    height: parent.height
                    color: ThemeBridge.colors["accent.primary"]
                }
                Rectangle {
                    anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                    height: 1
                    color: ThemeBridge.colors["line.subtle"]
                }

                // 前四列：纯文本
                Repeater {
                    model: tr.modelData.cells
                    delegate: Text {
                        required property int index
                        required property string modelData
                        visible: index < root.headers.length - 1
                        x: root.colX[index] + root.cellPadX
                        width: root.headers.length > 1 ? (root.colX[index + 1] - root.colX[index] - root.cellPadX * 2) : 60
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: modelData
                        // .num 走等宽小字；镜号列是 accent + 700；其余 text-secondary
                        color: index === 1 ? ThemeBridge.colors["accent.primary"]
                             : (tr.modelData.sel ? ThemeBridge.colors["text.primary"] : ThemeBridge.colors["text.secondary"])
                        font.family: index === 0 || index === 3 ? monoFont : ThemeBridge.fontFamily
                        font.pixelSize: index === 0 || index === 3 ? 11 : root.bodyFont
                        font.weight: index === 1 ? Font.Bold : Font.Normal
                    }
                }

                // 末列：状态 Tag
                Tag {
                    x: root.colX[root.headers.length - 1] + root.cellPadX
                    anchors.verticalCenter: parent.verticalCenter
                    text: tr.modelData.label
                    tone: tr.modelData.tone
                }

                MouseArea {
                    id: trMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.selectedIndex = tr.index
                }
            }
        }
    }

    readonly property string monoFont: "Cascadia Code, JetBrains Mono, Consolas, monospace"
}
