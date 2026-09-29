// src/ui/qml/ImageFlow.qml —— 出图工作区（QML 迁移）
//
// 对照 webui/src/views/ImageFlow.jsx（245 行），部件几何与配色逐条取自
// views.css:174-264（浮动工具栏 / 浮动面板 / 密集行）、views.css:1017-1113
// （画布 / 节点 / 画布工具）、views.css:1157-1179（评审清单）与 ui.css 的
// .btn / .tag / .seg / .prog / .dot / .spin / .art / .kv / .icon-btn。
// 数据逐条抄自 webui/src/data/mock.js 的 IMAGE_NODES / IMAGE_LINKS / BINDINGS /
// REVIEW_ITEMS / SHOTS。
//
// ⚠️ 根对象与定位纪律（与 widgets 侧 QQuickWidget 宿主一致）：
// 1. 根是 Ctl（Rectangle），不是 Window —— Window 会报 `invalid root object.`；
//    尺寸由宿主给（SizeRootObjectToView），本页只画**内容区**：画布 + 浮动工具栏
//    + 浮动参数面板，外壳（顶栏/侧栏/状态栏）归 src/ui/pages/shell/。
// 2. 页面这一层不套定位器：浮动工具栏/面板/画布工具都是显式锚点 + 显式 x/y，
//    hover 只改 color，不做 translateY —— 与 Gallery.qml 的纪律 2 同源。
// 3. 所有色值走 ThemeBridge（属性，不是方法）。唯一两个「非桥」取值是字符图标
//    与等宽字体名，文件头已注明。
//
// ⚠️ 共享套件迁移（本页 6 个页私有件 → 共享 Button/IconBtn/Seg/Art/Dot/Progress）后
//    有四处**刻意**不再复刻旧实现，都是往设计稿靠，理由写在这里免得下次被当回归改回去：
//    1. 按钮/图标钮的 `icon:` 改名 `glyph:`（共享件统一叫法，属性名不变的是
//       text / variant / sm / clicked）。那批页私有件已无人引用，待主 Agent 统一删除。
//    2. 分段控件的选中段宽度：设计稿 ui.css:215-242 是「文字 + 13 + 13 + margin-left 6
//       + 圆点 4」= 文字 + 36，旧实现尾部**多算了一个 6**（文字 + 42），连带未选中段也被
//       套 Row 撑宽。共享 Seg 取 36，是对的，不要改回去。
//    3. 两处 <Art>：旧实现是**无种子**的固定三色（bg.void / accent.primary / status.warn），
//       共享 Art 是按种子取 12 组 ART_PAL（UI.jsx:184-197，%12）。
//       这里给 seed = shot.id + 4（= 9，照抄 ImageFlow.jsx:157），而不是留默认 0。
//       后果：光源色从 status.warn 变成该组调色板的第三色（seed 9 → status.ok），
//       山脊拐点也随种子动 —— 这是向设计稿靠的必然视觉变化，出图确认。
//    4. 进度条的 shimmer 现在扫**整条槽**而不是只扫填充条（ui.css:444-451 的
//       ::after 包含块是 .prog 自己）；且 value 是 real，不再被 int 截断。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects  // MultiEffect（浮动面板的 --shadow-2 / 节点的 --shadow-1）
import Shine 1.0
// ⚠️ 连线用的 QtQuick.Shapes 只在 ImageFlowLink.qml 里 import —— 本文件不直接
// 出现 Shape/ShapePath 类型，import 放在这里会报 unused-imports。

