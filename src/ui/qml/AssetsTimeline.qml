pragma ComponentBehavior: Bound
// src/ui/qml/AssetsTimeline.qml —— 页面私有：关联时间线（章刻度 + 事件 + 下方镜头/参考图）
//
// 对照 webui/src/views/Assets.jsx:62-125（RelTimeline）+ styles/views.css:765-847：
//   .tl            height 92 / margin 2px 10px 0
//   .tl .axis      top 44 / height 2 / line-normal
//   .tl .tick      top 38 / 2×14 / line-strong
//   .tl .tick-l    top 58 / f11 / text-muted / nowrap
//   .tl .stem      1.5px / line-normal（jsx 行内 top 30 / height 14）
//   .tl .ev        top 12 / 竖排 / gap 4 / hover → translateY(-2px)
//   .tl .ev .pin   10px 圆 + 0 0 0 3px bg-void + 0 0 0 4px line-normal
//     .ev.hot .pin accent 底 + 4px accent-glow 环 + 10px accent-glow 光晕
//   .tl .ev .lb    f10.5 / text-secondary（hot → accent w700）
//   .tl-below      gap 8 / wrap / padding 2px 10px 0；.cap f11 text-muted（margin-right 2）
//   刻度           第 1–6 章，pct(ch) = ((ch - 0.5) / 6) × 100%
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property int artSeed: 5                  // 当前实体的 seed（出处事件 / 参考图用）
    property var shots: ["S01", "S02", "S05"]
    // 参考图的四张：[3, 6, 9, 12].map(...)（Assets.jsx:110）
    property var refSeeds: [3, 6, 9, 12]
    property int chapterCount: 6
    signal eventPicked(string label)

    readonly property real sidePad: 10        // .tl margin 10 / .tl-below padding 10
    readonly property real tlH: 92
    readonly property real belowH: 38         // padding 2 + 内容 36
    readonly property real thumbW: 52         // 参考图 .art 52×36
    readonly property real thumbBoxH: 36

    implicitHeight: 2 + tlH + belowH
    implicitWidth: 900
    color: "transparent"
    antialiasing: true

    function pctOf(ch) {
        return ((ch - 0.5) / root.chapterCount) * 100
    }

    readonly property var events: [
        { ch: 1, label: "外观基线 ①", hot: false, seed: 5 },
        { ch: 3, label: "外观基线 ② · 当前", hot: true, seed: 10 },
        { ch: 4, label: "出处 · 出场", hot: false, seed: root.artSeed }
    ]

    // —— 时间轴本体 ——
    Item {
        id: tl
        x: root.sidePad
        y: 2
        width: root.width - root.sidePad * 2
        height: root.tlH

        Rectangle {                      // .axis
            x: 0
            y: 44
            width: tl.width
            height: 2
            radius: 1
            color: ThemeBridge.colors["line.normal"]
        }

        // 章刻度：第 1–6 章
        Repeater {
            model: root.chapterCount
            delegate: Item {
                id: tick
                required property int index
                x: tl.width * root.pctOf(tick.index + 1) / 100
                y: 0
                width: 2
                height: tl.height
                Rectangle {              // .tl .tick
                    y: 38
                    width: 2
                    height: 14
                    color: ThemeBridge.colors["line.strong"]
                }
                Text {                   // .tl .tick-l
                    y: 58
                    width: 60
                    x: -29               // 12px 文字的「translateX(-50%)」
                    horizontalAlignment: Text.AlignHCenter
                    text: "第 " + (tick.index + 1) + " 章"
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 11
                }
            }
        }

        // 事件
        Repeater {
            model: root.events
            delegate: Item {
                id: ev
                required property var modelData
                x: tl.width * root.pctOf(ev.modelData.ch) / 100 - ev.width / 2
                y: evMouse.containsMouse && !root.reduce ? 10 : 12
                width: Math.max(10, evLabel.implicitWidth)
                height: 10 + 4 + evLabel.implicitHeight   // pin 10 + gap 4 + 标签行
                Behavior on y {
                    NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
                }

                // .stem（jsx 行内 top 30 / height 14）
                Rectangle {
                    x: ev.width / 2 - 0.75
                    y: 30 - 12
                    width: 1.5
                    height: 14
                    color: ThemeBridge.colors["line.normal"]
                }

                // .pin：10px 圆 + 3px bg-void 环 + 4px 描边环（hot 换 accent-glow）
                Item {
                    id: pin
                    x: ev.width / 2 - 5
                    y: 0
                    width: 10
                    height: 10
                    Rectangle {          // hot 的 10px accent-glow 光晕
                        visible: ev.modelData.hot
                        anchors.centerIn: parent
                        width: 30
                        height: 30
                        radius: 15
                        color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.1)
                    }
                    Rectangle {          // 4px 外环
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        radius: 9
                        color: ev.modelData.hot
                               ? Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)
                               : ThemeBridge.colors["line.normal"]
                    }
                    Rectangle {          // 3px bg-void 环
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        radius: 8
                        color: ThemeBridge.colors["bg.void"]
                    }
                    Rectangle {          // 钉身
                        anchors.fill: parent
                        radius: 5
                        color: ev.modelData.hot ? ThemeBridge.colors["accent.primary"]
                                                : ThemeBridge.colors["text.muted"]
                    }
                }

                Text {                   // .lb
                    id: evLabel
                    x: 0
                    y: 14
                    width: ev.width
                    horizontalAlignment: Text.AlignHCenter
                    text: ev.modelData.label
                    color: ev.modelData.hot ? ThemeBridge.colors["accent.primary"]
                                            : ThemeBridge.colors["text.secondary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 10      // .lb f10.5px → 取整 10（.5 向下，同 .tag 11.5→11）
                    font.weight: ev.modelData.hot ? Font.DemiBold : Font.Normal
                }

                MouseArea {
                    id: evMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.eventPicked(ev.modelData.label)
                }
            }
        }
    }

    // —— 下方一行：绑定镜头 / 参考图 ——
    Item {
        id: below
        x: root.sidePad
        y: 2 + root.tlH + 2
        width: root.width - root.sidePad * 2
        height: root.thumbBoxH

        Text {                           // .cap 绑定镜头
            id: capShots
            x: 0
            y: (below.height - height) / 2
            text: "绑定镜头"
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 11
        }

        Row {
            id: shotRow
            x: capShots.width + 2          // .cap margin-right: 2px
            y: (below.height - height) / 2
            spacing: 8                      // .tl-below gap: 8px
            Repeater {
                model: root.shots
                delegate: Chip {
                    required property var modelData
                    height: 24               // jsx 行内 style height: 24
                    text: modelData
                    glyph: "▤"              // <Icon name="clapper" 11px>
                }
            }
        }

        Text {                           // .cap 参考图（margin-left 10）
            id: capRefs
            x: shotRow.x + shotRow.width + 8 + 10
            y: (below.height - height) / 2
            text: "参考图"
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 11
        }

        Row {
            id: refRow
            x: capRefs.x + capRefs.width + 8
            y: 0
            spacing: 8
            Repeater {
                model: root.refSeeds
                delegate: Rectangle {
                    id: refThumb
                    required property var modelData
                    width: root.thumbW        // 52
                    height: root.thumbBoxH    // 36
                    radius: root.rSm
                    color: "transparent"
                    clip: true
                    Art {
                        width: parent.width
                        height: parent.height
                        seed: refThumb.modelData + root.artSeed   // seed + cur.art
                    }
                }
            }
        }

        Text {                           // .tiny .dim（margin-left: auto）
            x: below.width - width
            y: (below.height - height) / 2
            text: "全部条目可点击"
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
        }
    }
}
