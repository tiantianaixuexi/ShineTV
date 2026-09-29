// src/ui/qml/StoryboardDialog.qml —— ui.css:932-976 的 .scrim / .modal / .modal-h / .modal-b / .modal-f
//           + ui.css:278-329 的 .field / .input / .textarea / .select（Storyboard.jsx:184-222）
//
// .scrim     { fixed inset 0 · var(--scrim) · blur(4px) · fade-in dur-2 }
// .modal     { bg-overlay · line-normal 边 · r-lg · shadow-2 · min(560, 100vw-48) · overflow hidden }
//             本页 inline width: 460（Storyboard.jsx:186）
// .modal-h   { p14 18 · gap 10 · border-bottom line-subtle }  标题 f15 w700 + accent 图标
// .modal-b   { p18 }   .modal-f { p12 18 · gap 8 · border-top line-subtle }
// .field     { column · gap 6 }   label f12 w600 text-secondary   help = .tiny.dim
// .input     { h30 p0 10 r-sm · fill-muted 底 · line-normal 边
//              hover line-strong · focus line.focus 边 + 3px accent-dim 环 + bg-surface 底 }
// .textarea  { p8 10 · r-sm · line-height 1.6 }   .select { r-sm h30 · 右内距 26 }
//
// ⚠️ 三处替代：
//  1. `backdrop-filter: blur(4px)` 在 QML 无解（docs 第一节：唯一真能力边界），只留遮罩。
//  2. `.modal { overflow: hidden }` 会把 Select 的下拉层裁掉（设计稿自身就裁），
//     QML 侧把「情绪」做成**点击轮换下一项**（见 moodBox）而不是弹层：不凭空发明
//     一套设计稿没有的列表样式，也避开裁剪后看不见的下拉。
//  3. 输入框/文本域的 3px 焦点环用自绘外框（外扩 3px 描边矩形）—— software 场景图
//     后端下 layer.effect 会把 item 整个吞掉，ThemeBridge.layerEffectsAvailable 已探测。
//
// 底部两个按钮用**共享** Button（迁移自本页已淘汰的那件页私有按钮）：text / variant /
// glyph / onClicked 四个槽位与旧件同名，图标字号改按设计稿的 .btn .icon 15 盒出。
import QtQuick
import QtQuick.Effects        // MultiEffect（.modal 的 shadow-2）
import Shine 1.0

// MultiEffect 里要用外层 id（root），显式声明 Bound 才合法
pragma ComponentBehavior: Bound

