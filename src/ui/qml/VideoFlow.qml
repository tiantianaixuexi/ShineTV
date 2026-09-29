pragma ComponentBehavior: Bound   // Repeater delegate 里引用 root，必须显式绑定（pragma 在 import 之前）
// src/ui/qml/VideoFlow.qml —— 出片工作区（QML 迁移，2026-09-30）
//
// 对照 webui/src/views/VideoFlow.jsx（174 行）。部件几何逐条取自
// views.css:183-270（float-toolbar / float-panel / float-strip / canvas-tools）、
// views.css:294-320（.dlist / .drow）、views.css:1017-1113（画布 / 节点）与
// ui.css 的 .btn / .tag / .seg / .prog / .dot / .spin / .kv。
//
// ⚠️ 根对象与定位纪律（与 widgets 侧 QQuickWidget 宿主一致）：
// 1. 根是 Ctl（Rectangle），不是 Window —— Window 会报 `invalid root object.`；
//    尺寸由宿主给（FillWithRoot），本页只画**内容区**，外壳归 src/ui/pages/shell/。
// 2. 页面这一层不套定位器：浮动工具栏 / 面板 / 胶片条 / 画布工具都是显式锚点 +
//    显式 x/y，hover 只改 color，不做 translateY。
// 3. 色值只走 ThemeBridge（qml-kit 纪律 2）。唯一两个「非桥」取值是字符图标
//    与等宽字体名（见文件尾）。
//
// ============================ 与迁移前 Widgets 版的刻意差异 ============================
// 1. **浮动面的底色按 QSS 取 token**（QssBuilder 的 #floatPanel = %3 = bg.panel、
//    #floatToolbar = %4 = bg.elevated、#floatSep = %7 = line.normal），而不是按
//    设计稿的 `--glass`。这是本仓对 backdrop-filter 的既定契约（第一节：真边界）。
//    同页的 ImageFlow.qml 在这两处用了相邻档（toolbar=bg.panel / panel=bg.surface），
//    那是那边的偏差，**不要**当成惯例抄过来。
// 2. **面板页签从 QTabWidget 换成共享 Seg**：设计稿这一段就是 Segmented 胶囊分段。
//    迁移前刻意保留 QTabWidget（下划线页签）只是因为评审探针用
//    findChild<QTabWidget*>() 反查切页；现在切页状态上了桥（Page.tab），
//    探针改成读 ActiveTab()，那个理由不再成立。
// 3. **面板底 fp-f 的文案由真实状态算**（节点状态计数 + 队列运行数）。迁移前那行
//    「H3 生成 · RIFE 待跑 · Encode 排队」是写死的常量，与页面实际状态无关。
// 4. **各页签的脚注用真实汇总**（chainState.summary / taskState.summary /
//    cutState.summary），设计稿那三句是行为说明、不是状态。
// 5. **胶片格没有缩略图**：设计稿用 <Art seed> 画程序化占位图，那是假缩略；
//    迁移前的 FilmStrip::Cell.thumb 从没被填过。这里显式空态（有首帧才画图）。
// 6. **「停止」按钮没有**：设计稿 TaskList 有「停止」，但 Widgets 版那一格是
//    「演示运行」，两版都没有停止的实现。本页只给两个**真有实现**的按钮，
//    不做没接线的死按钮（纪律：宁可显式记 NOT-COVERED）。
// ==================================================================================
import QtQuick
import QtQuick.Effects  // MultiEffect（浮动面的 --shadow-1 / --shadow-2）
import Shine 1.0

