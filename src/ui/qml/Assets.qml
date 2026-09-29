pragma ComponentBehavior: Bound
// src/ui/qml/Assets.qml —— QML 版资产页（Widgets 版已退役删除）
//
// 真值来源：webui/src/views/Assets.jsx（结构与内容）+ webui/src/styles/{views,ui,tokens,base}.css
// （部件级几何与配色）。下面每个数值后面的注释是它在那份 css 里的行号。
//
// 两个形态（jsx:134 的 `overview ? … : …`）：
//   总览   .vw 默认 padding 20/24/26 + gap 16，.vw-head + .asset-grid 卡片网格
//   详情   .vw 覆盖成 padding 10/18/18 + gap 0，三个 .vsec（设定集 / 一致性 / 关联时间线）
//
// ⚠️ 接缝纪律（都来自 QuickHost / QQuickWidget 的实测）：
//   1. 根是 Ctl（Rectangle），不能是 Window；尺寸由宿主 SizeRootObjectToView 给，
//      根上不写 width/height/visible。页面只画内容区，不画顶栏/侧栏/状态栏。
//   2. 页面层不用 Flow/Row/Grid 当布局容器：定位器接管子项 y，会和卡片 hover 的
//      translateY(-2px) 抢 y（binding loop，卡片全叠在第一行）。网格用
//      Repeater + 显式 x/y（列宽/行高算术），三个分区也是显式 y。
//   3. 竖排只出现在组件内部的 Row（按钮组、标题条尾部、时间线下方那一行）——
//      那里没有 hover 位移，定位器不与任何绑定抢几何。
//   4. 颜色全部走 ThemeBridge（本文件与 Assets*.qml 里没有一处硬编码色值）。
//   5. 动画一律先读 reduce（ThemeBridge.reduceMotion）；layer.enabled 必须写成
//      `ThemeBridge.layerEffectsAvailable && 条件` —— software 场景图后端下
//      layer.effect: MultiEffect 会把整个 item 吞掉（不是阴影没画，是控件不显示）。
//
// 部件全部走共享套件（docs/10-modules/qml-kit.md）：Button / Tag / Seg / Art / Kv /
// Chip / Ctl。本页不再有这 6 件的私有副本 —— 尺寸与配色以共享件为准（它们逐条对过
// CSS），本页只提供数据与摆放。
//
// 页面私有的四件（AssetsSecHead / AssetsAssetCard / AssetsCompare / AssetsTimeline）
// 之外又加了两件：AssetsRefLibrary（④ 参考库）与 AssetsPolicyPanel（⑤ 依赖策略），
// 对应已删除的 Widgets 版参考库 / 策略面板 —— 同样只做渲染与动作派发，
// 取数与真值全在 C++ 桥上。
//
// 数据：真值全部来自 C++ 注入的 `Page`（ui/pages/assets/AssetPageModel.h），
// 取数逻辑在 ui/pages/assets/AssetVisualData.h。由 QmlAssetsPage 在
// OpenBook / 选中变化时驱动重算。本文件不持有业务真值，只保留
// 「用哪个视图形态」这一条纯 UI 状态。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    color: ThemeBridge.colors["bg.void"]      // body 的底（.vw 自身无底）

    // ============================================================
    // 数据源：全部来自 C++ 注入的 `Page`（AssetPageModel），**本页无 mock**。
    // 迁移前这里是一整块写死的 ENTITIES/ENTITY_STATUS（mock.js），
    // 页面里 400 多行的渲染代码一行没动 —— 只有数据入口换成了桥。
    //
    // 三块真数据的取数在 ui/pages/assets/AssetVisualData.h（只读真库 + 文件系统
    // 存在性），与已删除的 Widgets 版共用过同一份：
    //   设定集 / 派生链  Page.deriveChain（visual_artifacts + 产物落盘）
    //   一致性对比      Page.consistency / compareKv / diffRows
    //                    （visual_states / character_status / 镜头图像素差异）
    //   关联时间线      Page.timeline（按章基线 + 绑定镜头 + 镜头参考图）
    // 像素差异与图像解码都在 worker 上做，UI 线程只渲染。
    //
    // 口径对齐（易踩）：
    //  * 卡片的 kind 用 C++ 的 key（person/location/item/faction），
    //    中文标签在 kindLabel；筛选值也是 key，别拿 label 去比。
    //  * 筛选用 Page.setKindFilter()（触发重查），选中用 Page.selectAsset()。
    //    别在 QML 侧改本地副本 —— C++ 侧才是真状态。
    // ============================================================

    // —— 页面状态（宿主可写）——
    property bool overview: false
    property int viewIndex: 0          // 0=总览 1=详情（QmlAssetsPage::ShowDetailPage 写）
    // 产物大图预览（点「查看大图」时置位）
    property var previewLayer: null
    property bool previewVisible: false

    readonly property var entities: Page.assets      // = 迁移前的 entities（设计稿 .asset-grid 每张卡）
    readonly property var kinds: Page.kinds          // [{key,label,count,on}]，带真计数
    readonly property var deriveChain: Page.deriveChain  // 真产物就绪状态（visual_artifacts + 落盘）
    readonly property var consistency: Page.consistency
    readonly property var timeline: Page.timeline

    // C++ 的 severity（none/same/minor/significant）→ Tag 的 tone 词表
    readonly property string severityTone: {
        switch (root.consistency.severity) {
        case "same":        return "ok"
        case "minor":       return "warn"
        case "significant": return "danger"
        default:            return "idle"
        }
    }
    // —— ① 导出整版：结果行 ——
    // 合成在 worker 上跑，结果由 C++ 填回 Page.exportState（空态也带全键）。
    // ⚠️ 这一行**恒占一行**：只在有 message 时出现的话，第一次导出会把右侧
    // 整块派生链顶下去一格。空态那半句由「就绪层」计数兜住，是真值不是占位。
    readonly property var exportState: Page.exportState
    readonly property real exportRowH: 8 + 16      // .col gap-2 + .tiny 行盒
    readonly property string exportMsg: {
        if (root.exportState.busy) { return root.exportState.message }
        if (root.exportState.message === "") {
            return "尚未导出整版 · 就绪层 " + root.readyLayerCount() + " 张"
        }
        if (root.exportState.ok) {
            return root.exportState.message + " · " + root.exportState.count + " 层 · "
                 + root.formatBytes(root.exportState.lastBytes)
        }
        // 取消也是 message 非空、ok=false —— 但它不是错误，不给红字
        return root.exportState.message
    }
    readonly property color exportColor: root.exportState.ok
        ? ThemeBridge.colors["status.ok"] : ThemeBridge.colors["text.muted"]
    function readyLayerCount() {
        var n = 0
        for (var i = 0; i < Page.layers.length; ++i) {
            if (Page.layers[i].ready) { n += 1 }
        }
        return n
    }
    function formatBytes(n) {
        if (n <= 0) { return "0 B" }
        if (n < 1024) { return n + " B" }
        if (n < 1024 * 1024) { return (n / 1024).toFixed(1) + " KB" }
        return (n / 1024 / 1024).toFixed(1) + " MB"
    }
    // 时间线副标题：有绑定镜头才提参考图，别常年挂着设计稿那句「全部条目可点击」
    readonly property string timelineMeta: root.timeline.shots.length > 0
        ? "外观基线 " + root.timeline.events.length + " 条 · 绑定镜头 "
          + root.timeline.shots.length + " 个 · 参考图 " + root.timeline.refImages.length + " 张"
        : "尚未登记外观基线；在 visual_states 登记后按章显示。"

    // Assets.jsx:144 的 Segmented 两项
    readonly property var viewOptions: [
        { value: "detail", label: "详情" },
        { value: "overview", label: "总览" }
    ]

    readonly property string kindFilter: Page.kindFilter
    readonly property string selectedId: Page.selectedAssetId

    function countOfKind(key) {
        for (var i = 0; i < Page.kinds.length; ++i) {
            if (Page.kinds[i].key === key) { return Page.kinds[i].count }
        }
        return 0
    }

    function setKind(key) { Page.setKindFilter(key) }
    function selectAsset(id) { Page.selectAsset(id) }

    // 派生链节点名是中文（「正脸」/「四视图」/…），而 C++ 的 startLayer 收的是
    // key（front/turnaround/base_body/wardrobe）。**别拿中文去比** —— 靠
    // deriveChain[i].name 反查 key，别在这里硬写一张中英对照表。
    function startLayerByName(name) {
        if (!root.deriveChain) { return }
        for (var i = 0; i < root.deriveChain.length; ++i) {
            if (root.deriveChain[i].name === name) {
                Page.startLayer(root.deriveChain[i].key)
                return
            }
        }
    }

    // 派生链层名：来自真实产物层（Page.deriveChain），空链显示「暂无产物」
    readonly property string deriveNames: {
        if (!root.deriveChain || root.deriveChain.length === 0) { return "暂无产物" }
        var names = []
        for (var i = 0; i < root.deriveChain.length; ++i) {
            names.push(root.deriveChain[i].name)
        }
        return names.join(" → ")
    }

    function entityById(id) {
        for (var i = 0; i < entities.length; ++i) {
            if (entities[i].id === id) { return entities[i] }
        }
        return entities.length > 0 ? entities[0] : null
    }

    // C++ 侧已把 kind 过滤进 assets 本身，这里不再二次过滤。
    readonly property var visibleEntities: entities

    // ⚠️ current **不能是 null**：详情区有十几处 `root.current.xxx` 绑定，
    // 返回 null 会刷一片 TypeError（空态时必现）。也不能用 `{}`：那样字段是
    // undefined，赋给 QString / int 属性又报 "Unable to assign"。
    // 正确做法是给一份**带类型零值**的空资产，取字段得空串/0，两边都不报错。
    // 「有没有资产」用 hasCurrent 判，不要用 truthiness 判 current。
    readonly property var emptyAsset: ({
        id: "", name: "", entityId: "", entityName: "",
        kind: "", kindLabel: "", role: "", art: 0,
        status: "", statusLabel: "", statusTone: "idle",
        degraded: false, sheetRelPath: "", baseDesc: "", canonStatus: "",
        runtime: ({ none: true })
    })
    readonly property bool hasCurrent: entities.length > 0
    readonly property var current: hasCurrent ? entityById(selectedId) : root.emptyAsset

    // —— 总览网格：repeat(auto-fit, minmax(210px, 1fr)) / gap 14（views.css:708-712）——
    readonly property real ovPadT: 20        // .vw padding 20px 24px 26px
    readonly property real ovPadX: 24
    readonly property real ovPadB: 26
    readonly property real ovGap: 16          // .vw gap: 16px
    readonly property real gridGap: 14        // .asset-grid gap: 14px
    readonly property real cardMinW: 210      // minmax(210px, 1fr)
    readonly property real cardH: 213         // thumb 150 + abody 9+21+4+18+11
    readonly property real ovInnerW: flick.width - ovPadX * 2
    readonly property int gridCols: Math.max(1, Math.floor((ovInnerW + gridGap) / (cardMinW + gridGap)))
    readonly property real cardW: (ovInnerW - gridGap * (gridCols - 1)) / gridCols
    readonly property int gridRowCount: Math.max(1, Math.ceil(visibleEntities.length / gridCols))
    readonly property real gridH: gridRowCount * cardH + (gridRowCount - 1) * gridGap
    // .vw-head 高度 = max(glyph 32 / 标题块 29+20 / chips 26 / seg 34 / btn 30) = 49
    readonly property real ovHeadH: 49
    readonly property real ovH: ovPadT + ovHeadH + ovGap + gridH + ovPadB

    // —— 详情三段：.vw 覆盖成 padding 10/18/18 + gap 0 ——
    readonly property real detPadT: 10
    readonly property real detPadX: 18
    readonly property real detPadB: 18
    readonly property real vsecPadT: 14      // .vsec padding: 14px 2px
    readonly property real vsecPadX: 2
    readonly property real vsecPadB: 14
    readonly property real vsecGap: 10       // .vsec gap: 10px
    readonly property real detInnerW: flick.width - detPadX * 2 - vsecPadX * 2

    // 设定集：grid minmax(240px,320px) minmax(280px,1fr) / gap 16（先各自长到上限）
    readonly property real sheetLeftW: Math.max(240, Math.min(320, detInnerW - 16 - 280))
    readonly property real sheetRightX: sheetLeftW + 16
    readonly property real sheetRightW: Math.max(0, detInnerW - sheetLeftW - 16)
    readonly property real sheetArtH: 220    // .art width 100% height 220
    readonly property real kvH: 98           // 4 行 × 20 + 3 × 6
    readonly property real btnSmH: 24        // .btn.sm height: 24px
    readonly property real deriveH: 92       // .derive padding 6 + node 80 + 6
    // 右列 = KV + gap12 + 按钮行 + gap12 + 导出结果行 + gap12 + 派生块
    // （标题 19+6 / 链 92 / 脚注 4+19）
    readonly property real sheetRightH: kvH + 12 + btnSmH + 12 + root.exportRowH + 12
                                       + (25 + deriveH + 23)
    readonly property real sheetBodyH: Math.max(sheetArtH + 8 + 19, sheetRightH)
    readonly property real sheetHeadH: 34    // 尾部有 Segmented（34），min-height 26 被顶开
    readonly property real sheetSecH: 2 + vsecPadT + sheetHeadH + vsecGap + sheetBodyH + vsecPadB + 1
    readonly property real ySheet: detPadT + 26          // 26 = 筛选胶囊行高

    // 一致性：舞台 16:10、max-width 640；右侧信息列 KV + 差异表 + 按钮
    readonly property real cmpSecH: vsecPadT + 26 + vsecGap + cmpView.implicitHeight + vsecPadB + 1
    readonly property real yCompare: ySheet + sheetSecH

    // 关联时间线：最后一段无下边框
    readonly property real tlSecH: vsecPadT + 26 + vsecGap + tlView.implicitHeight + vsecPadB
    readonly property real yTimeline: yCompare + cmpSecH

    // ④ 项目参考库 / ⑤ 依赖策略：AssetsRefLibrary / AssetsPolicyPanel。
    // 页面**不写内容高度常数** —— 写死高度是 PanelCard 头注第 2 条记的旧账。
    readonly property real refsSecH: vsecPadT + 26 + vsecGap + refView.implicitHeight + vsecPadB + 1
    readonly property real yRefs: yTimeline + tlSecH
    readonly property real policySecH: vsecPadT + 26 + vsecGap + policyView.implicitHeight + vsecPadB
    readonly property real yPolicy: yRefs + refsSecH
    readonly property real detH: yPolicy + policySecH + detPadB
    readonly property real contentH: Math.max(flick.height, overview ? ovH : detH)

    // —— 内容滚动：.vw 是 min-height 100% 的长列，超出视口的部分要能滚 ——
    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: root.contentH
        boundsBehavior: Flickable.StopAtBounds

        // ============ 总览 ============
        Item {
            id: overviewView
            x: 0
            y: 0
            width: flick.width
            height: root.ovH
            visible: root.overview

            Item {                          // .vw-head（gap 14 / align-items center）
                id: ovHead
                x: root.ovPadX
                y: root.ovPadT
                width: flick.width - root.ovPadX * 2
                height: root.ovHeadH

                Text {                      // <Icon name="masks" 20px> 的 accent 字符位
                    id: ovGlyph
                    x: 0
                    y: (ovHead.height - 32) / 2
                    text: "◈"
                    color: ThemeBridge.colors["accent.primary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 20
                }
                Text {                      // .vw-title f18 w800
                    id: ovTitle
                    x: 14
                    y: 0
                    text: "视觉资产 · 总览"
                    color: ThemeBridge.colors["text.primary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 18
                    font.weight: Font.Black
                }
                Text {                      // .vw-sub f12.5 text-muted
                    id: ovSub
                    x: 14
                    y: 29
                    text: "全部实体 " + root.visibleEntities.length + " 个 · 点击卡片进入详情"
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12   // .vw-sub f12.5px → 取整 12（全局只写整像素，见 AGENTS.md 圆角与字号刻度）
                }

                // 类型筛选胶囊（带 .cnt 数量徽标，见 Chip.qml 头注）
                Row {
                    id: ovChips
                    x: ovTitle.x + root.titleBlockW + 14
                    y: (ovHead.height - height) / 2
                    spacing: 6                  // .chips gap: 6px
                    Repeater {
                        model: root.kinds
                        delegate: Chip {
                            required property var modelData
                            // Page.kinds 的每项是 {key,label,count,on}，不再是裸字符串
                            text: modelData.label
                            count: modelData.count
                            active: modelData.on
                            onPicked: root.setKind(modelData.key)
                        }
                    }
                }

                Button {
                    id: ovRefresh
                    x: ovHead.width - width
                    y: (ovHead.height - height) / 2
                    variant: "ghost"
                    glyph: "↻"
                }
                Seg {
                    x: Math.max(ovChips.x + ovChips.width + 14, ovRefresh.x - 14 - width)
                    y: (ovHead.height - height) / 2
                    options: root.viewOptions
                    value: root.overview ? "overview" : "detail"
                    onPicked: function(v) { root.overview = (v === "overview") }
                }
            }

            // .asset-grid：显式 x/y（定位器会与 hover 的 y 抢几何）
            Repeater {
                model: root.visibleEntities
                delegate: Item {
                    id: cell
                    required property var modelData
                    required property int index
                    x: (cell.index % root.gridCols) * (root.cardW + root.gridGap)
                    y: root.ovPadT + root.ovHeadH + root.ovGap
                      + Math.floor(cell.index / root.gridCols) * (root.cardH + root.gridGap)
                    width: root.cardW
                    height: root.cardH
                    AssetsAssetCard {
                        x: 0
                        y: 0
                        width: cell.width
                        height: cell.height
                        entityId: cell.modelData.id
                        entityName: cell.modelData.name
                        entityKind: cell.modelData.kind
                        entityRole: cell.modelData.role
                        statusLabel: cell.modelData.statusLabel
                        statusTone: cell.modelData.statusTone
                        artSeed: cell.modelData.art
                        selected: cell.modelData.id === root.selectedId
                        onActivated: function(id) {
                            // 选中态的**真值在 C++ 侧**（Page.selectedAssetId），这里只发意图
                            root.selectAsset(id)
                            root.overview = false
                        }
                    }
                }
            }
        }

        // ============ 详情 ============
        Item {
            id: detailView
            x: 0
            y: 0
            width: flick.width
            height: root.detH
            visible: !root.overview && root.hasCurrent

            // 类型筛选胶囊：设计稿把它们放在外壳左栏（Shell.jsx:538-544），
            // 设计稿把工具条放在页内头部 —— 本页沿用页内
            // 位置，所以两种形态的顶部都有这一行 26px 的筛选条。
            Row {
                id: detChips
                x: root.detPadX + root.vsecPadX
                y: root.detPadT
                spacing: 6
                Repeater {
                    model: root.kinds
                    delegate: Chip {
                        required property var modelData
                        text: modelData.label
                        count: modelData.count
                        active: modelData.on
                        onPicked: root.setKind(modelData.key)
                    }
                }
            }

            // —— ① 设定集 ——
            Item {
                id: sheetSec
                x: root.detPadX
                y: root.ySheet
                width: flick.width - root.detPadX * 2
                height: root.sheetSecH

                // 尾部控件（Segmented + 3 个按钮）由 AssetsSecHead 内部的 Row 摆放，
                // 这里**不写 x/y** —— 定位器接管几何，绑 x 就是 binding loop。
                AssetsSecHead {
                    id: sheetHead
                    x: root.vsecPadX
                    y: 2 + root.vsecPadT
                    width: parent.width - root.vsecPadX * 2
                    height: root.sheetHeadH
                    glyph: "◈"
                    title: "设定集 · " + root.current.name
                    tagText: root.current.statusLabel
                    tagTone: root.current.statusTone
                    meta: root.current.kind + " · " + root.current.role + " · 一部一库 assets/"

                    Seg {
                        options: root.viewOptions
                        value: "detail"
                        onPicked: function(v) { root.overview = (v === "overview") }
                    }
                    Button {
                        text: "生成完整链"
                        glyph: "▶"
                        variant: "primary"
                        sm: true
                        // 真实管线：四层连跑，状态经 Page.runtime 回灌
                        onClicked: Page.startPipeline()
                    }
                    Button {
                        text: "导出整版"
                        glyph: "↓"
                        sm: true
                        // 合成 + 写盘全在 worker（AssetPageModel::exportSheet），
                        // 不传 target 时由 C++ 弹 QFileDialog 选路径 —— 对话框是
                        // 模态的必须留在 UI 线程，所以**不在 QML 侧自己选路径**。
                        // busy 期间禁用 + 转圈：按下没反应的那几秒必须看得见。
                        loading: Page.exportState.busy
                        enabled: !Page.exportState.busy
                        onClicked: Page.exportSheet()
                    }
                    Button {
                        variant: "ghost"
                        sm: true
                        glyph: "↻"
                        onClicked: Page.refreshAssets()
                    }
                }

                // 左列：基线图（100% × 220）+ 说明
                Rectangle {
                    id: sheetArt
                    x: root.vsecPadX
                    y: 2 + root.vsecPadT + root.sheetHeadH + root.vsecGap
                    width: root.sheetLeftW
                    height: root.sheetArtH
                    radius: root.rSm
                    color: "transparent"
                    clip: true
                    Art {
                        width: sheetArt.width
                        height: sheetArt.height
                        seed: root.current.art
                        zoom: true
                    }
                }
                Text {
                    x: root.vsecPadX
                    y: sheetArt.y + sheetArt.height + 8     // .col gap-2
                    text: "基线图 · 点击查看大图"
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }

                // 右列
                Item {
                    id: sheetRight
                    x: root.vsecPadX + root.sheetRightX
                    y: sheetArt.y
                    width: root.sheetRightW
                    height: root.sheetRightH

                    Kv {
                        id: sheetKv
                        x: 0
                        y: 0
                        width: sheetRight.width
                        // 键名是 Kv 的 {key, value}（共享件统一了旧件的 k / v）
                        // 「别名」一行在 mock 时代是查表写死的（aliasOf 只有两条），
                        // 真库里没有这一列 —— 改用 baseDesc，别再编假数据。
                        rows: root.current ? [
                            { key: "类别", value: root.current.kindLabel + " · " + root.current.role },
                            { key: "描述", value: root.current.baseDesc },
                            { key: "定稿", value: root.current.canonStatus },
                            { key: "状态", value: root.current.statusLabel }
                        ] : []
                    }

                    Row {                        // .row gap-2 wrap
                        id: sheetBtns
                        x: 0
                        y: sheetKv.implicitHeight + 12      // .col gap-3
                        spacing: 8
                        Button {
                            text: "导入图片"
                            glyph: "↑"
                            sm: true
                            // 与下面 ④ 参考库分区是**同一份库**（assets/refs/）：
                            // 选路径的模态对话框留在 UI 线程，解码 + 写回 refs.json
                            // 由 C++ 放 worker。
                            onClicked: Page.chooseReferenceFiles()
                        }
                        Button {
                            text: "绑定当前实体"
                            glyph: "↔"
                            sm: true
                            // ⚠️ C++ 的 bindReferenceToEntity 用的是它自己那份
                            // 参考库选中项，没选中时**静默返回** —— 那正是「点了
                            // 没反应」。所以先判一次，没选中就把该做什么说出来。
                            onClicked: refView.selectedRefId === ""
                                       ? Page.exportingUnsupported("绑定当前实体（先在下方参考库里选一张）")
                                       : Page.bindReferenceToEntity()
                        }
                        Button {
                            text: "查看大图"
                            glyph: "◎"
                            variant: "ghost"
                            sm: true
                            // 打开正脸层产物（取第一个就绪层）。全都没有就绪层时
                            // 明确提示，不装作打开了一张空图。
                            onClicked: {
                                var layers = Page.layers
                                for (var i = 0; i < layers.length; ++i) {
                                    if (layers[i].ready) {
                                        root.previewLayer = layers[i]
                                        root.previewVisible = true
                                        return
                                    }
                                }
                                Page.exportingUnsupported("查看大图（没有就绪的产物层）")
                            }
                        }
                    }

                    Text {                        // ① 导出结果行（恒占一行，见 exportRowH）
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 8
                        width: sheetRight.width
                        height: 16
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: root.exportMsg
                        color: root.exportColor
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                    }
                    Text {                        // .tiny.strong（color text-secondary）
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 12 + root.exportRowH
                        // 链名由真实产物层拼出（Page.deriveChain 每项带 name），
                        // 别再写死「正脸 → 四视图 → 基础身体 → 服装」。
                        text: "V0 派生链 · " + root.deriveNames
                        color: ThemeBridge.colors["text.secondary"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    AssetsDerive {
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 12 + root.exportRowH + 19 + 6
                        width: sheetRight.width
                        height: root.deriveH
                        items: root.deriveChain
                        // 点某一层 = 提交该层的形象层任务（真实管线，走
                        // AssetPageModel::startLayer → 资产运行器）。
                        // 之前 nodePicked 无人监听，节点点了没反应 ——
                        // 而 startLayer 一直在桥上没人调。
                        onNodePicked: function(name) { root.startLayerByName(name) }
                    }
                    Text {                        // .tiny.dim（margin-top 4）
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 12 + root.exportRowH + 25 + root.deriveH + 4
                        text: "派生起点：文生图 · 缺层时：生成这一层"
                        color: ThemeBridge.colors["text.muted"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                    }
                }

                Rectangle {                      // .vsec border-bottom
                    x: 0
                    y: parent.height - 1
                    width: parent.width
                    height: 1
                    color: ThemeBridge.colors["line.subtle"]
                }
            }

            // —— ② 一致性对比（真数据：Page.consistency / compareKv / diffRows）——
            Item {
                id: cmpSec
                x: root.detPadX
                y: root.yCompare
                width: flick.width - root.detPadX * 2
                height: root.cmpSecH

                AssetsSecHead {
                    x: root.vsecPadX
                    y: root.vsecPadT
                    width: parent.width - root.vsecPadX * 2
                    height: 26
                    glyph: "◫"
                    title: "一致性对比 · 按章外观基线"
                    // 结论徽标来自真实像素差异判定（none/same/minor/significant）
                    tagText: root.consistency.verdict
                    tagTone: root.severityTone
                    meta: root.consistency.diffPct !== ""
                          ? "像素差异 " + root.consistency.diffPct + "%"
                          : "拖动中线对比"
                }
                AssetsCompare {
                    id: cmpView
                    x: root.vsecPadX
                    y: root.vsecPadT + 26 + root.vsecGap
                    width: parent.width - root.vsecPadX * 2
                    height: implicitHeight
                    entityName: root.current.name
                    kvRows: Page.compareKv
                    diffRows: Page.diffRows
                    frames: root.consistency.frames
                    hasFrames: root.consistency.hasFrames
                    verdict: root.consistency.verdict
                    diffPct: root.consistency.diffPct
                }
                Rectangle {
                    x: 0
                    y: parent.height - 1
                    width: parent.width
                    height: 1
                    color: ThemeBridge.colors["line.subtle"]
                }
            }

            // —— ③ 关联时间线（真数据：Page.timeline）——
            Item {
                id: tlSec
                x: root.detPadX
                y: root.yTimeline
                width: flick.width - root.detPadX * 2
                height: root.tlSecH

                AssetsSecHead {
                    x: root.vsecPadX
                    y: root.vsecPadT
                    width: parent.width - root.vsecPadX * 2
                    height: 26
                    glyph: "◷"
                    title: "关联时间线"
                    meta: root.timelineMeta
                }
                AssetsTimeline {
                    id: tlView
                    x: root.vsecPadX
                    y: root.vsecPadT + 26 + root.vsecGap
                    width: parent.width - root.vsecPadX * 2
                    height: implicitHeight
                    events: root.timeline.events
                    shots: root.timeline.shots
                    refImages: root.timeline.refImages
                    chapterCount: root.timeline.chapterCount
                    hasData: root.timeline.hasData
                }
            }

            // —— ④ 项目参考库（真数据：Page.refImages，AssetsRefLibrary）——
            Item {
                id: refsSec
                x: root.detPadX
                y: root.yRefs
                width: flick.width - root.detPadX * 2
                height: root.refsSecH

                AssetsSecHead {
                    x: root.vsecPadX
                    y: root.vsecPadT
                    width: parent.width - root.vsecPadX * 2
                    height: 26
                    glyph: "▦"
                    title: "项目参考图 · assets/refs/"
                    // 数量由面板自己显示（那一行就在标题条下面），这里只留
                    // 导入口径：解码在 worker，EXIF 方向同时校正。
                    meta: "导入在 worker 解码并写回 refs.json；EXIF 方向同时校正"
                }
                AssetsRefLibrary {
                    id: refView
                    x: root.vsecPadX
                    y: root.vsecPadT + 26 + root.vsecGap
                    width: parent.width - root.vsecPadX * 2
                    height: implicitHeight
                }
                Rectangle {                      // .vsec border-bottom
                    x: 0
                    y: parent.height - 1
                    width: parent.width
                    height: 1
                    color: ThemeBridge.colors["line.subtle"]
                }
            }

            // —— ⑤ 依赖等待与降级策略（真数据：Page.policy，AssetsPolicyPanel）——
            // 这两个开关进的是真实管线（AssetPolicy），不是页面上的装饰。
            Item {
                id: policySec
                x: root.detPadX
                y: root.yPolicy
                width: flick.width - root.detPadX * 2
                height: root.policySecH

                AssetsSecHead {
                    x: root.vsecPadX
                    y: root.vsecPadT
                    width: parent.width - root.vsecPadX * 2
                    height: 26
                    glyph: "◐"
                    title: "依赖等待与降级策略"
                    meta: Page.policy.strict ? "严格模式：缺依赖只挂起" : "已开启超时降级"
                }
                AssetsPolicyPanel {
                    id: policyView
                    x: root.vsecPadX
                    y: root.vsecPadT + 26 + root.vsecGap
                    width: parent.width - root.vsecPadX * 2
                    height: implicitHeight
                }
            }
        }
    }

    // .vw-head 标题块的宽度（标题 / 副标题取宽的那个）
    readonly property real titleBlockW: Math.max(ovTitle.implicitWidth, ovSub.implicitWidth)

    // 滚动条指示（base.css:64-78：10px 槽 + 3px 透明边 + bg-elevated 圆头）
    Rectangle {
        id: scrollbar
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 10
        visible: flick.contentHeight > flick.height
        color: "transparent"
        Rectangle {
            x: 3
            y: flick.contentY / flick.contentHeight * scrollbar.height
            width: 4
            height: Math.max(24, flick.height / flick.contentHeight * scrollbar.height)
            radius: 2
            color: ThemeBridge.colors["bg.elevated"]
        }
    }

    // —— 产物大图预览 ——
    // 只在 previewVisible 时存在：Esc / 点背景关闭。图片是**真实产物**
    // （Page.layers[].absPath，仅就绪层非空），不是占位图。
    Rectangle {
        anchors.fill: parent
        visible: root.previewVisible && root.previewLayer !== null
        color: Qt.alpha(ThemeBridge.colors["bg.void"], 0.86)
        MouseArea {
            anchors.fill: parent
            onClicked: root.previewVisible = false
        }
        Image {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 900)
            height: Math.min(parent.height - 80, 640)
            source: root.previewLayer !== null ? "file:///" + root.previewLayer.absPath : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.width: 1280
            sourceSize.height: 960
        }
        Text {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 18
            text: root.previewLayer !== null ? root.previewLayer.title : ""
            color: ThemeBridge.colors["text.primary"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 13
        }
        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 18
            text: "点击任意处关闭"
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 11
        }
    }

    // —— 页级空态 ——
    // 没打开书库、或当前筛选下没有资产时，上面两个分区（总览/详情）
    // 都是 visible:false —— 不铺这一层的话整页只剩一块底色，看着像崩了。
    Empty {
        anchors.fill: parent
        visible: !root.hasCurrent
        title: Page.hasBook ? "没有可显示的资产" : "未打开书库"
        text: Page.openError
              ? Page.openError
              : (Page.hasBook
                 ? "当前筛选下没有资产。在左栏换一个实体，或清掉类型筛选。"
                 : "打开一个项目并选中小说后，这里显示该书的视觉资产。")
    }
}
