pragma ComponentBehavior: Bound
// src/ui/qml/AssetsCompare.qml —— 页面私有：一致性对比（16:10 舞台 + 右侧信息列）
//
// 对照 webui/src/views/Assets.jsx:13-59（CompareFlat）+ styles/views.css:293-312（.dlist）
// + styles/views.css:898-942（.compare）：
//   栅格         minmax(0, 640px) minmax(220px, 1fr) / gap 16 / align stretch
//   .compare     r-md 10 / line-normal 边 1 / overflow hidden / 16:10 / max-width 640
//   --split      46%（拖动范围 4–96）
//   .cmp-a       clip-path: inset(0 0 0 var(--split))  —— 中线左侧显示「当前」
//   .handle      2px accent + 0 0 10px accent-glow
//   .handle .knob 22px 圆 / accent 底 / accent-fg 图标 / shadow-accent
//   角标 Tag     左上「基线 · 第 1 章」/ 右上「当前 · 第 3 章」（各 inset 8）
//   .dlist .drow 行内 padding 6px 2px / f12.5 / 行间 1px line-subtle / hover fill-hover
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string entityName: ""
    property real splitPct: 46            // --split
    signal rerunDiff()

    // 差异项：值里含「一致」走 ok，其余走 warn（jsx: v.includes('一致') ? ok : warn）
    property var diffs: [
        { k: "发型 / 服装", v: "一致", warn: false },
        { k: "灯罩裂纹长度", v: "变化（预期内）", warn: true },
        { k: "色彩基调", v: "一致", warn: false },
        { k: "构图重心", v: "偏移 2%（容差内）", warn: true }
    ]

    readonly property real gridGap: 16
    readonly property real minRight: 220
    readonly property real stageW: Math.max(120, Math.min(640, width - gridGap - minRight))
    readonly property real stageH: stageW * 10 / 16      // aspectRatio: 16 / 10
    readonly property real splitX: stageW * root.splitPct / 100
    readonly property real rightX: stageW + gridGap
    readonly property real rightW: Math.max(0, width - stageW - gridGap)
    readonly property real rightH: 2 + kv.implicitHeight + 8 + root.diffs.length * 32 + 8 + 24

    implicitHeight: Math.max(root.stageH, root.rightH)
    implicitWidth: 720
    color: "transparent"
    antialiasing: true

    // —— 舞台 ——
    Item {
        id: stage
        x: 0
        y: 0
        width: root.stageW
        height: root.stageH

        // 基线（第 1 章）
        AssetsArt {
            x: 0
            y: 0
            width: stage.width
            height: stage.height
            seed: 5
        }
        // 当前（第 3 章）—— 中线左侧才露出
        Item {
            id: clipA
            x: 0
            y: 0
            width: root.splitX
            height: stage.height
            clip: true
            AssetsArt {
                x: 0
                y: 0
                width: stage.width
                height: stage.height
                seed: 10
            }
        }

        // 中线辉光：0 0 10px var(--accent-glow)
        Rectangle {
            x: root.splitX - 5
            y: 0
            width: 12
            height: stage.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        Rectangle {                     // .handle 2px
            x: root.splitX
            y: 0
            width: 2
            height: stage.height
            color: ThemeBridge.colors["accent.primary"]
        }

        // .handle .knob 22px
        Item {
            x: root.splitX - 11
            y: (stage.height - 22) / 2
            width: 22
            height: 22
            Rectangle {                 // shadow-accent 光晕
                anchors.centerIn: parent
                width: 26
                height: 26
                radius: 13
                color: ThemeBridge.colors["shadow.accent"]
            }
            Rectangle {
                anchors.fill: parent
                radius: 11
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                anchors.centerIn: parent
                text: "⇄"                 // <Icon name="compare"> 12px
                color: ThemeBridge.colors["accent.primary.fg"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
        }

        AssetsTag {
            x: 8
            y: 8
            text: "基线 · 第 1 章"
        }
        AssetsTag {
            x: stage.width - 8 - width
            y: 8
            text: "当前 · 第 3 章"
            tone: "accent"
        }

        Rectangle {                     // 描边压在图像之上
            anchors.fill: parent
            radius: root.rMd
            color: "transparent"
            border.width: 1
            border.color: ThemeBridge.colors["line.normal"]
        }

        // 拖动中线：cursor ew-resize，范围 4–96
        MouseArea {
            id: dragArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            function moveTo(mx) {
                root.splitPct = Math.max(4, Math.min(96, (mx / stage.width) * 100))
            }
            onPressed: moveTo(dragArea.mouseX)
            onPositionChanged: if (dragArea.pressed) moveTo(dragArea.mouseX)
        }
    }

    // —— 右侧信息列（padding 2px 0 / gap 8）——
    Item {
        id: infoCol
        x: root.rightX
        y: 2
        width: root.rightW
        height: root.rightH

        AssetsKv {
            id: kv
            x: 0
            y: 0
            width: root.rightW
            rows: [
                { k: "对比对象", v: "外观基线 · 按章" },
                { k: "基线帧", v: "第 1 章 · 出场首秀" },
                { k: "当前帧", v: "第 3 章 · 风起" },
                { k: "检测项", v: "C1–C12 全过" }
            ]
        }

        Repeater {
            model: root.diffs
            delegate: Rectangle {
                id: drow
                required property var modelData
                required property int index
                x: 0
                y: kv.implicitHeight + 8 + drow.index * 32
                width: root.rightW
                height: 32                      // padding 6px 2px + 12.5px 行高 20
                color: drowMouse.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent"
                Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }

                Text {
                    id: dkey
                    x: 2
                    y: 0
                    width: Math.max(0, drow.width - 4 - dval.width - 8)
                    height: drow.height
                    verticalAlignment: Text.AlignVCenter
                    text: drow.modelData.k
                    elide: Text.ElideRight
                    color: ThemeBridge.colors["text.muted"]   // .dim
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12      // .drow f12.5px → 取整 12（QssBuilder 的 drow 同值）
                }
                Text {
                    id: dval
                    x: drow.width - 2 - width
                    y: 0
                    height: drow.height
                    verticalAlignment: Text.AlignVCenter
                    text: drow.modelData.v
                    color: drow.modelData.warn ? ThemeBridge.colors["status.warn"]
                                               : ThemeBridge.colors["status.ok"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
                Rectangle {                     // 行分隔发丝线（最后一行无）
                    visible: drow.index < root.diffs.length - 1
                    x: 0
                    y: drow.height - 1
                    width: drow.width
                    height: 1
                    color: ThemeBridge.colors["line.subtle"]
                }
                MouseArea {
                    id: drowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }
        }

        AssetsBtn {
            x: 0
            y: kv.implicitHeight + 8 + root.diffs.length * 32 + 8
            text: "仅重跑差异项"
            glyph: "↻"
            sm: true
            onPicked: root.rerunDiff()
        }
    }
}