Ctl {
    id: root

    // .flow-canvas { background: 点阵 + --fill-muted } —— 本页根即画布底
    color: ThemeBridge.colors["fill.muted"]

    // ===================== 取景（FlowCanvas.jsx:31/36-52） =====================
    // ⚠️ 变换**写进坐标**，不放容器的 transform 列表：节点走 x/y + scale，
    //    连线走 ax/ay/bx/by，两边共用 viewX / viewY / zoom 三个属性，
    //    物理上不可能错开（ImageFlow.qml 的注释有完整踩坑记录）。
    property real zoom: 1
    property real viewX: 40
    property real viewY: 30
    readonly property int fitInset: 380   // <FlowCanvas fitInset={380}> = 面板 348 + 外缩 16×2 - 16
    readonly property int fitPad: 48      // fit() 里的 -48
    readonly property int bboxPad: 24     // fit() 包围盒的 ±24 留白
    readonly property int nodeW: 150      // NODE_W
    readonly property int nodeBoxH: 76    // NODE_H（只用于 fit 包围盒）
    readonly property int portOffset: 38  // portPos 的 node.y + 38
    // 端口圆心相对卡片左上角（.port 9px 宽、left/right -5.5）：in = -1 / out = 151
    readonly property real inPortX: -1
    readonly property real outPortX: 151

    readonly property var segOptions: [
        { value: "chain", label: "首尾帧链" },
        { value: "task",  label: "视频任务" },
        { value: "cut",   label: "成片" }]

    // 任一节点在跑，或队列里有运行行 —— 画布工具条的运行指示（.canvas-tools 的 spin）
    readonly property bool graphRunning: {
        for (var i = 0; i < Page.graphNodes.length; ++i) {
            if (Page.graphNodes[i].state === "run") return true
        }
        return Page.taskState.running > 0
    }

    function nodeById(id) {
        for (var i = 0; i < Page.graphNodes.length; ++i)
            if (Page.graphNodes[i].id === id) return Page.graphNodes[i]
        return null
    }

    // fit()：与 FlowCanvas.jsx:36-52 同式（availW 扣掉 fitInset，缩放上限 1.15）
    function fit() {
        // 宿主还没给尺寸时（QQuickWidget 装载瞬间 width/height 可能为 0）不取景，
        // 否则会先按 availW=200/availH=160 算出一个极小缩放，等 onWidthChanged 再跳。
        if (root.width < 2 || root.height < 2) return
        var nodes = Page.graphNodes
        if (nodes.length === 0) {
            // 空图（还没导入工作流）：回到默认取景。**必须显式复位** ——
            // 否则清空后画布会停在上一次的缩放/平移上，「空态」那张取证图
            // 就不是空态了，而 manifest 照样记 saved。
            root.zoom = 1; root.viewX = 40; root.viewY = 30
            return
        }
        var minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9
        for (var i = 0; i < nodes.length; ++i) {
            var n = nodes[i]
            minX = Math.min(minX, n.x)
            minY = Math.min(minY, n.y)
            maxX = Math.max(maxX, n.x + root.nodeW)
            maxY = Math.max(maxY, n.y + root.nodeBoxH)
        }
        minX -= root.bboxPad; minY -= root.bboxPad
        maxX += root.bboxPad; maxY += root.bboxPad
        var availW = Math.max(200, root.width - root.fitInset - root.fitPad)
        var availH = Math.max(160, root.height - root.fitPad)
        var z = Math.min(availW / (maxX - minX), availH / (maxY - minY), 1.15)
        root.zoom = z
        root.viewX = (root.width - root.fitInset - (maxX - minX) * z) / 2 - minX * z + 8
        root.viewY = (root.height - (maxY - minY) * z) / 2 - minY * z
    }

    // 尺寸**或**节点数变化都重新取景（loadMock 灌图后要重新 fit）。
    // 合成为一个 key + 一个处理器：onWidthChanged / onHeightChanged 的单表达式体
    // 会被 QQml 当成属性赋值（坑位清单）。
    readonly property string fitKey: Math.round(width) + "x" + Math.round(height)
                                     + "|" + Page.graphNodes.length
    Component.onCompleted: { root.fit() }
    onFitKeyChanged: { root.fit() }

    // ===================== 画布底：点阵 =====================
    // views.css:1021-1023：radial-gradient(circle at 1px 1px, --line-normal 1px, transparent 0)
    //              0 0 / 22px 22px。点阵用 Canvas 一次画完（3000 个 Rectangle 不划算）。
    Canvas {
        id: dotGrid
        anchors.fill: parent
        // Immediate：onPaint 里要读 ThemeBridge，Cooperative 会把 onPaint 挪到
        // 渲染线程（QML 对象跨线程不安全）。
        renderStrategy: Canvas.Immediate
        readonly property string paintKey: Math.round(width) + "x" + Math.round(height)
                                           + "|" + ThemeBridge.colors["line.normal"]
        onPaintKeyChanged: { requestPaint() }
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = ThemeBridge.colors["line.normal"]
            var pitch = 22
            for (var y = 0; y < height; y += pitch) {
                for (var x = 0; x < width; x += pitch) {
                    ctx.beginPath()
                    ctx.arc(x + 1, y + 1, 1, 0, Math.PI * 2)
                    ctx.fill()
                }
            }
        }
    }

    // ===================== 画布交互：点空白取消选中 + 拖动平移 =====================
    // ⚠️ 平移后**不**触发取消选中：press 与 release 之间只要动过就记 panMoved。
    //    否则「拖一下画布」会顺手把刚选中的节点取消掉（Widgets 版的老毛病）。
    MouseArea {
        id: canvasBg
        anchors.fill: parent
        property bool panMoved: false
        property real lastX: 0
        property real lastY: 0
        onPressed: (mouse) => {
            panMoved = false
            lastX = mouse.x
            lastY = mouse.y
        }
        // ⚠️ QQuickMouseEvent **没有** delta（那是 QMouseEvent 的字段，qmllint 会报
        //    missing-property）：平移量只能自己按「本次位置 − 上次位置」算。
        onPositionChanged: (mouse) => {
            if (!pressed) return
            var dx = mouse.x - canvasBg.lastX
            var dy = mouse.y - canvasBg.lastY
            canvasBg.lastX = mouse.x
            canvasBg.lastY = mouse.y
            if (dx !== 0 || dy !== 0) {
                root.viewX += dx
                root.viewY += dy
                panMoved = true
            }
        }
        onClicked: { if (!panMoved) Page.selectNode("") }
    }

    // ===================== 节点层（连线在下、节点在上） =====================
    Repeater {
        model: Page.graphLinks
        delegate: CanvasLink {
            required property var modelData
            readonly property var na: root.nodeById(modelData.from)
            readonly property var nb: root.nodeById(modelData.to)
            // 画在**未变换**的页面坐标系里，所以自己铺满整页
            x: 0
            y: 0
            width: root.width
            height: root.height
            ax: root.viewX + root.zoom * (na.x + root.outPortX)
            ay: root.viewY + root.zoom * (na.y + root.portOffset)
            bx: root.viewX + root.zoom * (nb.x + root.inPortX)
            by: root.viewY + root.zoom * (nb.y + root.portOffset)
            zoom: root.zoom
            // active：两端节点都不是 todo（FlowCanvas.jsx:154）
            active: na.state !== "todo" && nb.state !== "todo"
        }
    }

    Repeater {
        model: Page.graphNodes
        delegate: CanvasNode {
            required property var modelData
            x: root.viewX + root.zoom * modelData.x
            y: root.viewY + root.zoom * modelData.y
            scale: root.zoom
            transformOrigin: Item.TopLeft     // == CSS transform-origin: 0 0
            title: modelData.title
            sub: modelData.sub
            iconGlyph: modelData.icon
            nodeState: modelData.state
            selected: modelData.id === Page.selectedNodeId
            onPicked: { Page.selectNode(modelData.id) }
        }
    }

    // ===================== 左下：画布工具条（views.css:271-283 的 .canvas-tools.bl-up） =====================
    // 出图页底部没有胶片条 → .bl（bottom 16）；本页有 → .bl-up（bottom 118）。
    Ctl {
        id: tools
        anchors { left: parent.left; leftMargin: 16; bottom: parent.bottom; bottomMargin: 118 }
        width: 30                       // 4(pad) + 22(.icon-btn.sm) + 4(pad)
        height: toolCol.implicitHeight + 8
        radius: ThemeBridge.radii.md     // 10（QSS #flowCanvasTools 同档）
        color: ThemeBridge.colors["bg.elevated"]
        border.width: 1
        border.color: ThemeBridge.colors["line.subtle"]
        layer.enabled: ThemeBridge.layerEffectsAvailable && !root.reduce
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            shadowScale: 1.0
        }

        Column {
            id: toolCol
            x: 4
            y: 4
            spacing: 4
            IconBtn { sm: true; glyph: "＋"; tip: "放大"
                      onClicked: { root.zoom = Math.min(2.5, root.zoom * 1.2); root.clampView() } }
            IconBtn { sm: true; glyph: "－"; tip: "缩小"
                      onClicked: { root.zoom = Math.max(0.35, root.zoom / 1.2); root.clampView() } }
            IconBtn { sm: true; glyph: "⊙"; tip: "适应视图"
                      onClicked: { root.fit() } }
            // running 时的 spin sm（.canvas-tools 的运行指示）
            Item {
                width: 22
                height: 15
                visible: root.graphRunning
                Spinner { anchors.centerIn: parent; sm: true }
            }
        }
    }

    // 缩放后把平移量夹回可视范围：节点被推出画布外就等于「看不见」，
    // 而 QML 不会因为内容移出父项就报错。
    function clampView() {
        root.viewX = Math.min(0, root.viewX)
        root.viewY = Math.min(0, root.viewY)
    }

    // ===================== 左上：浮动工具栏（views.css:183-203） =====================
    Ctl {
        id: toolbar
        anchors { left: parent.left; leftMargin: 16; top: parent.top; topMargin: 16 }
        width: toolRow.width + 26         // padding 12 两侧 + 边 1×2
        height: 42                        // padding 8 + .btn.sm 24 + 边 1×2
        radius: ThemeBridge.radii.md
        color: ThemeBridge.colors["bg.elevated"]   // QSS #floatToolbar = %4 = bg.elevated
        border.width: 1
        border.color: ThemeBridge.colors["line.subtle"]
        layer.enabled: ThemeBridge.layerEffectsAvailable && !root.reduce
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            shadowScale: 1.0
        }

        Row {
            id: toolRow
            x: 13      // padding 12 + 边 1
            y: 9       // padding 8 + 边 1
            spacing: 8 // .float-toolbar gap 8
            // .float-toolbar 是 align-items: center：Row 不做垂直居中，
            // 所以每个非按钮子项自己占满 24 高（.btn.sm）再垂直对齐。
            Text {
                width: 16
                height: 24
                verticalAlignment: Text.AlignVCenter
                text: "◈"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 16
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                width: implicitWidth
                height: 24
                verticalAlignment: Text.AlignVCenter
                text: "出片流程 · H3 视频"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12     // .small 12.5 → 12
                font.weight: Font.DemiBold
                color: ThemeBridge.colors["text.primary"]
            }
            Rectangle {                 // .vsep 1×18 + line-normal
                width: 1
                height: 18
                color: ThemeBridge.colors["line.normal"]
            }
            Button {
                id: validateBtn
                text: "提交前参数校验"
                variant: "secondary"
                sm: true
                // 图标走槽位（qml-kit 纪律 9）：组件内部把它摆进 .btn .icon 的 15"15 盒。
                iconSlot: Component {
                    Icon { anchors.fill: parent; name: "target"
                            glyphColor: validateBtn.fgColor }
                }
                onClicked: { Page.validate() }
            }
            Text {                      // 状态行：校验结果 / 绑定项目 / 动作回执
                // ⚠️ 宽度**不能**从 toolbar.width 反推：toolbar.width 依赖
                //    toolRow.width，而 toolRow.width 又依赖本子项的宽度 ——
                //    那是绑定环，会把工具栏撑成整页宽（2026-09-30 取证里
                //    真的出现过：状态行那一条横贯全屏）。固定上限 + elide。
                width: 420
                height: 24
                verticalAlignment: Text.AlignVCenter
                text: Page.status
                elide: Text.ElideRight
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 11     // .tiny 11.5 → 11
                color: ThemeBridge.colors["text.muted"]
            }
        }
    }

    // ===================== 右侧：浮动参数面板（views.css:204-248） =====================
    Ctl {
        id: panel
        anchors { top: parent.top; topMargin: 16; right: parent.right; rightMargin: 16 }
        width: 348                                    // .float-panel width 348
        // .float-panel.folded { bottom: auto } —— 折叠后只剩 fp-h，画布拿回整幅宽度
        height: Page.panelFolded ? head.height : parent.height - 32
        radius: ThemeBridge.radii.lg                  // --r-lg
        color: ThemeBridge.colors["bg.panel"]          // QSS #floatPanel = %3 = bg.panel
        border.width: 1
        border.color: ThemeBridge.colors["line.subtle"]
        clip: true
        layer.enabled: ThemeBridge.layerEffectsAvailable && !root.reduce
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            shadowScale: 1.0
        }

        // —— fp-h：p12 14 · gap 9 · 13.5px w700 · 下边线 ——
        Item {
            id: head
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 47     // 12 + 22(.icon-btn.sm) + 12 + 1
            Text {
                id: headMark
                x: 15   // 边 1 + fp-h padding 14
                anchors.verticalCenter: parent.verticalCenter
                text: "▤"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 15
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                id: headTitle
                x: 39      // 15 + 15(icon) + 9(gap)
                anchors.verticalCenter: parent.verticalCenter
                // 设计稿 maxWidth 200；选了镜头时右边还要放 Tag（~60）与折叠钮（22），
                // 200 + 60 + 22 会顶出面板，所以有镜头时收窄到 180。
                width: Page.panelTitle.hasShot ? 180 : 240
                text: Page.panelTitle.text
                elide: Text.ElideRight
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13     // 13.5 → 13
                font.weight: Font.Bold
                color: ThemeBridge.colors["text.primary"]
            }
            Tag {
                anchors.left: headTitle.right
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                visible: Page.panelTitle.hasShot
                text: Page.panelTitle.tagText
                tone: Page.panelTitle.tagTone
            }
            IconBtn {
                anchors { right: parent.right; rightMargin: 15; verticalCenter: parent.verticalCenter }
                sm: true
                glyph: Page.panelFolded ? "▸" : "▾"
                tip: Page.panelFolded ? "展开面板" : "折叠 / 展开面板（画布拿回整幅宽度）"
                onClicked: { Page.togglePanel() }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: ThemeBridge.colors["line.subtle"]
            }
        }

        // —— fp-b：p8 12 14 · gap 10 · 溢出可滚 ——
        Flickable {
            id: body
            anchors { top: head.bottom; left: parent.left; right: parent.right; bottom: foot.top }
            visible: !Page.panelFolded
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: bodyCol.implicitHeight + 22

            Column {
                id: bodyCol
                x: 13      // 边 1 + fp-b padding 12
                y: 9       // 边 1 + fp-b padding 8
                width: body.width - 26
                spacing: 10

                Seg {
                    width: parent.width
                    options: root.segOptions
                    value: Page.tab
                    onPicked: (v) => { Page.setTab(v) }
                }

                // ───────── 页签 1 · 首尾帧链（ChainList） ─────────
                Column {
                    width: parent.width
                    spacing: 8
                    visible: Page.tab === "chain"
                    Repeater {
                        model: Page.chainRows
                        delegate: DenseRow {
                            id: chainRow
                            required property var modelData
                            required property int index
                            width: parent.width
                            last: index === Page.chainRows.length - 1
                            Text {          // 镜号（两端都是）：mono 11px accent w700
                                x: 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: 38
                                text: chainRow.modelData.fromCode
                                font.family: root.monoFont
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                color: ThemeBridge.colors["accent.primary"]
                            }
                            Text {          // .chevron 10px
                                x: 46
                                anchors.verticalCenter: parent.verticalCenter
                                width: 10
                                text: "›"
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 10
                                color: ThemeBridge.colors["text.muted"]
                            }
                            Text {
                                x: 64
                                anchors.verticalCenter: parent.verticalCenter
                                width: 38
                                text: chainRow.modelData.toCode
                                font.family: root.monoFont
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                color: ThemeBridge.colors["accent.primary"]
                            }
                            Tag {           // ✔ 已连接 / ⚠ 断链
                                id: chainTag
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: chainRow.modelData.tagText
                                tone: chainRow.modelData.tagTone
                            }
                            Text {          // 策略（.ellipsis grow）
                                x: 110
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, chainRow.bodyW - 110 - 8 - chainTag.width)
                                text: chainRow.modelData.policy
                                elide: Text.ElideRight
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 11
                                color: ThemeBridge.colors["text.muted"]
                            }
                            // ⚠️ 链段的 detail（flow::VideoChainLink::detail）**没有**
                            //  出口：迁移前挂在 QLabel 的 tooltip 上，而设计稿的
                            //  `[data-tip]::after` 与 QML 都没有对应（与 IconBtn.tip
                            //  同一个已记录缺口，见 qml-kit）。本仓刻意不 import
                            //  QtQuick.Controls（它的 Button 会盖掉同目录的共享
                            //  Button.qml），所以这里没有 ToolTip 可用。
                            //  断链的处置建议已经拼进 policy 文本，不靠 tooltip 传达。
                        }
                    }
                    Text {              // 脚注用**真实汇总**（镜头 N · 断链 M），不是常量文案
                        width: parent.width
                        text: Page.chainState.summary
                        wrapMode: Text.WordWrap
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 11
                        color: ThemeBridge.colors["text.muted"]
                    }
                }

                // ───────── 页签 2 · 视频任务（TaskList） ─────────
                Column {
                    width: parent.width
                    spacing: 8
                    visible: Page.tab === "task"
                    // 只有两个**真有实现**的按钮：设计稿的「停止」在 Widgets 版
                    // 就没有实现（那一格是「演示运行」），本页不做没接线的死按钮。
                    Row {
                        x: 0
                        spacing: 8
                        Button { text: "全部入队"; variant: "primary"; sm: true
                                 onClicked: { Page.enqueueAll() } }
                        Button { text: "演示运行"; variant: "secondary"; sm: true
                                 onClicked: { Page.showRunning() } }
                    }
                    Repeater {
                        model: Page.taskRows
                        delegate: DenseRow {
                            id: taskRow
                            required property var modelData
                            required property int index
                            width: parent.width
                            last: index === Page.taskRows.length - 1
                            Text {          // 镜号 w30
                                x: 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30
                                text: taskRow.modelData.code
                                font.family: root.monoFont
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                color: ThemeBridge.colors["accent.primary"]
                            }
                            Text {          // 首帧态 w62
                                x: 38
                                anchors.verticalCenter: parent.verticalCenter
                                width: 62
                                text: taskRow.modelData.frameText
                                elide: Text.ElideRight
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 11
                                color: ThemeBridge.colors["text.muted"]
                            }
                            Tag {
                                id: taskTag
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: taskRow.modelData.tagText
                                tone: taskRow.modelData.tagTone
                                dot: taskRow.modelData.run
                            }
                            Text {          // 百分比 w32 右对齐
                                anchors.right: taskTag.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 32
                                horizontalAlignment: Text.AlignRight
                                text: Math.round(taskRow.modelData.progress) + "%"
                                font.family: root.monoFont
                                font.pixelSize: 11
                                color: ThemeBridge.colors["text.muted"]
                            }
                            Progress {      // .grow
                                x: 108
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, taskRow.bodyW - 108 - 32 - 8 - taskTag.width - 8)
                                value: taskRow.modelData.progress
                                run: taskRow.modelData.run
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: Page.taskState.summary
                        wrapMode: Text.WordWrap
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 11
                        color: ThemeBridge.colors["text.muted"]
                    }
                }

                // ───────── 页签 3 · 成片（CutPanel） ─────────
                Column {
                    width: parent.width
                    spacing: 10
                    visible: Page.tab === "cut"
                    Row {
                        x: 0
                        spacing: 8
                        Button {
                            id: playBtn
                            text: "系统播放器连播"
                            variant: "primary"
                            sm: true
                            iconSlot: Component {
                                Icon { anchors.fill: parent; name: "play"
                                        glyphColor: playBtn.fgColor }
                            }
                            onClicked: { Page.playAll() }
                        }
                        Button {
                            id: exportBtn
                            text: "导出一场"
                            variant: "secondary"
                            sm: true
                            iconSlot: Component {
                                Icon { anchors.fill: parent; name: "download"
                                        glyphColor: exportBtn.fgColor }
                            }
                            onClicked: { Page.exportScene() }
                        }
                    }
                    // 概览卡（.card + .kv）。键列宽固定 48：三个键都是两三个汉字，
                    // 实测宽度与 48 差 1–2px，固定值比给共享 Kv 加一个按行 token
                    // 的机制划算（值列仍按行取 token，缺失行是 status.danger）。
                    Ctl {
                        width: parent.width
                        height: 24 + cutKvCol.implicitHeight
                        radius: ThemeBridge.radii.md     // .card --r-md
                        color: ThemeBridge.colors["bg.elevated"]
                        border.width: 1
                        border.color: ThemeBridge.colors["line.subtle"]
                        Column {
                            id: cutKvCol
                            x: 12
                            y: 12
                            width: parent.width - 24
                            spacing: 6               // .kv gap 6px 14px（行 6）
                            Repeater {
                                model: Page.cutKv
                                delegate: Item {
                                    id: kvLine
                                    required property var modelData
                                    required property int index
                                    width: cutKvCol.width
                                    height: 20          // 12.5 × 1.6（就近取整 12）
                                    Text {
                                        x: 0
                                        width: 48
                                        height: parent.height
                                        verticalAlignment: Text.AlignVCenter
                                        text: kvLine.modelData.key
                                        font.family: ThemeBridge.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeBridge.colors["text.muted"]
                                    }
                                    Text {
                                        x: 62             // 48 + colGap 14
                                        width: Math.max(0, cutKvCol.width - x)
                                        height: parent.height
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        text: kvLine.modelData.value
                                        font.family: ThemeBridge.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: ThemeBridge.colors[kvLine.modelData.valueToken]
                                    }
                                }
                            }
                        }
                    }
                    Repeater {
                        model: Page.cutRows
                        delegate: DenseRow {
                            id: cutRow
                            required property var modelData
                            required property int index
                            width: parent.width
                            last: index === Page.cutRows.length - 1
                            Text {
                                x: 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: 40
                                text: cutRow.modelData.code
                                font.family: root.monoFont
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                color: ThemeBridge.colors["accent.primary"]
                            }
                            Tag {
                                id: cutTag
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: cutRow.modelData.tagText
                                tone: cutRow.modelData.tagTone
                            }
                            Text {
                                x: 48
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, cutRow.bodyW - 48 - 8 - cutTag.width)
                                text: cutRow.modelData.path
                                elide: Text.ElideRight
                                font.family: root.monoFont
                                font.pixelSize: 11
                                color: ThemeBridge.colors["text.muted"]
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: Page.cutState.summary
                        wrapMode: Text.WordWrap
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 11
                        color: ThemeBridge.colors["text.muted"]
                    }
                }
            }

            // 滚动条：内容超出才出现（与 ImageFlow 的竖向滚动条同一做法）
            Rectangle {
                visible: body.contentHeight > body.height
                opacity: 0.6
                x: body.width - width
                y: body.contentY * Math.max(0, body.height - height)
                      / Math.max(1, body.contentHeight - body.height)
                width: 4
                height: Math.min(48, body.height * body.height / Math.max(1, body.contentHeight))
                radius: 2
                color: ThemeBridge.colors["line.strong"]
            }
        }

        // —— fp-f：上边线 · p10 14 · 一行（DStatusDot + 状态 + 右侧规格）——
        Item {
            id: foot
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 41     // 1 + 10 + 21 + 10（脚注那行 21）
            visible: !Page.panelFolded
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: ThemeBridge.colors["line.subtle"]
            }
            Row {
                x: 15      // 边 1 + fp-f padding 14
                y: 11      // 边 1 + fp-f padding 10
                spacing: 8
                width: parent.width - 30
                Dot {
                    // 共享 Dot 的 tone 是**语义名**；父级是 Row，共享 Dot 只给
                    // implicit 尺寸，所以显式写 7×7（.dot 7px，ui.css:164-169）。
                    width: 7
                    height: 7
                    anchors.verticalCenter: parent.verticalCenter
                    tone: Page.panelFoot.dotTone
                    run: Page.panelFoot.run
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, foot.width - 30 - 8 - footSpec.width - 8)
                    text: Page.panelFoot.text
                    elide: Text.ElideRight
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 11
                    color: ThemeBridge.colors["text.muted"]
                }
                Text {
                    id: footSpec
                    anchors.verticalCenter: parent.verticalCenter
                    text: Page.panelFoot.spec
                    font.family: root.monoFont
                    font.pixelSize: 11
                    color: ThemeBridge.colors["text.muted"]
                }
            }
        }
    }

    // ===================== 底部：连播胶片条（views.css:250-270 的 .float-strip） =====================
    // left 16 / bottom 16 / right 384（384 = 面板 348 + 外缩 16 + 余量 20），
    // 横向可滚（overflow-x: auto）。
    Ctl {
        id: strip
        anchors { left: parent.left; leftMargin: 16; bottom: parent.bottom; bottomMargin: 16
                  right: parent.right; rightMargin: 384 }
        height: filmRow.implicitHeight + 20      // padding 10 上下
        radius: ThemeBridge.radii.lg             // --r-lg
        color: ThemeBridge.colors["bg.elevated"] // 迁移前 FilmStrip 的 QSS 同款
        border.width: 1
        border.color: ThemeBridge.colors["line.subtle"]
        clip: true
        layer.enabled: ThemeBridge.layerEffectsAvailable && !root.reduce
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            shadowScale: 1.0
        }

        Flickable {
            id: stripScroll
            anchors { left: parent.left; leftMargin: 12; right: parent.right; rightMargin: 12
                      top: parent.top; topMargin: 10; bottom: parent.bottom; bottomMargin: 10 }
            contentWidth: filmRow.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            Row {
                id: filmRow
                x: 0
                y: 0
                spacing: 10    // .float-strip gap 10
                // 固定前缀块：胶片图标 + 「成片」（flex:none，padding-right 4）
                Item {
                    width: 44
                    height: 86
                    Row {
                        x: 0
                        y: 0
                        spacing: 4
                        Text {
                            width: 15
                            text: "◈"
                            font.family: ThemeBridge.fontFamily
                            font.pixelSize: 15
                            color: ThemeBridge.colors["accent.secondary"]
                        }
                        Text {
                            text: "成片"
                            font.family: ThemeBridge.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            color: ThemeBridge.colors["text.primary"]
                        }
                    }
                }
                Repeater {
                    model: Page.filmCells
                    delegate: FilmCell {
                        required property var modelData
                        required property int index
                        code: modelData.code
                        duration: modelData.duration
                        ready: modelData.ready
                        playing: modelData.picked
                        videoPath: modelData.videoPath
                        onPicked: { Page.pickFilmCell(index) }
                    }
                }
            }
        }

        // 横向滚动条：内容超出才出现
        Rectangle {
            visible: stripScroll.contentWidth > stripScroll.width
            opacity: 0.6
            x: 12 + stripScroll.contentX * (stripScroll.width - 4)
                     / Math.max(1, stripScroll.contentWidth - stripScroll.width)
            y: parent.height - 6
            width: Math.max(24, stripScroll.width * stripScroll.width
                                   / Math.max(1, stripScroll.contentWidth))
            height: 3
            radius: 1.5
            color: ThemeBridge.colors["line.strong"]
        }
    }

    // ⚠️ ThemeBridge 只桥出了每主题的 UI 字体族，没有 --font-mono 那一档
    // （tokens.css:31 的 Cascadia Code / JetBrains Mono / Consolas 链）。
    // 这是本页唯一一处非桥取值，与 CanvasNode / FilmCell 取同一个替身。
    readonly property string monoFont: "Consolas"

    // ===================== 取证裁剪区（宿主按名字读，属性而非方法） =====================
    // 为什么是属性：QML 的 function 能用 QMetaObject 调，但参数类型要对上；
    // 属性用 QObject::property() 读是零风险的。**几何由 QML 自己报** ——
    // 宿主不重算一套面板尺寸，两边各算一份必然漂。
    //
    // 迁移前 P08 是分别抓 Canvas() / Chain() / Tasks() / Final() 四个**子控件**；
    // 迁到 QML 后整页只有一个 QQuickWidget，子控件抓不到了，改成「抓整页 + 裁这里」。
    // 像素仍是同一次真实抓帧，裁剪不换视口、不拉高宿主。
    readonly property rect regionPanel: Qt.rect(panel.x, panel.y, panel.width, panel.height)
    // 画布区：右让出 fitInset（面板 348 + 外缩 16 + 余量），与 fit() 的可视区一致
    readonly property rect regionCanvas: Qt.rect(0, 0, Math.max(0, root.width - root.fitInset), root.height)
    readonly property rect regionStrip: Qt.rect(strip.x, strip.y, strip.width, strip.height)
    readonly property rect regionToolbar: Qt.rect(toolbar.x, toolbar.y, toolbar.width, toolbar.height)
}
