pragma ComponentBehavior: Bound
// src/ui/qml/AssetsKv.qml —— 页面私有：.kv（键值两列表）
//
// 对照 webui/src/styles/ui.css:1015-1028：
//   .kv        grid auto 1fr / gap 6px 14px / f12.5
//   .kv .k     text-muted / nowrap
//   .kv .v     text-primary / w500 / ellipsis
//
// rows: [{ k: "类别", v: "人物 · 主角" }, …]（行高 12.5 × 1.6 ≈ 20，行间距 6）
//
// ⚠️ 第一列是 CSS 的 `auto`（= 各行 key 的最大宽度）。这里由每一行回报自己的
// 文本宽度取最大值，**不在绑定里赋值给探针** —— 绑定求值期间写 property 会让
// 依赖它的 implicitWidth 失效，直接打出 binding loop。
import QtQuick
import Shine 1.0

Item {
    id: root

    property var rows: []
    property real rowH: 20
    readonly property real rowGap: 6
    readonly property real colGap: 14
    // 第一列宽度：只增不减（rows 换掉后仍是旧的最大值，对本页两组固定 4 行无影响）
    property real keyW: 0

    function noteKeyWidth(w) {
        if (w > keyW) {
            keyW = w
        }
    }

    implicitHeight: rows.length > 0 ? rows.length * rowH + (rows.length - 1) * rowGap : 0
    implicitWidth: 200

    Repeater {
        model: root.rows
        delegate: Item {
            id: kvRow
            required property var modelData
            required property int index
            x: 0
            y: kvRow.index * (root.rowH + root.rowGap)
            width: root.width
            height: root.rowH

            Text {
                id: keyText
                x: 0
                y: 0
                width: root.keyW
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                text: kvRow.modelData.k
                color: ThemeBridge.colors["text.muted"]     // .kv .k
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12      // .kv f12.5px → 取整 12（同 QssBuilder 的 drow/tree）
                onImplicitWidthChanged: root.noteKeyWidth(keyText.implicitWidth)
            }
            Text {
                x: root.keyW + root.colGap                 // grid gap 6px 14px
                y: 0
                width: Math.max(0, root.width - root.keyW - root.colGap)
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                text: kvRow.modelData.v
                elide: Text.ElideRight                     // .v ellipsis
                color: ThemeBridge.colors["text.primary"]  // .kv .v
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
                font.weight: Font.Medium                  // 500
            }
        }
    }
}