Ctl {
    id: root

    // .flow-canvas { background: 点阵 + --fill-muted } —— 本页根即画布底
    color: ThemeBridge.colors["fill.muted"]

    // ===================== 数据（mock.js 的 IMAGE_* / BINDINGS / REVIEW_ITEMS） =====================
    // state 取值：done / run / fail / todo（FlowCanvas.jsx 的 STATE_COLOR）
    readonly property var nodes: [
        { id: "n1", x: 30,  y: 60,  title: "分镜提示词", icon: "≡", sub: "shots.prompt_v3",           state: "done" },
        { id: "n2", x: 235, y: 30,  title: "LoRA 载入",  icon: "▤", sub: "lamp_style_v2 · 0.8",      state: "done" },
        { id: "n3", x: 235, y: 160, title: "Checkpoint", icon: "▦", sub: "sdXL_light_04",            state: "done" },
        { id: "n4", x: 440, y: 95,  title: "K 采样器",   icon: "∿", sub: "28 步 · cfg 6.5",          state: "run"  },
        { id: "n5", x: 645, y: 95,  title: "VAE 解码",   icon: "◍", sub: "taesd",                    state: "todo" },
        { id: "n6", x: 850, y: 95,  title: "保存图像",   icon: "▣", sub: "output/shots/S12",        state: "todo" }
    ]
    readonly property var links: [["n1", "n4"], ["n2", "n4"], ["n3", "n4"], ["n4", "n5"], ["n5", "n6"]]

    readonly property var bindings: [
        { from: "shot.action",  to: "CLIPTextEncode.text", ok: true,  prev: "雨夜，镜头推近亮起的灯笼…" },
        { from: "shot.mood",    to: "CLIPTextEncode.style", ok: true, prev: "静谧" },
        { from: "entity.lamp",  to: "LoRALoader.lora_name", ok: true, prev: "lamp_style_v2" },
        { from: "shot.seed",    to: "KSampler.seed",        ok: false, prev: "缺数据 · 需先运行 V5" },
        { from: "shot.dur",     to: "帧数估计.frames",       ok: true,  prev: "112 帧 @ 24fps" }
    ]

    readonly property var reviewItems: [
        { name: "主体一致（与设定集）",   state: "pass" },
        { name: "光线方向（与机位）",     state: "pass" },
        { name: "服饰细节（与服装层）",   state: "pass" },
        { name: "构图（与分镜框）",       state: "warn" },
        { name: "色彩基调（与全片 LUT）", state: "fail" }
    ]

    readonly property var segOptions: [
        { value: "bind",   label: "绑定" },
        { value: "batch",  label: "批量出图" },
        { value: "review", label: "图评审" },
        { value: "result", label: "结果" }
    ]

    // 批量队列（ImageFlow.jsx:36-40 的初值：done×2 / running×2 / todo×2，
    // 进度 [100,100,62,18,0,0]，i===4 一行带 ⚠ 图标）
    property var batch: [
        { code: "S01", ch: 1, pct: 100, st: "done"    },
        { code: "S02", ch: 2, pct: 100, st: "done"    },
        { code: "S11", ch: 3, pct: 62,  st: "running" },
        { code: "S12", ch: 3, pct: 18,  st: "running" },
        { code: "S13", ch: 3, pct: 0,   st: "todo"    },
        { code: "S14", ch: 3, pct: 0,   st: "todo"    }
    ]

    // 选中镜头（ImageFlow.jsx:195 的 `SHOTS.find(...) || SHOTS[4]` → id 5 = S05）
    // ⚠️ shotId 是两处 <Art> 的 seed 派生源：设计稿 ImageFlow.jsx:157 写的是
    //    `<Art seed={shot.id + 4} …>`，而 mock.js:276-296 的 buildShots 里 n 从 0 起
    //    **跨章节累加**（每章内部再自增），所以 SHOTS[4].id === 5 → seed 9。
    //    Art 默认 seed 0，全页两个实例都留 0 会长成同一张；这里显式给确定性种子。
    readonly property int shotId: 5
    readonly property int shotArtSeed: shotId + 4      // ImageFlow.jsx:157
    readonly property string shotCode: "S05"
    readonly property string shotChapter: "第 2 章 · 长街灯影"
    readonly property string shotScene: "场景 1"
    readonly property string shotAction: "两盏灯的光在雾里交汇"
    readonly property string shotStatusLabel: "完成"
    readonly property string shotStatusTone: "ok"
    readonly property bool shotDone: true

    // ===================== 视图状态 =====================
    property string tab: "bind"
    property bool folded: false
    property bool comfy: true
    property string selNode: ""
    property bool batchRunning: false
    property bool reviewed: false

    // 画布取景（FlowCanvas.jsx:31/36-52）
    property real zoom: 1
    property real viewX: 40
    property real viewY: 30
    readonly property int fitInset: 380    // <FlowCanvas fitInset={380}> = 面板 348 + 左右 16×2 - 16
    readonly property int fitPad: 48       // fit() 里的 -48
    readonly property int bboxPad: 24      // fit() 包围盒的 ±24 留白
    readonly property int nodeW: 150       // NODE_W
    readonly property int nodeBoxH: 76     // NODE_H（只用于 fit 包围盒）
    readonly property int portOffset: 38   // portPos 的 node.y + 38
    // 端口圆心相对卡片左上角（views.css:1086-1097：.port 9px 宽、left/right -5.5）
    //   in  圆心 = -5.5 + 4.5 = -1
    //   out 圆心 = 150 + 5.5 - 4.5 = 151
    // 连线端点取这两个值，才是真的落在锚点上（差 1px 也是差）。
    readonly property real inPortX: -1
    readonly property real outPortX: 151
    readonly property string monoFont: "Consolas"   // tokens.css:31 的 --font-mono 链在 ThemeBridge 里没有对应项

    readonly property bool graphRunning: {
        var r = false
        for (var i = 0; i < root.nodes.length; ++i) {
            if (root.nodes[i].state === "run") { r = true; break }
        }
        return r
    }

    function nodeById(id) {
        for (var i = 0; i < root.nodes.length; ++i)
            if (root.nodes[i].id === id) return root.nodes[i]
        return null
    }

    // fit()：与 FlowCanvas.jsx:36-52 同式（availW 扣掉 fitInset，缩放上限 1.15）
    function fit() {
        // 宿主还没给尺寸时（QQuickWidget 装载瞬间 width/height 可能为 0）不取景，
        // 否则会先按 availW=200/availH=160 算出一个极小缩放，等 onWidthChanged 再跳。
        if (root.width < 2 || root.height < 2) return
        var minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9
        for (var i = 0; i < root.nodes.length; ++i) {
            var n = root.nodes[i]
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

    // 批量队列推进（ImageFlow.jsx:42-59 的 420ms 定时器：运行行 +9%，满则转 done，
    // 无运行行时把下一个 todo 提为 running；全 done 则停）
    function advanceBatch() {
        var next = []
        for (var i = 0; i < root.batch.length; ++i) {
            var r = root.batch[i]
            next.push({ code: r.code, ch: r.ch, pct: r.pct, st: r.st })
        }
        var run = -1, todo = -1
        for (var k = 0; k < next.length; ++k) {
            if (next[k].st === "running" && run < 0) run = k
            if (next[k].st === "todo" && todo < 0) todo = k
        }
        if (run < 0) {
            if (todo < 0) { root.batchRunning = false; return }
            next[todo].st = "running"
        } else {
            next[run].pct = Math.min(100, next[run].pct + 9)
            if (next[run].pct >= 100) next[run].st = "done"
        }
        root.batch = next
    }

    // 宿主尺寸 → 重新取景。**不用** onWidthChanged / onHeightChanged 两个处理器：
    //   ① 处理器体一律写 { } 块。单表达式体（`onWidthChanged: dotGrid.requestPaint()`）
    //      会被 QQml 当成「属性赋值」，同一句出现在两个 onXChanged 上就报
    //      `Property value set multiple times`（已踩，见 ImageFlow.qml 历史版本）。
    //   ② 宽高合成一个 fitKey，只留**一个**处理器做变化检测，避免重复处理器。
    readonly property string fitKey: Math.round(width) + "x" + Math.round(height)
    Component.onCompleted: { root.fit() }
    onFitKeyChanged: { root.fit() }

    Timer {
        id: batchTimer
        interval: 420
        repeat: true
        running: root.batchRunning
        onTriggered: { root.advanceBatch() }
    }

    // ===================== 画布底：点阵 =====================
    // views.css:1021-1023：radial-gradient(circle at 1px 1px, --line-normal 1px, transparent 0)
    //              0 0 / 22px 22px，底色 --fill-muted。点阵用 Canvas 一次画完
    //              （一屏 1600×900 是 3000 个点，3000 个 Rectangle 不划算）。
    Canvas {
        id: dotGrid
        anchors.fill: parent
        // Immediate：onPaint 里要读 ThemeBridge，Cooperative 会把 onPaint 挪到
        // 渲染线程（QML 对象跨线程不安全）。点阵只在尺寸/主题变化时重画，代价可忽略。
        renderStrategy: Canvas.Immediate
        // 尺寸 / 主题色任一变化都只靠**这一个**处理器重画（块体写法，见文件头纪律）。
        // 不再挂 onWidthChanged / onHeightChanged / Connections：前者是单表达式陷阱，
        // 后者与 fitKey 两处重画会互相覆盖。
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

    // 点空白处取消选中（FlowCanvas.jsx:99-103 的 onBgDown）
    MouseArea {
        anchors.fill: parent
        onClicked: { root.selNode = "" }
    }

    // ===================== 节点层（连线在下、节点在上） =====================
    //
    // ⚠️⚠️ 取景变换**不再**放在容器的 transform 列表里，而是写进坐标：
    //   节点   x/y = view + zoom * world，且自身 scale = zoom（原点左上）
    //   连线   ax..by = view + zoom * world（描边宽、虚线、dx 一并乘 zoom）
    // 原因：节点与连线必须落在同一个坐标系里，而这一页两类子项一个是 Item、
    // 一个是 Shape（各有各的尺寸/渲染路径）。用容器的 transform 时，只要两者
    // 对「谁吃到了变换」的理解有任何偏差（实测过一次：连线整束画到画外左上、
    // 汇聚成一个画外公共点），就是肉眼可见的整片错位。写进坐标之后，
    // 两者共用 viewX / viewY / zoom 三个属性，**物理上不可能错开**。
    // 缩放仍完全等价于设计稿的 `transform: translate(x,y) scale(z)` + `transform-origin: 0 0`。
    Repeater {
        model: root.links
        delegate: ImageFlowLink {
            required property var modelData
            readonly property var na: root.nodeById(modelData[0])
            readonly property var nb: root.nodeById(modelData[1])
            // 画在**未变换**的页面坐标系里，所以自己铺满整页
            x: 0
            y: 0
            width: root.width
            height: root.height
            // 出端口 = 节点右缘外 1px；y 与节点端口圆心同高（FlowCanvas.jsx:13 的 +38）
            ax: root.viewX + root.zoom * (na.x + root.outPortX)
            ay: root.viewY + root.zoom * (na.y + root.portOffset)
            bx: root.viewX + root.zoom * (nb.x + root.inPortX)
            by: root.viewY + root.zoom * (nb.y + root.portOffset)
            zoom: root.zoom
            // active：两端节点都不是 todo（FlowCanvas.jsx:154）
            active: na.state !== "todo" && nb.state !== "todo"
        }
    }

    Item {
        id: flowInner
        anchors.fill: parent

        Repeater {
            model: root.nodes
            delegate: ImageFlowNode {
                required property var modelData
                x: root.viewX + root.zoom * modelData.x
                y: root.viewY + root.zoom * modelData.y
                scale: root.zoom
                transformOrigin: Item.TopLeft     // == CSS transform-origin: 0 0
                title: modelData.title
                sub: modelData.sub
                iconGlyph: modelData.icon
                nodeState: modelData.state
                selected: root.selNode === modelData.id
                onPicked: { root.selNode = modelData.id }
            }
        }
    }

    // ===================== 左下：画布工具（.canvas-tools.bl） =====================
    Ctl {
        id: canvasTools
        anchors { left: parent.left; leftMargin: 16; bottom: parent.bottom; bottomMargin: 16 }
        width: 32        // padding 4×2 + 22(.icon-btn.sm) + 边 1×2
        // 跑动时多一段 spin：4(pad) + 22×3 + gap 4×2 + (4 margin + 11 spin) + 4(pad) + 边 2 = 99
        height: root.graphRunning ? 99 : 80
        radius: ThemeBridge.radii.md
        color: ThemeBridge.colors["bg.surface"]   // --glass → 与 QSS #floatPanel 同一决定
        border.width: 1
        border.color: ThemeBridge.colors["line.subtle"]

        Column {
            x: 5      // 边 1 + padding 4
            y: 5
            spacing: 4
            IconBtn { sm: true; glyph: "＋"; onClicked: root.zoom = Math.min(2, root.zoom * 1.2) }
            IconBtn { sm: true; glyph: "×"; onClicked: root.zoom = Math.max(0.35, root.zoom / 1.2) }
            IconBtn { sm: true; glyph: "◎"; onClicked: root.fit() }
            // running 时的 spin sm（margin: 4px auto 0 → 顶部留 4、水平居中）
            Item {
                width: 11
                height: 15
                visible: root.graphRunning
                Spinner {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    sm: true
                }
            }
        }
    }

    // ===================== 左上：浮动工具栏（views.css:183-203） =====================
    Ctl {
        id: toolbar
        anchors { left: parent.left; leftMargin: 16; top: parent.top; topMargin: 16 }
        width: toolRow.width + 26      // padding 12 两侧 + 边 1×2
        height: 42                     // padding 8 + 按钮 24 + 边 1×2
        radius: ThemeBridge.radii.md
        color: ThemeBridge.colors["bg.panel"]   // QSS #floatToolbar：bg-panel + 1px 边 + 10 圆角
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
                text: "出图流程 · 分镜图_v3"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12     // .small 12.5 → 12
                font.weight: Font.DemiBold
                color: ThemeBridge.colors["text.primary"]
            }
            // .float-toolbar .vsep：1×18，--line-normal，左右各 2 → 占宽 5
            Item {
                width: 5
                height: 24
                Rectangle {
                    x: 2
                    y: 3               // (24 - 18) / 2
                    width: 1
                    height: 18
                    color: ThemeBridge.colors["line.normal"]
                }
            }
            Button { text: "导入";        variant: "ghost";     sm: true; glyph: "↑" }
            Button { text: "导出";        variant: "ghost";     sm: true; glyph: "↓" }
            Button { text: "提交前校验";  variant: "secondary"; sm: true; glyph: "◎" }
            Button { text: "批量出图";    variant: "primary";   sm: true; glyph: "▶"
                     onClicked: { root.tab = "batch" } }
        }
    }

    // ===================== 右侧：浮动参数面板（views.css:204-248） =====================
    Ctl {
        id: panel
        anchors { top: parent.top; topMargin: 16; right: parent.right; rightMargin: 16 }
        width: 348                                          // .float-panel width 348px
        height: root.folded ? head.height : parent.height - 32
        radius: ThemeBridge.radii.lg                        // --r-lg
        // --glass 在 Qt 侧退化为 bg-surface（与 QssBuilder:688 的 #floatPanel 同一决定；
        // backdrop-filter: blur(16px) 在 QML 无对应，不做伪模糊）
        color: ThemeBridge.colors["bg.surface"]
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
                text: "◈"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 15
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                id: headTitle
                x: 39      // 15 + 15(icon) + 9(gap)
                anchors.verticalCenter: parent.verticalCenter
                width: 210 // 设计稿的 maxWidth 210
                text: root.shotCode + " · " + root.shotAction
                elide: Text.ElideRight
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13     // 13.5 → 13
                font.weight: Font.Bold
                color: ThemeBridge.colors["text.primary"]
            }
            Tag {
                id: headTag
                x: 258     // 39 + 210 + 9
                anchors.verticalCenter: parent.verticalCenter
                text: root.shotStatusLabel
                tone: root.shotStatusTone
            }
            IconBtn {
                anchors { right: parent.right; rightMargin: 15; verticalCenter: parent.verticalCenter }
                sm: true
                glyph: root.folded ? "▸" : "▾"
                onClicked: { root.folded = !root.folded }
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
            visible: !root.folded          // .float-panel.folded .fp-b { display: none }
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: bodyCol.implicitHeight + 22

            Column {
                id: bodyCol
                x: 13      // 边 1 + fp-b padding 12
                y: 9       // 边 1 + fp-b padding 8
                width: body.width - 26   // 348 - 边 1×2 - padding 12×2
                spacing: 10

                Seg {
                    width: parent.width
                    options: root.segOptions
                    value: root.tab
                    onPicked: (v) => { root.tab = v }
                }

                // ───────── 页签 1 · 绑定 ─────────
                Column {
                    width: parent.width
                    spacing: 8      // .col gap-2
                    visible: root.tab === "bind"

                    Column {
                        width: parent.width
                        spacing: 0   // .dlist
                        Repeater {
                            model: root.bindings
                            delegate: ImageFlowListItem {
                                id: bindRow
                                required property var modelData
                                required property int index
                                width: parent.width
                                height: 47     // 8 + 15 + 3 + 13 + 8（.drow p8 2 · gap 3）
                                last: index === root.bindings.length - 1
                                Item {
                                    id: line1
                                    width: parent.width
                                    height: 15
                                    Text {
                                        id: fromText
                                        x: 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: bindRow.modelData.from
                                        font.family: root.monoFont
                                        font.pixelSize: 12     // .mono 12
                                        font.weight: Font.DemiBold
                                        color: ThemeBridge.colors["accent.primary"]
                                    }
                                    Text {
                                        x: fromText.width + 8      // .row gap-2 = 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "›"
                                        font.family: ThemeBridge.fontFamily
                                        font.pixelSize: 10        // Icon chevron 10px
                                        color: ThemeBridge.colors["text.muted"]
                                    }
                                    Tag {
                                        id: okTag
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: bindRow.modelData.ok ? "✔ 可用" : "✘ 缺数据"
                                        tone: bindRow.modelData.ok ? "ok" : "danger"
                                    }
                                    Text {
                                        x: fromText.x + fromText.width + 26   // 18(宽) + 8 + 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.max(0, okTag.x - 8 - x)
                                        text: bindRow.modelData.to
                                        elide: Text.ElideRight
                                        font.family: root.monoFont
                                        font.pixelSize: 12
                                        color: ThemeBridge.colors["text.secondary"]
                                    }
                                }
                                Text {
                                    x: 0
                                    y: 18        // 15 + gap 3
                                    width: parent.width - 4
                                    text: bindRow.modelData.prev
                                    elide: Text.ElideRight
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 11     // .dsub 11px
                                    color: ThemeBridge.colors["text.muted"]
                                }
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: "发现 1 个提交前错误：KSampler.seed 缺数据 · 需先运行 V5 表演标注"
                        wrapMode: Text.WordWrap
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12     // .tiny 12
                        color: ThemeBridge.colors["status.danger"]
                    }
                }

                // ───────── 页签 2 · 批量出图 ─────────
                Column {
                    width: parent.width
                    spacing: 8
                    visible: root.tab === "batch"

                    Item {
                        width: parent.width
                        height: 24
                        Button {
                            id: enqueueBtn
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: "全部入队"
                            variant: "primary"
                            sm: true
                            glyph: "▶"
                            enabled: root.comfy     // disabled={!comfy}
                            onClicked: { root.batchRunning = true }
                        }
                        Button {
                            id: stopBtn
                            x: enqueueBtn.width + 8    // .row gap-2
                            anchors.verticalCenter: parent.verticalCenter
                            text: "中断 / 清队列"
                            variant: "secondary"
                            sm: true
                            glyph: "■"
                            onClicked: { root.batchRunning = false }
                        }
                        Text {
                            // ⚠️ 原来这里写死 116（= 旧页私有件按「固定 icon 盒 13」算出的
                            // 按钮宽）。共享 Button 的 implicitWidth 改成按**字形实际
                            // advance** 算（Button.qml:87-89），常数不再成立，改读 stopBtn
                            // 的实测宽，避免文字压到按钮上。
                            x: enqueueBtn.width + 8 + stopBtn.width + 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !root.comfy
                            text: "ComfyUI 未连接 · 底部「连接」"
                            font.family: ThemeBridge.fontFamily
                            font.pixelSize: 12
                            color: ThemeBridge.colors["status.warn"]
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 0
                        Repeater {
                            model: root.batch
                            delegate: ImageFlowListItem {
                                id: batchRow
                                required property var modelData
                                required property int index
                                width: parent.width
                                height: 36     // 8 + 20(.tag) + 8
                                last: index === root.batch.length - 1
                                Item {
                                    id: brow
                                    width: parent.width
                                    height: 20
                                    Text {
                                        x: 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 18
                                        text: ("0" + (batchRow.index + 1)).slice(-2)
                                        font.family: root.monoFont
                                        font.pixelSize: 12
                                        color: ThemeBridge.colors["text.muted"]
                                    }
                                    Text {
                                        x: 26
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 30
                                        text: batchRow.modelData.code
                                        font.family: root.monoFont
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: ThemeBridge.colors["accent.primary"]
                                    }
                                    Text {
                                        x: 64
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 52
                                        text: "第" + batchRow.modelData.ch + "章"
                                        font.family: ThemeBridge.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeBridge.colors["text.muted"]
                                    }
                                    Text {
                                        visible: batchRow.index === 4
                                        x: 124
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 12
                                        text: "⚠"
                                        font.family: ThemeBridge.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeBridge.colors["status.warn"]
                                    }
                                    Tag {
                                        id: btag
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: batchRow.modelData.st === "todo" ? "排队"
                                            : batchRow.modelData.st === "running" ? "运行中" : "完成"
                                        tone: batchRow.modelData.st === "todo" ? "idle"
                                            : batchRow.modelData.st === "running" ? "busy" : "ok"
                                    }
                                    Text {
                                        x: Math.max(144, parent.width - 32 - 8 - btag.width)
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 32
                                        horizontalAlignment: Text.AlignRight
                                        text: batchRow.modelData.pct + "%"
                                        font.family: root.monoFont
                                        font.pixelSize: 12
                                        color: ThemeBridge.colors["text.muted"]
                                    }
                                    Progress {
                                        x: batchRow.index === 4 ? 144 : 124
                                        anchors.verticalCenter: parent.verticalCenter
                                        // ⚠️ 共享 Progress 只给 implicitHeight（6 / 4），
                                        // 而本调用点的父级是**裸 Item**（不套定位器，不会替
                                        // 子项套用 implicit 尺寸）—— 不显式给 height 就是 0 高、
                                        // 整条看不见。旧页私有件也是自带 height 的，这里保持。
                                        height: 4        // .prog.thin 4px
                                        width: Math.max(20, parent.width - x - 8 - 32 - 8 - btag.width)
                                        thin: true
                                        run: batchRow.modelData.st === "running"
                                        // value 是 real：pct 推进到 0.x 段时旧 int 属性会把小数
                                        // 静默截断，条子看着不动（Progress.qml 文件头）。
                                        value: batchRow.modelData.pct
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: "缺依赖先按 C 挂起，不阻塞其他资产；超期后按 B 降级并写 degradations.jsonl 与 audit_logs。"
                        wrapMode: Text.WordWrap
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        color: ThemeBridge.colors["text.muted"]
                    }
                }

                // ───────── 页签 3 · 图评审 ─────────
                Column {
                    width: parent.width
                    spacing: 12     // .col gap-3
                    visible: root.tab === "review"

                    Item {
                        width: parent.width
                        height: 24
                        Button {
                            id: reviewBtn
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: "评审当前图"
                            variant: "primary"
                            sm: true
                            glyph: "◉"
                            onClicked: { root.reviewed = true }
                        }
                        Button {
                            x: reviewBtn.width + 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "仅重跑 ⚠ 项"
                            variant: "secondary"
                            sm: true
                            glyph: "↻"
                        }
                    }

                    Art {
                        width: parent.width
                        height: 150
                        dimmed: !root.reviewed
                        // ⚠️ 设计稿这一处（ImageFlow.jsx:96-104）是**手写内联 SVG、根本没有
                        // 种子**；本页按「评审页与结果页看的是同一张镜头成图」取与结果页同一个
                        // 派生种子（shot.id + 4 = 9），而不是留 0。取舍见文件头第 3 条。
                        seed: root.shotArtSeed
                        // 未评审时的占位层（设计稿是 absolute inset 0 的居中说明）
                        Text {
                            visible: !root.reviewed
                            anchors.centerIn: parent
                            width: parent.width - 24
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "▣\n生成结果（点击「评审当前图」加载）"
                            font.family: ThemeBridge.fontFamily
                            font.pixelSize: 12     // .tiny 12
                            lineHeight: 1.4
                            color: ThemeBridge.colors["text.muted"]
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 6      // .checklist gap 6
                        Repeater {
                            model: root.reviewItems
                            delegate: Ctl {
                                width: parent.width
                                height: 36     // 7 + 20(.tag) + 7 + 边 1×2
                                radius: ThemeBridge.radii.sm
                                color: ThemeBridge.colors["fill.muted"]
                                border.width: 1
                                border.color: ThemeBridge.colors["line.subtle"]
                                id: ckRow
                                required property var modelData
                                readonly property string st: root.reviewed ? modelData.state : "todo"

                                Text {
                                    x: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14     // .ck-ic 14px
                                    text: parent.st === "pass" ? "✔" : parent.st === "warn" ? "⚠"
                                        : parent.st === "fail" ? "✘" : "◷"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 14
                                    color: parent.st === "pass" ? ThemeBridge.colors["status.ok"]
                                         : parent.st === "warn" ? ThemeBridge.colors["status.warn"]
                                         : parent.st === "fail" ? ThemeBridge.colors["status.danger"]
                                         : ThemeBridge.colors["text.muted"]
                                }
                                Text {
                                    x: 33         // 10 + 14 + 9(gap)
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.max(0, parent.width - 33 - 9 - ckTag.width - 10)
                                    text: ckRow.modelData.name
                                    elide: Text.ElideRight
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 12     // 12.5 → 12
                                    color: ThemeBridge.colors["text.secondary"]
                                }
                                Tag {
                                    id: ckTag
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: parent.st === "pass" ? "过" : parent.st === "warn" ? "⚠"
                                        : parent.st === "fail" ? "✘" : "待评审"
                                    tone: parent.st === "pass" ? "ok" : parent.st === "warn" ? "warn"
                                        : parent.st === "fail" ? "danger" : "idle"
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: "评审模型不可用时会提示配置视觉模型或显式切换 mock。"
                        wrapMode: Text.WordWrap
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        color: ThemeBridge.colors["text.muted"]
                    }
                }

                // ───────── 页签 4 · 结果 ─────────
                Column {
                    width: parent.width
                    spacing: 12
                    visible: root.tab === "result"

                    Art {
                        width: parent.width
                        height: 190
                        // ImageFlow.jsx:157 的 `<Art seed={shot.id + 4} cover …>` ——
                        // 照抄设计稿的派生式（shot.id = 5 → seed 9），不写死 0。
                        seed: root.shotArtSeed
                        // 未完成时压一层 scrim（设计稿是纯黑 45% 透明，
                        // 这里取 --bg-void 45% 透明，语义等价且不写死黑色）
                        Rectangle {
                            visible: !root.shotDone
                            anchors.fill: parent
                            color: Qt.alpha(ThemeBridge.colors["bg.void"], 0.45)
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "▣"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 22
                                    color: ThemeBridge.colors["text.muted"]
                                }
                                Text {
                                    text: root.shotStatusLabel + " · 出图后在此显示"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeBridge.colors["text.muted"]
                                }
                            }
                        }
                    }

                    // KV（ui.css:1015-1028：两列 auto/1fr，行距 6，列距 14）
                    Column {
                        width: parent.width
                        spacing: 6
                        Item { width: parent.width; height: 15
                            Text { x: 0; anchors.verticalCenter: parent.verticalCenter
                                   text: "所属章节"; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; color: ThemeBridge.colors["text.muted"] }
                            Text { x: 62; anchors.verticalCenter: parent.verticalCenter
                                   text: root.shotChapter; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; font.weight: Font.Medium
                                   color: ThemeBridge.colors["text.primary"] } }
                        Item { width: parent.width; height: 15
                            Text { x: 0; anchors.verticalCenter: parent.verticalCenter
                                   text: "场景"; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; color: ThemeBridge.colors["text.muted"] }
                            Text { x: 62; anchors.verticalCenter: parent.verticalCenter
                                   text: root.shotScene; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; font.weight: Font.Medium
                                   color: ThemeBridge.colors["text.primary"] } }
                        Item { width: parent.width; height: 15
                            Text { x: 0; anchors.verticalCenter: parent.verticalCenter
                                   text: "出图状态"; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; color: ThemeBridge.colors["text.muted"] }
                            Text { x: 62; anchors.verticalCenter: parent.verticalCenter
                                   text: root.shotStatusLabel; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; font.weight: Font.Medium
                                   color: ThemeBridge.colors["text.primary"] } }
                        Item { width: parent.width; height: 15
                            Text { x: 0; anchors.verticalCenter: parent.verticalCenter
                                   text: "Prompt"; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; color: ThemeBridge.colors["text.muted"] }
                            Text { x: 62; anchors.verticalCenter: parent.verticalCenter
                                   text: "v7（重生成 +1）"; font.family: ThemeBridge.fontFamily
                                   font.pixelSize: 12; font.weight: Font.Medium
                                   color: ThemeBridge.colors["text.primary"] } }
                    }

                    Column {
                        width: parent.width
                        spacing: 0
                        ImageFlowListItem {
                            width: parent.width
                            height: 36
                            clickable: true
                            Item {
                                width: parent.width
                                height: 20
                                Text {
                                    x: 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 13
                                    text: "▤"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 13
                                    color: ThemeBridge.colors["accent.secondary"]
                                }
                                Text {
                                    id: mp4Text
                                    x: 21    // 13 + gap 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.shotCode + ".mp4"
                                    font.family: root.monoFont
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: ThemeBridge.colors["text.primary"]
                                }
                                Text {
                                    x: mp4Text.x + mp4Text.width + 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "48fps · 4.2s"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeBridge.colors["text.muted"]
                                }
                                Tag {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "已出片"
                                    tone: "ok"
                                }
                            }
                        }
                        ImageFlowListItem {
                            width: parent.width
                            height: 32
                            clickable: true
                            last: true
                            Item {
                                width: parent.width
                                height: 16
                                Text {
                                    x: 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 13
                                    text: "◎"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 13
                                    color: ThemeBridge.colors["accent.info"]
                                }
                                Text {
                                    x: 21
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "图评审 · 五项清单"
                                    font.family: root.monoFont
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: ThemeBridge.colors["text.primary"]
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "›"
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: 10
                                    color: ThemeBridge.colors["text.muted"]
                                }
                            }
                        }
                    }

                    Button {
                        text: "重跑本镜出图"
                        variant: "secondary"
                        sm: true
                        glyph: "↻"
                    }
                }
            }

            // 滚动指示条（浏览器滚动条在 QML 无对应；3px 用 --line-strong）
            Rectangle {
                visible: body.contentHeight > body.height
                width: 3
                height: 60
                radius: 1.5
                color: ThemeBridge.colors["line.strong"]
                opacity: 0.6
                x: body.width - width
                y: body.contentY * Math.max(0, body.height - height)
                      / Math.max(1, body.contentHeight - body.height)
            }
        }

        // —— fp-f：上边线 · p10 14 ——
        Item {
            id: foot
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 21 + footCol.implicitHeight     // 1 + 10 + 22 + 6 + 24 + 10
            visible: !root.folded
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: ThemeBridge.colors["line.subtle"]
            }
            Column {
                id: footCol
                x: 15      // 边 1 + fp-f padding 14
                y: 11      // 边 1 + fp-f padding 10
                width: parent.width - 30
                spacing: 6     // 设计稿 fp-f 第二行的 margin-top 6

                Item {
                    width: parent.width
                    height: 22
                    Dot {
                        x: 0
                        anchors.verticalCenter: parent.verticalCenter
                        // ⚠️ 共享 Dot 的 `tone` 是**语义名**不是颜色：设计稿
                        // ImageFlow.jsx:231 写的就是 <StatusDot tone={comfy ? 'ok' : 'warn'} />。
                        // 直接给色要走 `toneColor:` 槽，这里有名可名，不该用逃生舱。
                        // 父级是裸 Item（不套 implicit 尺寸），共享 Dot 只给 implicitWidth/Height，
                        // 所以 7×7 显式写死（.dot 7px，ui.css:164-169）。
                        width: 7
                        height: 7
                        tone: root.comfy ? "ok" : "warn"
                        run: root.comfy
                    }
                    Text {
                        x: 15     // 7(.dot) + gap 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.comfy ? "已连接 · 执行中" : "ComfyUI 未连接"
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12     // .small 12.5 → 12
                        font.weight: Font.DemiBold
                        color: root.comfy ? ThemeBridge.colors["status.ok"]
                                          : ThemeBridge.colors["status.warn"]
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "127.0.0.1:8188"
                        font.family: root.monoFont
                        font.pixelSize: 12
                        color: ThemeBridge.colors["text.muted"]
                    }
                }

                Item {
                    width: parent.width
                    height: 24
                    Text {
                        x: 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: "队列剩余 2 · 运行 1 · 失败 0"
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        color: ThemeBridge.colors["text.muted"]
                    }
                    Button {
                        id: linkBtn
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.comfy ? "断开" : "连接"
                        variant: "secondary"
                        sm: true
                        onClicked: { root.comfy = !root.comfy }
                    }
                    Button {
                        anchors.right: linkBtn.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "释放 VRAM"
                        variant: "ghost"
                        sm: true
                    }
                }
            }
        }
    }
}
