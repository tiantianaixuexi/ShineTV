pragma ComponentBehavior: Bound
// src/ui/qml/AssetsGallery.qml —— 页面私有：全局图库（QML 迁移，⑥）
//
// 数据源：C++ 注入的 `Page`（ui/pages/assets/AssetPageModel.h 的 galleryItems /
// galleryState / galleryViewer），真值取 shine::gallery（media/Gallery.h）的扫描 /
// 模型 / 查看器。本文件只做渲染与动作派发：扫描、过滤排序、缩放钳制全在 C++ 侧
// （setGalleryZoom 自己 clamp 到 [minZoom, maxZoom]），QML 不再抄一份边界。
//
// ⚠️ 五条接缝纪律：
//   1. **选中态只能在 QML 侧留一份镜像**。C++ 只给了写动作 selectGalleryItem()，
//      没有 selectedGalleryId 这种读属性；`galleryViewer.id` 也不能当读侧 —— 它写的
//      是 `gallery::ViewerImageId()`（AssetPageModel.cpp:855-856），**查看器一关就是 0**，
//      且整张 map 会被下一次 PollGallery 重写。所以本页自己存 selectedId，并挂
//      Connections(onGalleryChanged) 纠偏：C++ 在选中项已不在表里时会把选择清掉
//      （AssetPageModel.cpp:835-838），这里要跟上，别让高亮停在一条不存在的条目上。
//      （对照 AssetsRefLibrary 头注 ② 的同一判断。）
//   2. **缩略图必须 asynchronous + sourceSize**（AGENTS.md：解码不上 UI 线程）。
//      cover 裁切交给 `fillMode: Image.PreserveAspectCrop` —— **不要**自己算 scale 去
//      铺满，Art.qml 头注 frame/zoomBox 的 transformOrigin 坑就是这么踩出来的
//      （AssetsRefLibrary / AssetsTimeline 同一写法）。
//   3. **查看器的缩放必须显式 transformOrigin: Item.Center**。本仓的坑是「缩放绕左上角」
//      （Art.qml:25-31 记着两个 transformOrigin 各管一件事）。这里是对一张居中图做缩放，
//      取 Center 才是 CSS `transform-origin: 50% 50%` 的等价物；不写会朝左上角跑。
//   4. **`file://` 要三个斜杠**。C++ 投过来的是**裸路径**（AssetPageModel.cpp:823
//      `util::PathToUtf8(item.path)`，形如 `C:/a/b.png`），拼成 `file://C:/a/b.png`
//      解析不出主机名。照 AssetsRefLibrary:212 / Assets.qml:805 的既有写法用 `"file:///" + path`。
//   5. **空 map 的兜底不在这里**。C++ 侧 PollGallery(force) 保证首读前表已带全键
//      （AssetPageModel.cpp 的构造函数 / OpenBook 强制投影过一次），所以 gstate.* /
//      gviewer.* 直接绑，少一层影子表就少一处会漂的副本。
//
// 为什么网格用 GridView 而不是 Repeater + 手算 x/y：图库动辄上千张，Repeater 会一次
// 建出全部 delegate（每张一个异步解码的 Image）。GridView 只建可见的那些，与
// AssetsRefLibrary 用 ListView 是同一取舍。
//
// 为什么**不画引用链列表**：用量由 C++ 的 ReferenceUsageLabels() / ActivateReferenceUsage()
// 供验收探针取（AssetPageModel.h:233-235），QML 侧没有对应属性 —— 画不出来就别画。
// 同理没有搜索框 / 排序下拉：galleryItems 投过来的已经是 gallery::Model().View()
// （过滤 + 排序后的视图），再加控件就是死控件。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // ============================================================
    // 真值：全部来自 Page（见头注 ⑤：键恒在，不做影子兜底）
    // ============================================================
    readonly property var entries: Page.galleryItems
    readonly property var gstate: Page.galleryState
    readonly property var gviewer: Page.galleryViewer

    // —— 选中（头注 ①：本地镜像 + C++ 纠偏）——
    property var selectedId: 0
    // 空态照 Assets.qml emptyAsset / AssetsRefLibrary emptyRef 给一份**带类型零值**的
    // 记录：不能是 null（下面十几处 selectedItem.xxx 绑定会把空态刷成一片 TypeError），
    // 也不能是 {}（字段是 undefined，赋给 int 报 "Unable to assign"）。
    readonly property var emptyItem: ({
        id: 0, path: "", name: "", width: 0, height: 0, format: "", source: 0
    })
    readonly property var selectedItem: {
        for (var i = 0; i < root.entries.length; ++i) {
            if (root.entries[i].id === root.selectedId) { return root.entries[i] }
        }
        return root.emptyItem
    }
    readonly property bool hasSel: root.selectedItem.id !== 0

    // —— 间距别名（Ctl 只转发圆角/动效，不带 spaces）——
    readonly property int spXs: ThemeBridge.spaces["1"]   // 4
    readonly property int spSm: ThemeBridge.spaces["2"]   // 8

    // —— 盒模型（网格档位是本页自己的排版选择，不是设计稿的）——
    readonly property int headH: 34        // 头部由 Seg（34）撑，不写死
    readonly property int rowH: 30         // 选中行
    readonly property int cellMinW: 96     // 缩略格最小档
    readonly property int cellH: 96        // 方格
    readonly property int gridGap: 8
    readonly property int maxRows: 3       // 最多露出 3 行，超出的 GridView 自己滚
    readonly property int cols: Math.max(1, Math.floor((root.width + root.gridGap)
                                                       / (root.cellMinW + root.gridGap)))
    readonly property int cellW: Math.max(48, Math.round(
        (root.width - root.gridGap * (root.cols - 1)) / root.cols))
    readonly property int rowCount: root.entries.length > 0
        ? Math.ceil(root.entries.length / root.cols) : 0
    // 空态时留的是 Empty 自己的固有高（不是本页写死的数字，同 AssetsRefLibrary）
    readonly property int gridH: root.rowCount > 0
        ? Math.min(root.rowCount * root.cellH + (root.rowCount - 1) * root.gridGap,
                   root.maxRows * root.cellH + (root.maxRows - 1) * root.gridGap)
        : emptyState.implicitHeight
    readonly property real gridY: root.headH + root.spSm
    readonly property real rowY: root.gridY + root.gridH + root.spSm

    implicitHeight: root.rowY + root.rowH
    implicitWidth: 720
    color: "transparent"

    // 状态行：来源标签 + 计数 + 扫描进度 + 截断 + message（真值，不写占位假数）
    readonly property string stateLine: {
        var s = root.gstate.sourceLabel
        if (root.gstate.scanning) {
            return s + " · 扫描中 · 已载入 " + root.gstate.loaded + " 张"
        }
        s += " · " + root.gstate.count + " 张"
        if (root.gstate.truncated) { s += "（已截断）" }
        if (root.gstate.message !== "") { s += " · " + root.gstate.message }
        if (root.gstate.error !== "") { s += " · " + root.gstate.error }
        return s
    }
    // 选中行文案：有选中给「名称 · 尺寸 · 格式」，没选中给操作提示
    readonly property string selLine: root.hasSel
        ? root.selectedItem.name + " · " + root.selectedItem.width + "×" + root.selectedItem.height
          + (root.selectedItem.format === "" ? "" : " · " + root.selectedItem.format)
        : "选择一张图查看尺寸与格式；双击缩略图打开查看器。"
    readonly property string emptyTitle: root.gstate.scanning ? "正在扫描" : "图库为空"
    readonly property string emptyText: {
        if (root.gstate.error !== "") { return root.gstate.error }
        if (root.gstate.message !== "") { return root.gstate.message }
        if (root.gstate.scanning) { return "扫描完成后缩略图会逐张出现。" }
        return "该来源下没有可用图片。换一个来源，或把图片放进项目目录后重扫。"
    }

    // —— 动作派发（值一律交给 C++，本地不留第二份真值）——
    function pick(id) {
        if (id === 0 || id === root.selectedId) { return }
        root.selectedId = id
        Page.selectGalleryItem(id)
    }
    function openViewer(id) {
        if (id === 0) { return }
        root.selectedId = id
        Page.openGalleryViewer(id)
    }
    function hasEntry(id) {
        for (var i = 0; i < root.entries.length; ++i) {
            if (root.entries[i].id === id) { return true }
        }
        return false
    }
    // ⚠️ 缩放交给 C++ 钳制（AssetPageModel.cpp:946-949），这里的 ± 只是把相邻两档
    //   连起来 —— 按钮的 enabled 已经在边界上关掉了，这一步只是兜底。
    function zoomIn() { Page.setGalleryZoom(root.gviewer.zoom * 1.25) }
    function zoomOut() { Page.setGalleryZoom(root.gviewer.zoom / 1.25) }
    function zoomReset() { Page.setGalleryZoom(1) }

    Connections {
        target: Page
        // 只处理「选中的那张没了」（头注 ①）。不要在这里无条件回灌 selectedId：
        // galleryChanged 在扫描推进、重扫、切来源时都会发。
        function onGalleryChanged() {
            if (root.selectedId !== 0 && !root.hasEntry(root.selectedId)) {
                root.selectedId = 0
            }
        }
    }

    // 不在 Component.onCompleted 里自动选第一张：图库是用户挑的资源，自动选中等于
    // 替用户把某张图顶成「设为工作流输入」的候选（参考库会自动选，二者语义不同）。

    // —— 头部：来源切换（Seg）+ 扫描圈 + 状态行 + 重扫 ——
    Item {
        id: head
        x: 0
        y: 0
        width: root.width
        height: root.headH

        Seg {
            id: sourceSeg
            x: 0
            y: (head.height - height) / 2
            // value 直接就是 C++ 的 SourceKind 下标（0/1/2），中文只在 label 上 ——
            // 再维护一张中英对照表就是第二份真值。
            options: [
                { value: "0", label: "本地" },
                { value: "1", label: "Comfy 输出" },
                { value: "2", label: "Comfy 输入" }
            ]
            value: "" + root.gstate.source
            onPicked: (v) => Page.selectGallerySource(parseInt(v, 10))
        }
        Text {
            id: stateText
            x: sourceSeg.width + root.spSm
            y: (head.height - height) / 2
            width: Math.max(0, head.width - x - rescanBtn.width
                                 - (scanSpin.visible ? scanSpin.width + root.spSm : 0)
                                 - root.spSm)
            height: 18
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: root.stateLine
            color: root.gstate.error !== "" ? ThemeBridge.colors["status.danger"]
                                           : ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
        }
        // 扫描圈放头部不放网格：网格区在空态时只有 Empty 的高度，另开一行会多一档
        // 高度差；放头部还顺带让「正在扫」和「已载入 N 张」在同一行里。
        Spinner {
            id: scanSpin
            visible: root.gstate.scanning
            x: rescanBtn.x - width - root.spSm
            y: (head.height - height) / 2
            sm: true
        }
        Button {
            id: rescanBtn
            x: head.width - width
            y: (head.height - height) / 2
            text: "重扫"
            glyph: "↻"
            variant: "secondary"
            sm: true
            // 已经在扫就别再发一次：重扫会重置缩略图缓存，正在解码的那批白做。
            enabled: !root.gstate.scanning
            onClicked: Page.rescanGallery()
        }
    }

    // —— 网格区：网格 / 空态 两态共用一块高度（扫描态由头部那一行表达）——
    Item {
        id: gridBox
        x: 0
        y: root.gridY
        width: root.width
        height: root.gridH

        GridView {
            id: grid
            x: 0
            y: 0
            width: parent.width
            height: parent.height
            visible: root.gstate.count > 0
            clip: true
            model: root.entries
            cellWidth: root.cellW
            cellHeight: root.cellH
            boundsBehavior: Flickable.StopAtBounds
            // ⚠️ delegate 里只准引用 root.* 与自己的 id —— 本文件是
            // `pragma ComponentBehavior: Bound`，外层 id（grid 等）在 delegate 里不可见。
            delegate: Rectangle {
                id: cell
                required property var modelData

                width: root.cellW
                height: root.cellH
                radius: root.rSm
                readonly property bool on_: cell.modelData.id === root.selectedId

                color: cell.on_ ? ThemeBridge.colors["fill.selected"]
                                : cellHover.containsMouse ? ThemeBridge.colors["fill.hover"]
                                                         : ThemeBridge.colors["fill.muted"]
                Behavior on color {
                    ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                }
                border.width: cell.on_ ? 2 : 1
                border.color: cell.on_ ? ThemeBridge.colors["accent.primary"]
                                       : ThemeBridge.colors["line.subtle"]
                clip: true

                Image {
                    anchors.fill: parent
                    // cover 裁切由 fillMode 内部完成（头注 ②）
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: root.cellW * 2
                    sourceSize.height: root.cellH * 2
                    source: "file:///" + cell.modelData.path
                }
                MouseArea {
                    id: cellHover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.pick(cell.modelData.id)
                    onDoubleClicked: root.openViewer(cell.modelData.id)
                }
            }
        }

        Empty {
            id: emptyState
            x: 0
            y: 0
            width: parent.width
            visible: root.gstate.count === 0
            icon: "image"
            title: root.emptyTitle
            text: root.emptyText
        }
    }

    // —— 选中行：名称 / 尺寸 / 格式 + 两个动作（**恒占一行**，选中变化不推版面）——
    Item {
        id: selRow
        x: 0
        y: root.rowY
        width: root.width
        height: root.rowH

        Text {
            x: 0
            y: (selRow.height - height) / 2
            width: Math.max(0, selRow.width - root.selTrailW - root.spSm)
            height: 18
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: root.selLine
            color: root.hasSel ? ThemeBridge.colors["text.primary"]
                               : ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: ThemeBridge.baseFontPx
        }
        Row {
            id: selTrail
            x: selRow.width - width
            y: (selRow.height - height) / 2
            spacing: root.spSm
            Button {
                text: "查看大图"
                variant: "ghost"
                sm: true
                enabled: root.hasSel
                onClicked: root.openViewer(root.selectedId)
            }
            Button {
                text: "设为工作流输入"
                variant: "primary"
                sm: true
                // 没有选中就不给按：C++ 侧会静默返回，按下去看着像坏了。
                enabled: root.hasSel
                onClicked: Page.setGalleryAsWorkflowInput()
            }
        }
    }
    // 尾部按钮行的宽度要在上面那行 Text 的 width 算式里用到，先量出来
    readonly property real selTrailW: selTrail.width + root.spSm

    // —— 查看器（叠在本面板上：Page.galleryViewer.open）——
    // ⚠️ 声明在**最后**：同层兄弟按声明顺序叠放，写在前面会被下面的内容盖住。
    // 高度**不进** implicitHeight 算式 —— 打开查看器不改变面板高度，页面布局不跳。
    //
    // ⚠️ 这层是「叠在图库面板上」而不是整页灯箱：Assets.qml 的本文件所有权只到
    //    「把面板插进详情滚动区」为止，页面级浮层不在本次范围内。真要整页灯箱，
    //    要在 Assets.qml 里像 previewLayer 那层一样挂一个 anchors.fill 的兄弟项。
    Rectangle {
        id: viewerBox
        anchors.fill: parent
        visible: root.gviewer.open
        color: Qt.alpha(ThemeBridge.colors["bg.void"], 0.92)

        // 点背景关闭（与 Assets.qml 的产物大图预览同一手势）。**先声明**，后面的
        // 文字 / 按钮 / 图像才在它上面，点空白才关得到。
        MouseArea {
            anchors.fill: parent
            onClicked: Page.closeGalleryViewer()
        }

        Item {
            id: viewerHead
            x: 0
            y: 0
            width: parent.width
            height: 30
            Text {
                x: root.spSm
                y: (viewerHead.height - height) / 2
                width: Math.max(0, viewerHead.width - x - closeBtn.width - 2 * root.spSm)
                height: 18
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
                text: root.hasSel ? root.selectedItem.name : root.gviewer.path
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: ThemeBridge.baseFontPx
            }
            Button {
                id: closeBtn
                x: viewerHead.width - width - root.spSm
                y: (viewerHead.height - height) / 2
                text: "关闭"
                glyph: "✕"
                variant: "secondary"
                sm: true
                onClicked: Page.closeGalleryViewer()
            }
        }

        Image {
            x: 0
            y: viewerHead.height
            width: parent.width
            height: parent.height - viewerHead.height - zoomBar.height
            // 缩放绕中心（头注 ③）
            transformOrigin: Item.Center
            scale: root.gviewer.zoom
            fillMode: Image.PreserveAspectFit
            // 原图大：解码仍然在 worker（asynchronous），只是把 sourceSize 放大一档，
            // 让 1:1 档不至于糊。UI 线程仍不碰同步 IO。
            asynchronous: true
            sourceSize.width: 2048
            sourceSize.height: 2048
            source: root.gviewer.path === "" ? "" : "file:///" + root.gviewer.path
        }

        Item {
            id: zoomBar
            x: 0
            y: parent.height - height
            width: parent.width
            height: 30
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: (zoomBar.height - height) / 2
                spacing: root.spSm
                Button {
                    id: zoomOutBtn
                    text: "−"
                    variant: "secondary"
                    sm: true
                    // 边界由 C++ 的 minZoom/maxZoom 定（QML 侧滑杆必须绑它，见
                    // AssetPageModel.h:145-146 的告警）——这里只拿它关按钮，不自己算钳制。
                    enabled: root.gviewer.zoom > root.gviewer.minZoom
                    onClicked: root.zoomOut()
                }
                Text {
                    width: 48
                    height: 24
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: Math.round(root.gviewer.zoom * 100) + "%"
                    color: ThemeBridge.colors["text.secondary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
                Button {
                    id: zoomInBtn
                    text: "+"
                    variant: "secondary"
                    sm: true
                    enabled: root.gviewer.zoom < root.gviewer.maxZoom
                    onClicked: root.zoomIn()
                }
                Button {
                    text: "1:1"
                    variant: "ghost"
                    sm: true
                    onClicked: root.zoomReset()
                }
            }
        }
    }
}
