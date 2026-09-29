pragma ComponentBehavior: Bound
// src/ui/qml/AssetsCompare.qml —— 页面私有：一致性对比（16:10 舞台 + 右侧信息列）
//
// 数据源：**C++ 桥的真值**（ui/pages/assets/AssetPageModel.h 的 compareKv /
// diffRows / consistency.frames），取数逻辑在 ui/pages/assets/AssetVisualData.h
// （与 Widgets 的 ConsistencyView 共用同一份，只读 visual_states /
// character_status / shots / generated_images）。像素差异由 C++ 侧在 worker 上算。
// 迁移前本文件整块是写死的「第1章↔第3章 / C1–C12 全过」设计稿 mock。
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

    // —— 数据源：C++ 桥 AssetPageModel 的真值，全部来自只读真库 ——
    //   kvRows  [{key, value}]  对比对象 / 基线帧 / 当前帧 / 检测项
    //   diffRows[{key, value, changed}]  逐段对照基线，外观/色彩**分开判**
    //   frames  [{label, absPath, hasImage}]  同角色镜头帧（真实图片路径）
    //   verdict  结论文字（none/same/minor/significant 判定由 C++ 侧做）
    //   diffPct  像素差异百分比，worker 算出；空串 = 尚未比对
    // 迁移前这里是一整块写死的「第1章↔第3章 / C1–C12 全过 / 灯罩裂纹长度」mock。
    property var kvRows: []
    property var diffRows: []
    property var frames: []
    property string verdict: "未比对"
    property string diffPct: ""
    property bool hasFrames: false
    // 结论徽标 tone：与 C++ 的 severity 同一口径
    property string verdictTone: "idle"

    // 基线帧 / 当前帧：取前两张真实帧图；没有就用占位（并显示文案）
    readonly property var frameA: frames.length > 0 ? frames[0] : null
    readonly property var frameB: frames.length > 1 ? frames[1] : null

    readonly property real gridGap: 16
    readonly property real minRight: 220
    readonly property real stageW: Math.max(120, Math.min(640, width - gridGap - minRight))
    readonly property real stageH: stageW * 10 / 16      // aspectRatio: 16 / 10
    readonly property real splitX: stageW * root.splitPct / 100
    readonly property real rightX: stageW + gridGap
    readonly property real rightW: Math.max(0, width - stageW - gridGap)
    readonly property real rightH: 2 + kv.implicitHeight + 8 + root.diffRows.length * 32 + 8 + 24

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

        // 基线帧（第 1 张）
        Image {
            x: 0
            y: 0
            width: stage.width
            height: stage.height
            visible: root.frameA !== null
            source: root.frameA !== null ? "file:///" + root.frameA.absPath : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: Math.round(stage.width)
            sourceSize.height: Math.round(stage.height)
        }
        // 当前帧 —— 中线左侧才露出
        Item {
            id: clipA
            x: 0
            y: 0
            width: root.splitX
            height: stage.height
            clip: true
            Image {
                x: 0
                y: 0
                width: stage.width
                height: stage.height
                visible: root.frameB !== null
                source: root.frameB !== null ? "file:///" + root.frameB.absPath : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: Math.round(stage.width)
                sourceSize.height: Math.round(stage.height)
            }
        }

        // 无可比帧时的占位（真实提示，不画假图）
        Rectangle {
            anchors.fill: parent
            visible: !root.hasFrames
            color: ThemeBridge.colors["fill.muted"]
            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "同角色镜头图不足两张；已找到 "
                      + Page.consistency.frames.length + " 张。"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
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

        Tag {
            x: 8
            y: 8
            visible: root.frameA !== null
            text: root.frameA !== null ? "基线 · " + root.frameA.label : ""
            sm: true          // jsx: className="tag sm"（:35-36）
        }
        Tag {
            x: stage.width - 8 - width
            y: 8
            visible: root.frameB !== null
            text: root.frameB !== null ? "当前 · " + root.frameB.label : ""
            tone: "accent"
            sm: true          // jsx: className="tag accent sm"（:36）
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

        Kv {
            id: kv
            x: 0
            y: 0
            width: root.rightW
            visible: root.kvRows.length > 0
            // 键名是 Kv 的 {key, value}；行由 C++ 侧按真库行数生成
            rows: root.kvRows
        }

        Repeater {
            model: root.diffRows
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
                    text: drow.modelData.key
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
                    text: drow.modelData.value
                    // 值一律 status.ok，一变 status.warn（与 Widgets 侧同一口径）
                    color: drow.modelData.changed ? ThemeBridge.colors["status.warn"]
                                                  : ThemeBridge.colors["status.ok"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
                Rectangle {                     // 行分隔发丝线（最后一行无）
                    visible: drow.index < root.diffRows.length - 1
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

        // 「仅重跑差异项」：Widgets 侧刻意没做（无真实后端），
        // 这里保留按钮但点下去只做一次重投影，不假装有 diff 服务。
        Button {
            x: 0
            y: kv.implicitHeight + 8 + root.diffRows.length * 32 + 8
            text: root.diffPct !== "" ? "像素差异 " + root.diffPct + "%" : "仅重跑差异项"
            glyph: "↻"
            sm: true
            onClicked: root.rerunDiff()
        }
    }
}
