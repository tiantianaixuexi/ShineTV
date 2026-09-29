// src/ui/qml/IconBtn.qml —— 共享纯图标钮：ui.css:88-117 的 .icon-btn / .icon-btn.sm
//
// ================================ 合并来源（2 份 → 1 份） ================================
//   · GalleryIconBtn.qml    active 档（.icon-btn.active）+ tip 属性 + accent 字色
//   · ImageFlowIconBtn.qml  sm 尺寸档（22 / 13）+ clicked 纯发信号 + 22/13 的图标盒
// 差异 → 属性：`icon` 统一改名 `glyph`（三个共享件的叫法一致）；其余两处差异原本就是
// 「有没有 active」「是不是 sm 档」，现在都是属性 —— 两份实现的其余行为完全同构。
// ======================================================================================
//
// 设计稿（webui/src/styles/ui.css，行号即真值）：
//   .icon-btn              28 × 28 / r-sm / 字 --text-secondary /
//                          transition: background-color + color（--dur-1）        :88-97
//   .icon-btn:hover        底 --fill-hover / 字 --text-primary                     :98-101
//   .icon-btn.active       底 --fill-selected / 字 --accent                        :102-105
//   .icon-btn.sm           22 × 22                                                 :106-109
//   .icon-btn .icon        16 × 16                                                 :110-113
//   .icon-btn.sm .icon     13 × 13                                                 :114-117
//
// ⚠️ **active 与 hover 同时成立时 active 赢**，这是从 CSS 推出来的，不是拍脑袋：
//   `:hover`（:98-101）和 `.active`（:102-105）特异度相同（都是「一个类 + 一个伪类/类」），
//   按层叠顺序后者胜，而 `.active` 写在后面。旧 GalleryIconBtn.qml:22-23/31-32 的
//   `active ? … : (hover ? …)` 结构与这条一致，本组件原样保留。
//
// ⚠️ 设计稿**没有** `:active` 按下态（无 scale、无边框），所以本组件也不加 —— 不要照抄
//   Button 的 scale(0.97)。同样，`.icon-btn` 整条规则里没有 border，Rectangle 的默认
//   border.width 是 0，别手滑写上 `border.width: 1`（那正是旧 GalleryIconBtn 的
//   "active 态多画了一圈边" 被判为 bug 的那类漂移，见 gaps 文档第 351 行）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 对外 API ——
    // 字符图标占位。设计稿是内联 SVG（Icon.jsx），本仓不引外部图标资源，kit 约定用字符。
    // ⚠️ 旧两份实现里这个属性叫 `icon`，共享层统一叫 `glyph`（与 Button / Chip 对齐），
    //    调用点（Gallery.qml:282-284、ImageFlow.qml:299-301/433）在页面迁移时一并改名。
    property string glyph: ""
    property bool sm: false                 // .icon-btn.sm 22px / .icon-btn 28px
    property bool active: false
    // tooltip 文本。⚠️ **纯数据，本组件不画 tooltip**：设计稿靠 `[data-tip]::after` 伪元素
    // （CSS 没有能直接对应的东西），旧 GalleryIconBtn.qml:6-7 同样把 tooltip 留给宿主。
    // 留着这个属性是为了让页面有一处统一的地方取文案，不是为了在这里弹窗。
    property string tip: ""
    signal clicked()

    // —— 尺寸档（ui.css:92-93 / :107-108 / :111-112 / :115-116）——
    readonly property real box: sm ? 22 : 28
    // ⚠️ 字号按 CSS 的 **icon 盒尺寸**（16 / 13）。旧 StoryboardButton.qml:87 那种
    // 「盒 15 → 字 10」的字形内缩放在这里**没有依据**：盒本身就是 16，照盒出。
    readonly property int iconPx: sm ? 13 : 16

    implicitWidth: box
    implicitHeight: box
    // ⚠️ 同时给 width / height：旧 ImageFlowIconBtn.qml:16-17 就是这么写的，且它的调用点
    // （ImageFlow.qml:299-301）把控件直接丢进 Row —— 定位器会**跳过 width 为 0 的子项**，
    // 只给 implicitWidth 的话行内图标钮会当场塌成 0 宽。调用方给 width/anchors 会顶掉
    // 这两个绑定，行为与 GalleryIconBtn 的 implicit 版一致。
    width: box
    height: box

    radius: root.rSm                        // --r-sm 6（ui.css:94）
    // ⚠️ Ctl 是 Rectangle，color 默认**白色**；这里三个分支都给了值，不会露出白底方块。
    color: root.active ? ThemeBridge.colors["fill.selected"]                       // ui.css:103
         : (area.containsMouse ? ThemeBridge.colors["fill.hover"]              // ui.css:99
                                    : "transparent")                                // .icon-btn 无底色
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    // .icon-btn 是 `display:inline-flex` + align/justify center（ui.css:89-91），
    // 所以图标是**在盒里居中**的，盒宽与图标宽是两件事。
    Text {
        anchors.centerIn: parent
        text: root.glyph
        color: root.active ? ThemeBridge.colors["accent.primary"]                    // ui.css:104
             : (area.containsMouse ? ThemeBridge.colors["text.primary"]          // ui.css:100
                                        : ThemeBridge.colors["text.secondary"])       // ui.css:95
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.iconPx          // .icon-btn .icon 16 / sm 13
        // 字色也在 transition 列表里（ui.css:96）
        Behavior on color {
            ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
        }
    }

    // ⚠️ 这里**只发信号，不改自己的 active**。旧 GalleryIconBtn.qml:39 写的是
    // `onClicked: root.active = !root.active`（组件自翻转），合并时删掉了：active 已经是
    // 公开属性，调用方一旦也绑定它，调用方的赋值和这里的自翻转会互相打架
    // （点一下翻两次，或绑定直接被内部改掉）。active 归调用方，本组件只负责发 clicked()。
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
