pragma ComponentBehavior: Bound
// src/ui/qml/AssetsSecHead.qml —— 页面私有：.vsec-h（详情分区标题条）
//
// 对照 webui/src/styles/views.css:754-763：
//   .vsec-h        flex / align-items center / gap 9 / min-height 26
//   .vsec-h .t     f13.5 w700
//   行内小字       .tiny .dim（f12 / text-muted）
//   尾部控件       Segmented / Button（与前面元素同一个 gap 9）
//
// ⚠️ 高度是内容撑出来的：设计稿写的是 min-height 26，但 设定集 那一节的尾部有
// Segmented（34px），实际行高 = 34。这里不写死高度，取 max(26, 尾部最高控件)。
//
// ⚠️ 前导区（图标 / 标题 / Tag / 小字）**全部显式 x**，不用 Row：定位器对不可见
// 子项是否仍留 gap 并不确定，而设计稿的 gap 9 是固定值 —— 「关联时间线」那一节
// 没有 Tag，用 Row 会在标题和小字之间凭空多出 9 + 空胶囊宽的空洞。尾部控件由
// 一条 Row 摆放（它们全都不做 y 位移，且调用方要么给全、要么一个不给）。
import QtQuick
import Shine 1.0

Item {
    id: root

    default property alias trailing: trailRow.data

    property string glyph: ""
    property string title: ""
    property string meta: ""
    property string tagText: ""
    property string tagTone: ""

    readonly property real gap: 9
    readonly property real glyphW: glyph === "" ? 0 : 15     // .vsec-h 里的 Icon 15px
    readonly property real titleX: glyphW > 0 ? glyphW + gap : 0
    readonly property real tagX: titleX + titleText.implicitWidth + gap
    readonly property bool hasTag: tagText !== ""
    readonly property real metaX: (hasTag ? tagX + tag.width : titleX + titleText.implicitWidth)
                                   + gap
    readonly property real leadingW: metaX + (meta === "" ? 0 : metaText.implicitWidth)

    implicitHeight: Math.max(26, trailRow.implicitHeight)
    implicitWidth: leadingW + (trailRow.implicitWidth > 0 ? gap + trailRow.implicitWidth : 0)

    Text {
        id: glyphText
        x: 0
        y: (root.height - height) / 2
        visible: root.glyph !== ""
        width: root.glyphW
        text: root.glyph
        color: ThemeBridge.colors["accent.primary"]   // --accent 字符图标
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 15        // .vsec-h 里的 Icon 15px
    }
    Text {
        id: titleText
        x: root.titleX
        y: (root.height - height) / 2
        text: root.title
        color: ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 13      // .vsec-h .t font-size: 13.5px → 取整 13（与 QssBuilder 的 vsechead 同值）
        font.weight: Font.DemiBold
    }
    AssetsTag {
        id: tag
        visible: root.hasTag
        x: root.tagX
        y: (root.height - height) / 2
        text: root.tagText
        tone: root.tagTone
    }
    Text {
        id: metaText
        visible: root.meta !== ""
        x: root.metaX
        y: (root.height - height) / 2
        text: root.meta
        color: ThemeBridge.colors["text.muted"]   // .tiny .dim
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12      // .tiny font-size: 12px
    }

    Row {
        id: trailRow
        x: Math.max(root.leadingW + root.gap, root.width - trailRow.implicitWidth)
        y: (root.height - trailRow.implicitHeight) / 2
        spacing: root.gap
    }
}