Ctl {
    id: root

    // ⚠️ Ctl 是 Rectangle，默认 color 是不透明白。不写这一行，弹层一打开就是一块
    // 盖住整页的白板（.scrim 只在上面再叠一层遮罩，白板照样透出来）。
    color: "transparent"

    // 入参：宿主在 open 前把当前镜头塞进来
    property var shot: null
    signal applied(string action, string mood, string dur, string note)
    signal cancelled()

    // 表单本地副本：保存前不动 shot，避免打断宿主的属性绑定
    property string fAction: ""
    property string fMood: ""
    property string fDur: ""
    property string fNote: ""

    readonly property var moodOptions: ["静谧", "温柔", "怀旧", "怅然", "紧张",
                                       "惊喜", "恢弘", "急切", "诡异", "悬疑"]

    // ThemeBridge 没有 qmltypes（`Failed to import Shine` 是预期噪音）。但它有个**级联副作用**：
    // 某个对象里只要出现一处 ThemeBridge 表达式，qmllint 就会把该对象的某个**基类属性**
    // 误报成 missing-property。所以备注文本域这一个对象全部改读 root 上已解析的属性，
    // 把噪音挡在根这一层（根上的都是自定义 property，不会触发级联）。
    readonly property string uiFont: ThemeBridge.fontFamily
    readonly property int baseFont: ThemeBridge.baseFontPx
    readonly property color fgText: ThemeBridge.colors["text.primary"]
    readonly property color selBg: ThemeBridge.colors["fill.selected"]

    // —— 几何（全部来自设计稿 px，不是估的）——
    readonly property real modalW: 460                      // Storyboard.jsx:186 inline
    readonly property real headH: 14 * 2 + 15 * 1.6 + 1     // .modal-h p14 · f15·1.6 · 1px 边
    readonly property real bodyPad: 18                      // .modal-b padding
    readonly property real footH: 12 * 2 + 30 + 1           // .modal-f p12 · 30 高按钮 · 1px 边
    readonly property real fieldGap: 6                      // .field { gap: 6px }
    readonly property real labelH: 12 * 1.6                 // f12 w600
    readonly property real inputH: 30                      // .input { height: 30px }
    readonly property real durW: 120                        // Storyboard.jsx:201 inline width
    readonly property real moodW: modalW - bodyPad * 2 - 12 - durW
    readonly property real noteH: 2 * 13 * 1.6 + 8 * 2 + 2  // rows 2 · p8 · 1px 边
    readonly property real field1H: labelH + fieldGap + inputH
    readonly property real noteFieldH: labelH + fieldGap + noteH + fieldGap + labelH
    readonly property real bodyH: field1H + 12 + field1H + 12 + noteFieldH   // gap-3 = 12
    readonly property real modalH: headH + bodyPad * 2 + bodyH + footH

    onVisibleChanged: {
        if (visible && shot !== null) {
            fAction = shot.action !== undefined ? shot.action : ""
            fMood = shot.mood !== undefined ? shot.mood : ""
            fDur = shot.dur !== undefined ? shot.dur : ""
            fNote = shot.note !== undefined ? shot.note : ""
        }
    }

    function cycleMood() {
        const i = root.moodOptions.indexOf(root.fMood)
        fMood = root.moodOptions[(i + 1) % root.moodOptions.length]
    }

    // —— .scrim ——（@keyframes fade-in dur-2）
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: ThemeBridge.colors["shadow.scrim"]
        ParallelAnimation {
            running: root.visible && !root.reduce
            NumberAnimation { target: scrim; property: "opacity"; from: 0; to: 1; duration: root.durBase }
        }
        MouseArea {
            anchors.fill: parent
            // Storyboard.jsx:185 e.target === e.currentTarget 才关（即点遮罩空白处）
            onClicked: root.cancelled()
        }
    }

    // —— .modal ——
    // ⚠️ 定位纪律：modal 自身**不能**用 anchors（pop-in 要动画 y，锚定会抢 y）。
    // 居中交给外层 modalBox，modal 在里面显式给 x/y/宽高。
    Item {
        id: modalBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(root.modalW, parent.width - 48)     // .modal min(560, 100vw - 48)
        height: root.modalH

        Rectangle {
            id: modal
            x: 0
            y: 6
            width: parent.width
            height: parent.height
            radius: root.rLg
            color: ThemeBridge.colors["bg.overlay"]
            border.width: 1
            border.color: ThemeBridge.colors["line.normal"]
            clip: true                                         // .modal { overflow: hidden }

            layer.enabled: root.shadows
            layer.effect: MultiEffect {
                shadowEnabled: !root.reduce
                shadowBlur: 1.0
                shadowScale: 1.0
            }

            // @keyframes pop-in { opacity 0 + scale(.96) translateY(6px) → 常态 }
            // 「减少动效」下不挂动画，静止出现在终点位。
            ParallelAnimation {
                running: root.visible && !root.reduce
                NumberAnimation { target: modal; property: "y"; from: 6; to: 0; duration: root.durBase; easing.type: Easing.OutCubic }
                NumberAnimation { target: modal; property: "opacity"; from: 0; to: 1; duration: root.durBase; easing.type: Easing.InQuad }
                NumberAnimation { target: modal; property: "scale"; from: 0.96; to: 1; duration: root.durBase; easing.type: Easing.OutCubic }
            }

        // —— .modal-h ——
        Item {
            id: head
            x: 0; y: 0
            width: parent.width
            height: root.headH
            Text {
                x: root.bodyPad
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                horizontalAlignment: Text.AlignHCenter
                text: "✦"                                  // Icon name="wand" accent
                font.pixelSize: 13
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                x: root.bodyPad + 16 + 10                   // .modal-h { gap: 10px }
                anchors.verticalCenter: parent.verticalCenter
                text: "编辑镜头 · " + (root.shot !== null && root.shot.code !== undefined ? root.shot.code : "")
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold                  // .modal-h .title 700
                color: ThemeBridge.colors["text.primary"]
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: root.bodyPad
                anchors.verticalCenter: parent.verticalCenter
                text: "保存后 Prompt 版本 +1"                // .tiny.dim
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
                color: ThemeBridge.colors["text.muted"]
            }
            Rectangle {                                    // border-bottom
                x: 0; y: parent.height - 1
                width: parent.width; height: 1
                color: ThemeBridge.colors["line.subtle"]
            }
        }

        // —— .modal-b ——
        Item {
            x: root.bodyPad
            y: root.headH + root.bodyPad
            width: parent.width - root.bodyPad * 2
            height: root.bodyH

            // Field 动作
            Text {
                x: 0; y: 0
                text: "动作"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: ThemeBridge.colors["text.secondary"]
            }
            Rectangle {
                id: actionBox
                x: 0
                y: root.labelH + root.fieldGap
                width: parent.width
                height: root.inputH
                radius: root.rSm
                color: actionInput.activeFocus ? ThemeBridge.colors["bg.surface"]
                                               : ThemeBridge.colors["fill.muted"]
                border.width: 1
                border.color: actionInput.activeFocus ? ThemeBridge.colors["line.focus"]
                           : areaHover.containsMouse ? ThemeBridge.colors["line.strong"]
                           : ThemeBridge.colors["line.normal"]
                Behavior on border.color {
                    ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                }
                Rectangle {                                  // :focus 的 3px accent-dim 环
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: parent.radius + 3
                    color: "transparent"
                    border.width: 3
                    border.color: ThemeBridge.toneBg["accent"]
                    visible: actionInput.activeFocus
                }
                TextInput {
                    id: actionInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: ThemeBridge.baseFontPx     // 继承 body 13
                    color: ThemeBridge.colors["text.primary"]
                    selectionColor: ThemeBridge.colors["fill.selected"]
                    selectedTextColor: ThemeBridge.colors["text.primary"]
                    selectByMouse: true
                    text: root.fAction
                    onTextEdited: root.fAction = text
                }
                MouseArea {
                    id: areaHover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }

            // Field 情绪（Select · 点击轮换） + Field 时长
            Item {
                x: 0
                y: root.field1H + 12
                width: parent.width
                height: root.field1H

                Text {
                    x: 0; y: 0
                    text: "情绪"
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: ThemeBridge.colors["text.secondary"]
                }
                Rectangle {
                    id: moodBox
                    x: 0
                    y: root.labelH + root.fieldGap
                    width: root.moodW
                    height: root.inputH
                    radius: root.rSm
                    color: moodArea.containsMouse ? ThemeBridge.colors["fill.hover"]
                                                  : ThemeBridge.colors["fill.muted"]
                    border.width: 1
                    border.color: ThemeBridge.colors["line.normal"]
                    Text {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.fMood
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: ThemeBridge.baseFontPx
                        color: ThemeBridge.colors["text.primary"]
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10                 // 箭头 right 10 center
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"                              // 10×6 SVG 的字符替身（与 Widgets 侧同款）
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 10
                        color: ThemeBridge.colors["text.muted"]
                    }
                    MouseArea {
                        id: moodArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.cycleMood()
                    }
                }

                Text {
                    x: root.moodW + 12
                    y: 0
                    text: "时长"
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: ThemeBridge.colors["text.secondary"]
                }
                Rectangle {
                    x: root.moodW + 12
                    y: root.labelH + root.fieldGap
                    width: root.durW
                    height: root.inputH
                    radius: root.rSm
                    color: durInput.activeFocus ? ThemeBridge.colors["bg.surface"]
                                               : ThemeBridge.colors["fill.muted"]
                    border.width: 1
                    border.color: durInput.activeFocus ? ThemeBridge.colors["line.focus"]
                              : durHover.containsMouse ? ThemeBridge.colors["line.strong"]
                              : ThemeBridge.colors["line.normal"]
                    Behavior on border.color {
                        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: parent.radius + 3
                        color: "transparent"
                        border.width: 3
                        border.color: ThemeBridge.toneBg["accent"]
                        visible: durInput.activeFocus
                    }
                    TextInput {
                        id: durInput
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        font.family: "Consolas"                     // .mono
                        font.pixelSize: 12                        // .mono 12px
                        color: ThemeBridge.colors["text.primary"]
                        selectionColor: ThemeBridge.colors["fill.selected"]
                        selectedTextColor: ThemeBridge.colors["text.primary"]
                        selectByMouse: true
                        text: root.fDur
                        onTextEdited: root.fDur = text
                    }
                    MouseArea {
                        id: durHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
            }

            // Field 备注（TextArea rows 2 + help）
            Item {
                x: 0
                y: root.field1H * 2 + 12 * 2
                width: parent.width
                height: root.noteFieldH
                Text {
                    x: 0; y: 0
                    text: "备注"
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: ThemeBridge.colors["text.secondary"]
                }
                Rectangle {
                    id: noteBox
                    x: 0
                    y: root.labelH + root.fieldGap
                    width: parent.width
                    height: root.noteH
                    radius: root.rSm
                    color: noteInput.activeFocus ? ThemeBridge.colors["bg.surface"]
                                                : ThemeBridge.colors["fill.muted"]
                    border.width: 1
                    border.color: noteInput.activeFocus ? ThemeBridge.colors["line.focus"]
                              : noteHover.containsMouse ? ThemeBridge.colors["line.strong"]
                              : ThemeBridge.colors["line.normal"]
                    Behavior on border.color {
                        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: parent.radius + 3
                        color: "transparent"
                        border.width: 3
                        border.color: ThemeBridge.toneBg["accent"]
                        visible: noteInput.activeFocus
                    }
                    Text {
                        // .textarea::placeholder { color: var(--text-muted) }（ui.css:306-307）
                        // 自绘而不用 TextEdit.placeholderText：本 Qt 的 qmllint 在 Shine 导入失败后
                        // 会把含 ThemeBridge 表达式的对象里的某个基类属性误报成 missing-property，
                        // placeholderText 正好被点名。Text 不接受鼠标事件，点穿给下面的 TextEdit。
                        x: 10
                        y: 8
                        visible: root.fNote === ""
                        text: "例如：火苗光比要压暗一档…"
                        font.family: root.uiFont
                        font.pixelSize: root.baseFont
                        color: ThemeBridge.colors["text.muted"]
                    }
                    TextEdit {
                        id: noteInput
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.topMargin: 8
                        anchors.bottomMargin: 8
                        wrapMode: TextEdit.Wrap
                        selectionColor: root.selBg
                        selectedTextColor: root.fgText
                        selectByMouse: true
                        text: root.fNote
                        onTextChanged: root.fNote = text
                        font.family: root.uiFont
                        font.pixelSize: root.baseFont
                        // ⚠️ .textarea 的 line-height 1.6 落不了地：QQuickTextEdit 没有
                        // lineHeight（只有 QQuickText 有），本 Qt 6.11.2 实测确认。
                        // 框高仍按 1.6 算（noteH），文字按默认行距顶对齐。
                        color: root.fgText
                    }
                    MouseArea {
                        id: noteHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
                Text {
                    x: 0
                    y: root.labelH + root.fieldGap + root.noteH + root.fieldGap
                    text: "写入 shots 备注列，供出图 prompt 参考"
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                    color: ThemeBridge.colors["text.muted"]
                }
            }
        }

        // —— .modal-f ——
        Item {
            x: 0
            y: parent.height - root.footH
            width: parent.width
            height: root.footH
            Rectangle {                                        // border-top
                x: 0; y: 0
                width: parent.width; height: 1
                color: ThemeBridge.colors["line.subtle"]
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: root.bodyPad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8                                        // .modal-f { gap: 8px }
                Button {
                    text: "取消"
                    variant: "ghost"
                    onClicked: root.cancelled()
                }
                Button {
                    text: "保存"
                    variant: "primary"
                    glyph: "✓"
                    onClicked: {
                        root.applied(root.fAction, root.fMood, root.fDur, root.fNote)
                        root.cancelled()
                    }
                }
            }
        }
        }   // modal
    }       // modalBox
}           // root
