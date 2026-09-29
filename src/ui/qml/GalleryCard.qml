// src/ui/qml/GalleryCard.qml —— 设计稿 .card 的完整外壳：标题栏 + 内容区
//
// 面壳复用冻结的 Card.qml（bg-panel / line-subtle / r-md），这里只补三件
// Card.qml 不提供的事：
//
// 1. **标题栏 .card-h**（ui.css:182-188：flex / align-items center / gap 8 /
//    padding 12 16 / border-bottom 1）与**内容区 .card-b**（padding 16）。
//    ⚠️ 这两套内边距不一样，所以本组件给 Card 传 `bodyPad: 0`，自己按设计稿排版。
//    旧版让 Card 的 margins:16 兜着，标题栏实际内边距变成 16+12，要退回 12 就得写
//    `y: -4`；发丝线同时写 `anchors.top` 和 `y:`，两者互斥、anchors 赢，线被画到卡片
//    **最上沿**与边框重叠——出图里「标题下面那条水平框」就是这样消失的。
//    图标与标题的横向关系交给共享 Flex 算，不再手摆 x/y。
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

    // —— 盒模型（ui.css:182-195）——
    //   .card-h  padding 12 16 + 内容行 16 + border-bottom 1  → headH = 12+16+12+1 = 41
    //   .card-b  padding 16
    // ⚠️ 所有位置都从这几个数推出来，**不再有 `y: -4` 这类补偿值**。旧版把 .card-h
    //    画在 Card 那条 margins:16 的 body Column 里，于是标题栏实际内边距变成了
    //    16+12，要退回 12 就得写 y:-4；发丝线更惨，它同时写了 `anchors.top` 和
    //    `y: headH-1`，两者互斥、anchors 赢，线被画到**卡片最上沿**和边框叠在一起，
    //    于是「标题下面那条水平框」在出图里根本看不见。现在 bodyPad: 0，两套内边距
    //    各自按设计稿摆，没有一层压一层。
    readonly property int headPadV: ThemeBridge.spaces["3"]      // 12
    readonly property int headPadH: ThemeBridge.spaces["4"]      // 16
    readonly property int headContentH: 16                        // 13.5px 字号的行盒取整
    readonly property int ruleH: 1                                // border-bottom
    readonly property int headH: headPadV + headContentH + headPadV + ruleH
    readonly property int padB: ThemeBridge.spaces["4"]          // 16  (.card-b padding)

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
    implicitHeight: headH + padB + bodyH + padB

    Card {
        anchors.fill: parent
        hoverable: false
        // 本组件自己按 .card-h / .card-b 排版，不要 Card 再套一层 16（见上面注释）
        bodyPad: 0

        Item {
            id: slot
            width: parent.width
            height: parent.height

            // —— .card-h：display:flex / align-items:center / gap:8 / padding:12 16 ——
            // 交给共享 Flex 排，图标与标题的相对位置由它算，不手摆坐标。
            Flex {
                id: head
                x: root.headPadH
                y: root.headPadV
                width: parent.width - root.headPadH * 2
                height: root.headContentH
                gap: ThemeBridge.spaces["2"]          // .card-h gap 8
                align: "center"                        // align-items: center

                GalleryIcon {
                    width: 15
                    height: 15
                    visible: root.icon !== ""
                    name: root.icon
                    glyphColor: ThemeBridge.colors["accent.primary"]
                }
                Text {
                    text: root.title
                    color: ThemeBridge.colors["text.primary"]
                    font.family: ThemeBridge.fontFamily
                    // .card-title 13.5px → 就近取整 13（与 body 同号，设计稿也只靠 600 字重区分）
                    font.pixelSize: ThemeBridge.baseFontPx
                    font.weight: Font.DemiBold
                }
            }

            // —— .card-h 的 border-bottom：横贯整卡（到卡片左右边缘）——
            // ⚠️ 只用 anchors.left/right，**不要再写 y**：anchors 与显式 y 互斥，
            //    同时写会让 y 被静默丢弃、线跑到卡片上沿（旧版就是这么把这条线弄丢的）。
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                y: root.headH - 1
                height: root.ruleH
                color: ThemeBridge.colors["line.subtle"]
            }

            // —— .card-b：padding 16 ——
            Loader {
                id: loader
                x: root.padB
                y: root.headH + root.padB
                width: Math.max(0, parent.width - root.padB * 2)
                sourceComponent: root.body
            }
        }
    }
}
