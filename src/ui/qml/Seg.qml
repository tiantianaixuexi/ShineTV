pragma ComponentBehavior: Bound   // delegate 里引用 root，必须显式绑定（pragma 必须在 import 之前）
// src/ui/qml/Seg.qml —— 共享分段控件（ui.css:207-242 的 .seg）
//
// ================================ 合并来源：三份手搓实现收成一份 ================================
//   GallerySeg.qml    : options: string[] + current: int（选中下标）
//   AssetsSeg.qml     : options: [{value,label}] + value: string + signal picked(string)
//   ImageFlowSeg.qml  : options: [{value,label}] + value: string + signal picked(string)
//   → 统一到**对象数组 + 字符串 value + picked 信号**。字符串 value 是真值（选项的语义身份
//     不随排序变），下标是位置身份，重排一次就全错。
//     `current: int` 那一套本页还没改（current 归页面所有），由页面 Agent 在下一轮迁移；
//     本文件不读 current，页面用 [].indexOf(...) 换成 value 即可，不碰页面文件。
// =================================================================================================
//
// 设计稿（webui/src/styles/ui.css:207-242）：
//   .seg                { display:inline-flex; padding:3px; gap:2px; background:var(--fill-muted);
//                         border:1px solid var(--line-subtle); border-radius:var(--r-sm) }  ← 207-214
//   .seg > button       { height:26px; padding:0 13px; border-radius:var(--r-xs);
//                         font-size:12.5px; font-weight:600; color:var(--text-muted);
//                         transition: background-color var(--dur-1) var(--ease),
//                                     color var(--dur-1) var(--ease) }                 ← 215-224
//   .seg > button:hover { color: var(--text-primary) }                                 ← 225-227
//   .seg > button.on    { background:var(--bg-elevated); color:var(--text-primary);
//                         box-shadow: var(--shadow-1) }                                ← 228-232
//   .seg > button.on::after { content:""; display:inline-block; width:4px; height:4px;
//                         border-radius:50%; background:var(--accent);
//                         margin-left:6px; vertical-align:2px }                       ← 233-242
//
// 几何（border-box，1px 边画在盒内不额外占位）：
//   外框高 = 3(pad) + 26(选项) + 3(pad) + 1×2(上下边) = 34
//   外框宽 = 行内各项宽之和 + gap×(n-1) + pad 3×2 + 边 1×2
//   选项宽 = 13(p0 左) + 文字 advance + 10(::after) + 13(p0 右)
//            ├ 未选中：文字 + 26
//            └ 选中  ：文字 + 36  ← 26 + margin-left 6 + 圆点 4
//   ⚠️ 三份旧实现这里不一致：AssetsSeg 算 26+6+4=36（对），ImageFlowSeg 算 26+6+4+**6**=42
//      （尾部多一个 6，会让选中段比 CSS 宽 6px 且文字整体左移），GallerySeg 干脆不扩宽、
//      圆点直接溢出段外。统一到上面那条算式 = 文字 + 36。
//   字号 12.5→12（就近取整，与 kit/controls 的 seg 同档）；段高 26、r-xs 4、外框 r-sm 6。
//
// ⚠️ 选中圆点**必须显式定位**（x = 标签 x + 文字 advance + 6，y = 垂直居中 + 2），
//    绝不能套 Row：Row 会把 ::after 的 6px gap 算进**每个**段的布局宽度，未选中段凭空
//    多出 6+4 的宽度。旧 ImageFlowSeg 正是这么写的。
//
// ⚠️ layer.enabled 必须带 root.shadows：ThemeBridge 探测不到场景图后端时（software）
//    挂 layer 的 item 会被**整个吞掉** —— 不是阴影没画出来，是选中段整块消失。
//    写法固定为 layer.enabled: root.shadows && <条件>，不要拆开写。
//
// ⚠️ --shadow-1（tokens.css:67/108/149/190/243）是**两层**阴影
//    （0 1px 2px + 0 4px 16px），QtQuick.Effects 的 MultiEffect 只承载一层。
//    这里只复刻近距那一层（shadowBlur 1.0，与三份旧实现一致），16px 的那层远投影
//    不复刻 —— 硬塞会得到一层错误的大糊边，不如少画一层。属于设计稿之外的已知缺口。
//
// ⚠️ 本组件**不改** value：只有 picked 信号，值归调用方。组件内自改值会出现「谁是真值」
//    的双写，页面一旦自己维护一份就会和内部那份分叉。
import QtQuick
import QtQuick.Effects  // MultiEffect（.seg > button.on 的 --shadow-1）
import Shine 1.0

Ctl {
    id: root

    // [{ value: "detail", label: "详情" }, …]
    property var options: []
    property string value: ""   // 当前选中的 value
    signal picked(string v)

    implicitHeight: 34                                    // 3 + 26 + 3 + 1×2
    implicitWidth: inner.implicitWidth + 8                 // pad 3×2 + 边 1×2

    radius: root.rSm                                      // --r-sm 6
    color: ThemeBridge.colors["fill.muted"]
    border.width: 1
    border.color: ThemeBridge.colors["line.subtle"]

    Row {
        id: inner
        anchors.centerIn: parent
        spacing: 2                                        // .seg { gap: 2px }

        Repeater {
            model: root.options
            delegate: Rectangle {
                id: seg
                required property var modelData
                readonly property bool on_: root.value === seg.modelData.value

                // p0 13 两侧 + 文字；选中段再加 ::after 的 margin-left 6 + 圆点 4
                width: segLabel.implicitWidth + 26 + (seg.on_ ? 6 + 4 : 0)
                height: 26
                radius: ThemeBridge.radii.xs              // --r-xs 4
                color: seg.on_ ? ThemeBridge.colors["bg.elevated"] : "transparent"
                Behavior on color {
                    ColorAnimation { duration: root.reduce ? 0 : root.durFast }   // --dur-1 = 120ms
                }

                // .seg > button.on { box-shadow: var(--shadow-1) }
                // ⚠️ 必须带 root.shadows —— software 场景图后端下挂 layer 的 item 会被吞掉
                layer.enabled: root.shadows && seg.on_ && !root.reduce
                layer.effect: MultiEffect {
                    shadowEnabled: seg.on_ && !root.reduce
                    shadowBlur: 1.0
                    shadowScale: 1.0
                }

                Text {
                    id: segLabel
                    x: 13                                              // padding: 0 13
                    y: (seg.height - height) / 2
                    text: seg.modelData.label
                    color: seg.on_ ? ThemeBridge.colors["text.primary"]
                         : (segMouse.containsMouse ? ThemeBridge.colors["text-primary"]
                                                   : ThemeBridge.colors["text.muted"])
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12       // .seg > button f12.5px → 就近取整 12
                    font.weight: Font.DemiBold
                    Behavior on color {     // transition: color var(--dur-1)
                        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                    }
                }

                // .seg > button.on::after：4px accent 圆点，margin-left 6 / vertical-align 2
                // ⚠️ 显式定位，不套 Row —— 套 Row 会让未选中段凭空多出 6 + 4 的宽度
                Rectangle {
                    visible: seg.on_
                    x: segLabel.x + segLabel.implicitWidth + 6   // margin-left: 6px
                    y: (seg.height - height) / 2 + 2              // vertical-align: 2px
                    width: 4
                    height: 4
                    radius: 2
                    color: ThemeBridge.colors["accent.primary"]
                }

                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.picked(seg.modelData.value)
                }
            }
        }
    }
}
