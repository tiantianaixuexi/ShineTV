// src/ui/qml/StoryboardCardHead.qml —— ui.css:182-191 的 .card-h / .card-title
//
// 冻结的 Card.qml 只提供 body（Column，margins 16），没有标题栏插槽，所以本页
// 的三张带标题卡片把这个区块作为 body 的**第一个子项**放进去。
//
// ⚠️ y 为什么是 -16：Card.qml 的 body 有 16px 上边距（= 设计稿 .card-b padding 16），
// 而 .card-h 是从**卡片顶边**就开始的（padding-top 12 + 标题行 21 + padding-bottom 12
// + 1px border-bottom = 46）。所以这一块要往上顶 16px 才能落回设计稿的位置。
// 正文起点随之落在 inner y = 46（.card-b 的 16px padding 已在 body 边距里）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string title: ""
    property string glyph: ""              // 设计稿 Card 的 icon（clapper/target/film），accent 色
    default property alias extra: extraBox.data

    x: 0
    y: -16                                 // 见文件头说明
    width: parent ? parent.width : 0
    height: 46
    color: "transparent"

    Text {
        id: iconGlyph
        x: 0
        y: 12
        width: 15                         // UI.jsx:57 Icon 15×15
        height: 15
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: root.glyph
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 11
        color: ThemeBridge.colors["accent.primary"]
    }

    Text {
        x: iconGlyph.width + 8             // .card-h { gap: var(--sp-2) } = 8
        y: 12
        text: root.title
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 13                // .card-title 13.5 → 13（docs 〇节字号档）
        font.weight: Font.DemiBold
        lineHeight: 1.6
        lineHeightMode: Text.FixedHeight
        color: ThemeBridge.colors["text.primary"]
    }

    Item {
        id: extraBox
        x: root.width - width              // spacer：靠右对齐
        y: 12 + (21 - height) / 2          // align-items: center（21 = 标题行高 13×1.6）
        width: childrenRect.width
        height: childrenRect.height
    }

    // .card-h { border-bottom: 1px solid var(--line-subtle) } —— 满幅，所以要往两边各出 16
    Rectangle {
        x: -16
        y: 45
        width: root.width + 32
        height: 1
        color: ThemeBridge.colors["line.subtle"]
    }
}
