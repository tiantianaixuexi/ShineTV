// src/ui/qml/GalleryCard.qml —— 设计稿 .card 的完整外壳：标题栏 + 内容区
//
// 面壳复用冻结的 Card.qml（bg-panel / line-subtle / r-md），这里只补三件
// Card.qml 不提供的事：
//
// 1. **标题栏 .card-h**（ui.css:182-188，p12 16 + 底部发丝线）。
//    Card.qml 的 body 是一条 margins 16 的 Column，它会接管**直接**子项的 y，
//    所以标题栏画在 body 内部的那个填满 Item 里，靠 y: -4 退回 16 → 12；
//    发丝线则由本组件自己锚在根上，才能横贯整卡（设计稿里它到卡片边缘）。
//
// 2. **内容区高度由内容推导，不是页面声明的**。
//    ⚠️ 旧版这里是 `property int contentH`，页面在 `cards` 数组里给 9 张卡各写死
//    一个高度常数（112 / 76 / 147 / 234 …）。那是错的：内容比常数高就溢出、
//    **压住下一行的卡片**（实测：Button 卡压住 Field 卡标题，中间一道黑带），
//    比常数矮就留一块死白。共享组件一改尺寸这种错只会更多。
//    现在 `body` 收一个 Component，Loader 加载后直接读 `item.implicitHeight` ——
//    页面只要保证组件正文是带 implicitHeight 的 Item/Column，**一个高度数字都不用写**。
//
// 3. `hoverable` 显式关掉：Gallery.jsx 里所有 <Card> 都没传 hover，
//    设计稿这几个卡片本来就不带 .hoverable —— 没有 translateY(-2px)、
//    没有阴影、hover 也不换边线。
//
// ⚠️ 正文 Component 的根**必须是 Item 派生**且有 implicitHeight（Column 最省事）。
//    页面层不再定位本组件的位置与高度 —— 那由共享 AutoGrid.qml 负责。
import QtQuick
import Shine 1.0

Item {
    id: root

    property string title: ""
    property string icon: ""
    // 卡片正文。根对象需提供 implicitHeight。
    property Component body: null

    // .card-h = 12(上) + 16(13px 行高取整) + 12(下) + 1(发丝线)
    readonly property int headH: 41
    readonly property int padB: ThemeBridge.spaces["4"]        // 16

    // 正文实测高度。
    //
    // ⚠️ 这里读的是 `loader.childrenRect.height` 而不是 `loader.item.implicitHeight`：
    //    `Loader.item` 的静态类型是 QObject（Loader 可以加载非 Item 的东西），
    //    直接访问 implicitHeight 会被 qmllint 报
    //    `Member "implicitHeight" not found on type "QObject"`；
    //    显式声明成 Item 又会变成 `Cannot assign binding of type QObject to QQuickItem`。
    //    `childrenRect` 是 Loader 自己的 Item 属性，类型干净。
    //
    // ⚠️ 因此**每个正文 Component 的根都必须自己写 `height: implicitHeight`**：
    //    Loader 会接管「没有显式尺寸」的子项，若正文不钉住高度，Loader.height ← bodyH
    //    ← childrenRect ← item.height ← Loader.height 就是一条绑定环，高度恒为 0。
    //    本组件所有正文根都是 Column/单控件，这条是它们的固定写法。
    readonly property int bodyH: Math.ceil(loader.childrenRect.height)

    implicitWidth: 320
    implicitHeight: headH + bodyH + padB

    Card {
        anchors.fill: parent
        hoverable: false

        // ⚠️ 卡片面壳只放这一个填满的 Item（Card.qml 的 body 是 Column，
        //    直接子项的 y 由它接管；套一层 Item 后 y 由我们自己说了算）
        Item {
            id: slot
            width: parent.width
            height: parent.height

            // —— 标题栏内容：绝对 y = 16(body 上边距) + (-4) = 12 ——
            GalleryIcon {
                id: headIcon
                x: ThemeBridge.spaces["4"]
                y: -4
                width: 15
                height: 15
                visible: root.icon !== ""
                name: root.icon
                glyphColor: ThemeBridge.colors["accent.primary"]
            }
            Text {
                x: headIcon.visible ? headIcon.x + 15 + ThemeBridge.spaces["2"]
                                   : ThemeBridge.spaces["4"]
                y: -4
                height: 16
                verticalAlignment: Text.AlignVCenter
                text: root.title
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                // .card-title 13.5px → 就近取整 13（与 body 同号，设计稿也只靠 600 字重区分）
                font.pixelSize: ThemeBridge.baseFontPx
                font.weight: Font.DemiBold
            }

            // —— 内容区起点：绝对 y = 16 + 25 = 41 = headH ——
            // ⚠️ 不写 height：高度由 root.bodyH（= childrenRect.height）驱动，
            //    若这里也绑一次就会和子项高度构成环（见上方注释）。
            Loader {
                id: loader
                x: 0
                y: root.headH - ThemeBridge.spaces["4"]
                width: parent.width
                sourceComponent: root.body
            }
        }
    }

    // .card-h 底部发丝线（横贯整卡，所以锚在根上而不是 body 里）
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        y: root.headH - 1
        height: 1
        color: ThemeBridge.colors["line.subtle"]
    }
}
