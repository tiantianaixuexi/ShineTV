pragma ComponentBehavior: Bound
// src/ui/qml/AssetsAssetCard.qml —— 页面私有：总览网格里的一张资产卡
//
// 对照 webui/src/styles/views.css:708-741 + ui.css:174-204：
//   .asset-grid        repeat(auto-fit, minmax(210px, 1fr)) / gap 14
//   .asset-card        .card + hoverable + overflow hidden；on = accent-glow 边 + shadow-accent
//   .card              bg-panel / line-subtle 边 1 / r-md 10
//   .card.hoverable:hover  边 line-strong + shadow-1 + translateY(-2px)
//   .asset-grid .thumb      height 150px
//   .asset-card .abody      padding 9px 11px 11px
//   .asset-card .aname      f13 w700
//   .asset-card .ameta      margin-top 4 / f11 / text-muted
//
// ⚠️ 三条定位纪律：
//   1. hover 的 translateY(-2px) 绑在**卡片自身**上 —— 它必须不在任何定位器里，
//      所以根用 Repeater + 显式 x/y（见 Assets.qml 的 gridCols/rows 计算），
//      定位器接管 y 会和这一行抢 y（binding loop，卡片全叠在第一行）。
//   2. 卡片的 width/height 由**父 Item 显式给**，不给 anchors —— 一旦 anchors.fill
//      就接管 y，抬升会连整块一起弄没。
//   3. 缩略图用 Rectangle 的 topLeft/topRightRadius + 自己的 clip 取得
//      「overflow hidden + 顶部圆角」，卡片本体不需要 clip —— 这样 layer（阴影）
//      与 clip 不落在同一个 item 上。
import QtQuick
import QtQuick.Effects  // MultiEffect（hover shadow-1 / 选中 shadow-accent）
import Shine 1.0

Ctl {
    id: root

    property string entityId: ""
    property string entityName: ""
    property string entityKind: ""
    property string entityRole: ""
    property string statusLabel: ""
    property string statusTone: ""
    property int artSeed: 0
    property bool selected: false
    signal activated(string id)

    readonly property real thumbH: 150    // .asset-grid .thumb height: 150px
    readonly property real bodyPadX: 11   // .abody padding: 9px 11px 11px
    readonly property real nameRowH: 21   // 13px 字 × 1.6 行高
    readonly property real metaH: 18      // 11px 字 × 1.6 行高
    readonly property real bodyH: 9 + nameRowH + 4 + metaH + 11

    implicitHeight: thumbH + bodyH      // 150 + 63 = 213
    implicitWidth: 210                  // .asset-grid 的 minmax 下限
    radius: root.rMd                    // --r-md 10
    antialiasing: true

    color: ThemeBridge.colors["bg.panel"]
    border.width: 1
    border.color: root.selected ? Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)
                       : (area.containsMouse ? ThemeBridge.colors["line.strong"]
                                             : ThemeBridge.colors["line.subtle"])
    Behavior on border.color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }

    // .card.hoverable:hover { transform: translateY(-2px) }
    y: area.containsMouse && !root.reduce ? -2 : 0
    Behavior on y { NumberAnimation { duration: root.reduce ? 0 : root.durBase; easing.type: Easing.OutCubic } }

    layer.enabled: root.shadows && (area.containsMouse || root.selected)
    layer.effect: MultiEffect {
        shadowEnabled: (area.containsMouse || root.selected) && !root.reduce
        shadowBlur: 1.0
        shadowScale: 1.0
    }

    // 缩略图：150px 高，顶部两角 r-md（等价 overflow:hidden + 卡片的圆角裁切）
    Rectangle {
        id: thumb
        x: 0
        y: 0
        width: parent.width
        height: root.thumbH
        topLeftRadius: root.rMd
        topRightRadius: root.rMd
        color: "transparent"
        clip: true
        Art {
            width: thumb.width
            height: thumb.height
            seed: root.artSeed
            zoom: true
            zoomed: area.containsMouse
        }
    }

    // .abody
    Tag {
        id: statusTag
        x: root.width - root.bodyPadX - width
        y: root.thumbH + 9 + (root.nameRowH - height) / 2
        text: root.statusLabel
        tone: root.statusTone
        sm: true          // <Tag tone={st.tone} sm>（Assets.jsx:156）
    }

    Text {
        id: nameText
        x: root.bodyPadX
        y: root.thumbH + 9
        width: Math.max(0, statusTag.x - root.bodyPadX - 8)
        height: root.nameRowH
        verticalAlignment: Text.AlignVCenter
        text: root.entityName
        elide: Text.ElideRight
        color: ThemeBridge.colors["text.primary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 13      // .aname font-size: 13px
        font.weight: Font.DemiBold
    }

    Text {
        x: root.bodyPadX
        y: root.thumbH + 9 + root.nameRowH + 4     // .ameta margin-top: 4px
        width: root.width - root.bodyPadX * 2
        height: root.metaH
        verticalAlignment: Text.AlignVCenter
        text: root.entityKind + " · " + root.entityRole
        elide: Text.ElideRight
        color: ThemeBridge.colors["text.muted"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 11      // .ameta font-size: 11px
    }

    // 描边压在内容之上（QML 的子项会盖住父 Rectangle 的 border，CSS 不会）
    Rectangle {
        anchors.fill: parent
        radius: root.rMd
        color: "transparent"
        border.width: 1
        border.color: root.border.color
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.activated(root.entityId)
    }
}
