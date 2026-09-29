// src/ui/qml/GalleryCard.qml —— 设计稿 .card 的完整外壳：标题栏 + 内容区
//
// 面壳复用冻结的 Card.qml（bg-panel / line-subtle / r-md / hover 变色），
// 这里只补两件 Card.qml 不提供的事：
//
// 1. **标题栏 .card-h**（ui.css:182-188，p12 16 + 底部发丝线）。
//    Card.qml 的 body 是一条 margins 16 的 Column，它会接管**直接**子项的 y，
//    所以标题栏画在 body 内部的那个填满 Item 里，靠 y: -4 退回 16 → 12；
//    发丝线则由本组件自己锚在根上，才能横贯整卡（设计稿里它到卡片边缘）。
//
// 2. **内容区高度显式化**（contentH）。页面这一层不用任何定位器
//    （Flow/Row/Grid 会和显式坐标抢 y，见文件头纪律），卡片位置由
//    Gallery.qml 的行网格函数算出来，因此每张卡的内容高度必须先声明。
//
// `hoverable` 显式关掉：Gallery.jsx 里所有 <Card> 都没传 hover，
// 设计稿这几个卡片本来就不带 .hoverable —— 没有 translateY(-2px)、
// 没有阴影、hover 也不换边线。
import QtQuick
import Shine 1.0

Item {
    id: root

    property string title: ""
    property string icon: ""
    // .card-b 内容盒高度（不含标题栏与 16px 下内边距）
    property int contentH: 0

    default property alias content: bodyBox.data

    // .card-h = 12(上) + 16(13px 行高取整) + 12(下) + 1(发丝线)
    readonly property int headH: 41
    readonly property int padB: ThemeBridge.spaces["4"]        // 16

    implicitWidth: 320
    implicitHeight: headH + contentH + padB

    Card {
        anchors.fill: parent
        hoverable: false

        // ⚠️ 卡片正文只放这一个填满的 Item（Card.qml 的 body 是 Column，
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
            Item {
                id: bodyBox
                x: 0
                y: root.headH - ThemeBridge.spaces["4"]
                width: parent.width
                height: root.contentH
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
