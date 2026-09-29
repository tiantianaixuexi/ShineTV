pragma ComponentBehavior: Bound
// src/ui/qml/AssetsRefLibrary.qml —— 页面私有：项目参考库（QML 迁移）
//
// 数据源：C++ 注入的 `Page`（ui/pages/assets/AssetPageModel.h 的 refImages），
// 取数经 visual::ReferenceLibrary + AssetVisualData.h 的 AssetCollectRefs，
// 本页只做渲染与动作派发。
// 文案沿用迁移前那一版（导入图片 / 刷新 / 保存标记 / 绑定当前实体 / 移除）。
//
// ⚠️ 三条接缝纪律：
//   1. **refImages 每项带全键**（id / originalName / absPath / orientation /
//      rawWidth / rawHeight / displayWidth / displayHeight / entityId /
//      markers / markerText）。少一个键在 QML 侧就是 undefined，赋给
//      QString/int 报 "Unable to assign"。所以下面 selectedRef 的空态照
//      Assets.qml 的 emptyAsset 给一份**带类型零值**的记录（不能是 null：
//      十几处 selectedRef.xxx 绑���会把空态刷成一片 TypeError）。
//   2. **选中 id 必须是 Q_PROPERTY** —— C++ 侧只给 `Page.selectReference(id)` 这个
//      写动作，方法调用在 QML 依赖图里是空的、只求值一次，首屏之后再选别的图
//      绑定不会跟着重算。所以读侧补了 `Page.selectedRefId`（AssetPageModel.h），
//      高亮与标记回填都绑它；本页仍保留一份本地 selectedId 供点击即时反馈，
//      但**真值以 Page.selectedRefId 为准**，标记 / 绑定 / 移除三个写动作
//      一律由 C++ 用它自己那份真选择。
//   3. 缩略图走 `Image.asynchronous` + sourceSize（AssetsTimeline / AssetsCompare
//      同一写法）：解码在 worker，UI 线程不做同步 IO。cover 裁切交给
//      `fillMode: Image.PreserveAspectCrop` —— **不要**自己算 scale 去铺满，
//      Art.qml 头注里 frame/zoomBox 的 transformOrigin 坑就是这么踩出来的。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 真值：参考图列表（每项带全键，见头注 ①）——
    readonly property var refs: Page.refImages
    // 见头注 ②：本地镜像的真选择 id，仅供高亮与标记回填
    property string selectedId: ""

    readonly property var emptyRef: ({
        id: "", originalName: "", absPath: "", orientation: "",
        rawWidth: 0, rawHeight: 0, displayWidth: 0, displayHeight: 0,
        entityId: 0, markers: [], markerText: ""
    })
    readonly property var selectedRef: {
        for (var i = 0; i < root.refs.length; ++i) {
            if (root.refs[i].id === root.selectedId) { return root.refs[i] }
        }
        return root.emptyRef
    }
    readonly property bool hasSel: root.selectedRef.id !== ""
    // 对外只读镜像：页面层（Assets.qml 设定集那行的「绑定当前实体」）要先判
    // 有没有选中再调 C++，否则 C++ 会静默返回。
    // 真值取 C++ 的 Page.selectedRefId（Q_PROPERTY，随 changed 重算）；
    // 本地 selectedId 只负责点击时的即时反馈，随后会被真值追上。
    readonly property string selectedRefId: Page.selectedRefId

    // 绑定按钮的标签：有选中实体时带上名字（与迁移前的 Widgets 版同一口径）
    readonly property string bindLabel: {
        var name = Page.selectedEntityId > 0
                 ? Page.entityNameById("" + Page.selectedEntityId) : ""
        return name === "" ? "绑定当前实体" : "绑定 " + name
    }

    // —— 间距别名：Ctl 只转发圆角/动效，不带 spaces（键是 CSS 刻度名）——
    readonly property int spXs: ThemeBridge.spaces["1"]   // 4
    readonly property int spSm: ThemeBridge.spaces["2"]   // 8
    readonly property int spMd: ThemeBridge.spaces["3"]   // 12

    // —— 盒模型（行高/缩略图是本页自己的排版选择，不是设计稿的）——
    readonly property int headH: 30            // 头部由 .btn.sm 撑
    readonly property int rowH: 64             // 缩略 84×48 + 上下各 8
    readonly property int thumbW: 84
    readonly property int thumbH: 48
    // 最多露出 3 行，超出的由 ListView 自己滚（不滚的话一页塞 20 张参考图）
    readonly property int listMaxH: rowH * 3
    readonly property int listH: refs.length > 0
                              ? Math.min(refs.length * rowH, listMaxH) : 0
    readonly property int inputH: 30           // 与 Input.qml 同档（ui.css:288 h30）
    readonly property int btnH: 24             // .btn.sm
    readonly property int kvH: hasSel ? 3 * 20 + 2 * 6 : 0
    readonly property int detailH: hasSel ? (kvH + spSm + inputH + spSm + btnH) : 0
    readonly property int hintH: refs.length === 0 ? 16 : (hasSel ? 0 : 16)

    // 头 + 列表 + 提示行（有选中时才出现的详情块另算一段）
    readonly property real headListH: headH
        + (listH > 0 ? spSm + listH : 0)
        + (hintH > 0 ? spSm + hintH : 0)
    readonly property real hintY: headH + (listH > 0 ? spSm + listH : 0) + spSm

    implicitHeight: hasSel ? headListH + spMd + detailH : headListH
    implicitWidth: 720
    color: "transparent"

    // 换选中的那张：草稿跟着换（标记是**每张各存**的，串台就是写坏库）
    function pick(id) {
        if (id === root.selectedId) { return }
        root.selectedId = id
        markerInput.text = markersOf(id)
        Page.selectReference(id)
    }
    function markersOf(id) {
        for (var i = 0; i < root.refs.length; ++i) {
            if (root.refs[i].id === id) { return root.refs[i].markerText }
        }
        return ""
    }

    Connections {
        target: Page
        // ⚠️ 这里**只**处理「选中的那张被删了」。不要在这里无条件把
        // markerInput.text 换回 refImages 里的值：C++ 的 changed() 在刷新
        // 资产、切筛选、跑完一层时都会发，无条件回灌会把用户正在打的
        // 标记冲掉（那正是原来 Widgets 版 signal 阻塞要防的事）。
        function onChanged() {
            if (root.selectedId !== "" && root.selectedRef.id === "") {
                root.selectedId = ""
                markerInput.text = ""
            }
        }
    }

    Component.onCompleted: {
        if (root.refs.length > 0) { root.pick(root.refs[0].id) }
    }

    // —— 头部：计数 + 导入 / 刷新 ——
    Item {
        id: head
        x: 0
        y: 0
        width: root.width
        height: root.headH

        Text {
            id: countText
            x: 0
            y: (head.height - height) / 2
            width: Math.max(0, head.width - headTrail.implicitWidth - root.spSm)
            elide: Text.ElideRight
            // 数量来自真库行数，没有图就是 0 张，不写占位假数
            text: root.refs.length + " 张 · 缩略 " + root.thumbW + "×" + root.thumbH
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
        }
        Row {
            id: headTrail
            x: head.width - width
            y: (head.height - height) / 2
            spacing: root.spSm
            Button {
                text: "导入图片"
                glyph: "↑"
                variant: "primary"
                sm: true
                // 选路径的对话框必须在 UI 线程（模态），解码/写回 refs.json
                // 由 C++ 放 worker —— 这里只发意图。
                onClicked: Page.chooseReferenceFiles()
            }
            Button {
                text: "刷新"
                glyph: "↻"
                variant: "secondary"
                sm: true
                onClicked: Page.refreshReferences()
            }
        }
    }

    // —— 列表：可点的缩略行 ——
    ListView {
        id: list
        x: 0
        y: root.headH + root.spSm
        width: root.width
        height: root.listH
        visible: root.listH > 0
        clip: true
        // 行高固定，ListView 自己不再推导
        spacing: 0
        boundsBehavior: Flickable.StopAtBounds
        model: root.refs

        delegate: Item {
            id: row
            required property var modelData

            width: list.width
            height: root.rowH
            readonly property bool on: row.modelData.id === root.selectedId
            readonly property bool bound: row.modelData.entityId > 0

            Rectangle {
                anchors.fill: parent
                radius: root.rSm
                color: row.on ? ThemeBridge.colors["fill.selected"]
                              : rowArea.containsMouse ? ThemeBridge.colors["fill.hover"]
                                                      : "transparent"
                Behavior on color {
                    ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                }
            }
            Rectangle {                  // 缩略（底色 = .art 的 fill-muted，图没来时也有个框）
                x: root.spSm
                y: (root.rowH - root.thumbH) / 2
                width: root.thumbW
                height: root.thumbH
                radius: root.rSm
                color: ThemeBridge.colors["fill.muted"]
                clip: true
                Image {
                    width: parent.width
                    height: parent.height
                    source: "file:///" + row.modelData.absPath
                    // cover 裁切由 fillMode 内部完成（见头注 ③）
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: Math.round(root.thumbW * 2)
                    sourceSize.height: Math.round(root.thumbH * 2)
                }
            }
            Text {                      // 文件名
                x: root.spSm + root.thumbW + root.spMd
                y: root.spSm + 4
                width: Math.max(0, row.width - x - root.spSm - 8 - root.tagW)
                height: 18
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: row.modelData.originalName
                color: row.on ? ThemeBridge.colors["accent.primary"]
                              : ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: ThemeBridge.baseFontPx
            }
            Text {                      // 次行：原始尺寸 + 标记
                x: root.spSm + root.thumbW + root.spMd
                y: root.spSm + 4 + 18
                width: Math.max(0, row.width - x - root.spSm - 8 - root.tagW)
                height: 16
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: row.modelData.rawWidth + "×" + row.modelData.rawHeight
                      + (row.modelData.markerText === ""
                         ? "" : " · " + row.modelData.markerText)
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
            Tag {
                id: bindTag
                x: row.width - width - root.spSm
                y: (root.rowH - height) / 2
                text: row.bound ? "已绑定" : "未绑定"
                tone: row.bound ? "ok" : "idle"
                sm: true
            }
            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.pick(row.modelData.id)
            }
        }
    }

    // 标签宽度是量出来的，不能在上面的 width 算式里凭空猜一个数
    TextMetrics {
        id: tagProbe
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 10          // Tag sm 档
        font.weight: Font.DemiBold
    }
    readonly property real tagW: tagProbe.width + 6 * 2 + 1 * 2

    // —— 空态 / 未选中提示（恒占一行，别让详情块的出现把版面顶下去）——
    Text {
        x: 0
        y: root.hintY
        width: root.width
        height: 16
        visible: root.hintH > 0
        elide: Text.ElideRight
        text: root.refs.length === 0
            ? "未打开项目或还没有参考图。"
            : "选择一张参考图查看方向、尺寸与绑定。"
        color: ThemeBridge.colors["text.muted"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }

    // —— 选中详情：尺寸 KV + 标记输入 + 三个动作 ——
    Item {
        id: detail
        x: 0
        y: root.headListH + root.spMd
        width: root.width
        height: root.detailH
        visible: root.hasSel

        Kv {
            id: kv
            x: 0
            y: 0
            width: parent.width
            rows: [
                { key: "原始尺寸", value: root.selectedRef.rawWidth + "×" + root.selectedRef.rawHeight },
                { key: "显示尺寸", value: root.selectedRef.displayWidth + "×" + root.selectedRef.displayHeight },
                { key: "EXIF 方向", value: root.selectedRef.orientation === ""
                                                ? "无" : root.selectedRef.orientation }
            ]
        }

        // 标记输入：视觉照 Input.qml（ui.css:288-299）那一档，另加设计稿的
        // focus 光圈（3px accent-dim）。**自绘 TextInput 而不是套 Input**：
        // Input 的 text 是单向绑定（Input.qml:32），用户敲进去的值不会回填
        // 组件的 text 属性，保存时只能读到旧值 —— 那正是要避免的「以为存上了」。
        Rectangle {
            id: markerBox
            x: 0
            y: root.kvH + root.spSm
            width: parent.width - root.btnRowW
            height: root.inputH
            radius: root.rSm
            color: markerInput.activeFocus ? ThemeBridge.colors["bg.surface"]
                                           : ThemeBridge.colors["fill.muted"]
            border.width: 1
            border.color: markerInput.activeFocus ? ThemeBridge.colors["line.focus"]
                          : markerHover.containsMouse ? ThemeBridge.colors["line.strong"]
                                                     : ThemeBridge.colors["line.normal"]
            Behavior on border.color {
                ColorAnimation { duration: root.reduce ? 0 : root.durFast }
            }
            Rectangle {                 // focus 光圈（--accent-dim 的 12% 混色）
                anchors.fill: parent
                anchors.margins: -3
                radius: parent.radius + 3
                color: "transparent"
                border.width: 3
                border.color: ThemeBridge.toneBg["accent"]
                visible: markerInput.activeFocus
            }
            TextInput {
                id: markerInput
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: TextInput.AlignVCenter
                selectByMouse: true
                clip: true
                color: ThemeBridge.colors["text.primary"]
                selectionColor: ThemeBridge.colors["fill.selected"]
                selectedTextColor: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: ThemeBridge.baseFontPx
            }
            Text {                      // placeholder（自绘，同 StoryboardDialog 的理由）
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: markerInput.text.length === 0
                text: "标记，逗号分隔（例如：正脸, 灰风衣）"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: ThemeBridge.baseFontPx
            }
            MouseArea {
                id: markerHover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
        }

        Row {
            id: btnRow
            x: parent.width - width
            y: root.kvH + root.spSm + (root.inputH - root.btnH) / 2
            spacing: root.spSm
            Button {
                text: "保存标记"
                glyph: "✓"
                variant: "secondary"
                sm: true
                // 没改就别写库：setReferenceMarkers 每次都会动 refs.json
                enabled: markerInput.text !== root.selectedRef.markerText
                onClicked: Page.setReferenceMarkers(markerInput.text)
            }
            Button {
                text: root.bindLabel
                glyph: "↔"
                variant: "primary"
                sm: true
                enabled: Page.selectedEntityId > 0
                onClicked: Page.bindReferenceToEntity()
            }
            Button {
                text: "移除"
                glyph: "✕"
                variant: "danger"
                sm: true
                onClicked: Page.removeReference()
            }
        }
    }

    // 按钮行的宽度要在 markerBox 的 width 算式里用到，先量出来
    readonly property real btnRowW: btnRow.implicitWidth + root.spSm
}
