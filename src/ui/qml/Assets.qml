pragma ComponentBehavior: Bound
// src/ui/qml/Assets.qml —— QML 版资产页（Widgets 版见 src/ui/pages/assets/AssetWorkspace.cpp）
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
// 数据：设计稿的 mock 数据（webui/src/data/mock.js 的 ENTITIES / ENTITY_STATUS /
// DERIVE_CHAIN）。本页不接 C++ 模型（本轮只迁视觉层），所以这些实体是页面私有的
// 静态数据；宿主接真数据时替换 entities 一处即可。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    color: ThemeBridge.colors["bg.void"]      // body 的底（.vw 自身无底）

    // —— 页面状态（宿主可写）——
    property string kindFilter: "全部"
    property string selectedId: "e1"
    property bool overview: false

    // —— 设计稿数据：mock.js ENTITIES + ENTITY_STATUS ——
    readonly property var entities: [
        { id: "e1", name: "林晚", kind: "人物", role: "主角 · 守灯人", art: 5,
          statusLabel: "已就绪", statusTone: "ok" },
        { id: "e2", name: "陈拾", kind: "人物", role: "邮差", art: 6,
          statusLabel: "已就绪", statusTone: "ok" },
        { id: "e3", name: "老周", kind: "人物", role: "钟表匠", art: 7,
          statusLabel: "参考就绪", statusTone: "info" },
        { id: "e4", name: "回声灯", kind: "物品", role: "核心道具", art: 8,
          statusLabel: "已就绪", statusTone: "ok" },
        { id: "e5", name: "纸鸢", kind: "物品", role: "信物", art: 9,
          statusLabel: "待生成", statusTone: "idle" },
        { id: "e6", name: "灯下街", kind: "地点", role: "主场景", art: 10,
          statusLabel: "已就绪", statusTone: "ok" },
        { id: "e7", name: "钟表铺", kind: "地点", role: "支线场景", art: 11,
          statusLabel: "失败", statusTone: "danger" },
        { id: "e8", name: "守灯人公会", kind: "势力", role: "组织", art: 12,
          statusLabel: "待生成", statusTone: "idle" }
    ]
    // Shell.jsx:522 的 5 个 kind
    readonly property var kinds: ["全部", "人物", "地点", "物品", "势力"]
    // mock.js DERIVE_CHAIN
    readonly property var deriveChain: [
        { name: "正脸", state: "done" },
        { name: "四视图", state: "done" },
        { name: "基础身体", state: "run" },
        { name: "服装", state: "todo" }
    ]
    // Assets.jsx:144 的 Segmented 两项
    readonly property var viewOptions: [
        { value: "detail", label: "详情" },
        { value: "overview", label: "总览" }
    ]

    function countOf(kind) {
        var n = 0
        for (var i = 0; i < entities.length; ++i) {
            if (kind === "全部" || entities[i].kind === kind) {
                n += 1
            }
        }
        return n
    }

    function entityById(id) {
        for (var i = 0; i < entities.length; ++i) {
            if (entities[i].id === id) {
                return entities[i]
            }
        }
        return entities[0]
    }

    // 设定集 KV 的「别名」列（Assets.jsx:208 的三元表达式）
    function aliasOf(name) {
        if (name === "林晚") {
            return "晚晚 / 小灯"
        }
        if (name === "陈拾") {
            return "邮差阿拾"
        }
        return "—"
    }

    // 绑定里读了 kindFilter，所以它会随筛选重算
    readonly property var visibleEntities: {
        var out = []
        for (var i = 0; i < entities.length; ++i) {
            if (kindFilter === "全部" || entities[i].kind === kindFilter) {
                out.push(entities[i])
            }
        }
        return out
    }
    readonly property var current: entityById(selectedId)

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
    // 右列 = KV + gap12 + 按钮行 + gap12 + 派生块（标题 19+6 / 链 92 / 脚注 4+19）
    readonly property real sheetRightH: kvH + 12 + btnSmH + 12 + (25 + deriveH + 23)
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
    readonly property real detH: yTimeline + tlSecH + detPadB
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
                    font.pixelSize: 12   // .vw-sub f12.5px → 取整 12（同 AssetWorkspace.cpp:308 的落地值）
                }

                // 类型筛选胶囊（带 .cnt 数量徽标，见 AssetsChip 头注释）
                Row {
                    id: ovChips
                    x: ovTitle.x + root.titleBlockW + 14
                    y: (ovHead.height - height) / 2
                    spacing: 6                  // .chips gap: 6px
                    Repeater {
                        model: root.kinds
                        delegate: AssetsChip {
                            required property var modelData
                            text: modelData
                            count: root.countOf(modelData)
                            active: root.kindFilter === modelData
                            onPicked: root.kindFilter = modelData
                        }
                    }
                }

                AssetsBtn {
                    id: ovRefresh
                    x: ovHead.width - width
                    y: (ovHead.height - height) / 2
                    variant: "ghost"
                    glyph: "↻"
                }
                AssetsSeg {
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
                            root.selectedId = id
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
            visible: !root.overview

            // 类型筛选胶囊：设计稿把它们放在外壳左栏（Shell.jsx:538-544），
            // Widgets 版放在页内头部（AssetWorkspace.cpp:314-332）——本页沿用页内
            // 位置，所以两种形态的顶部都有这一行 26px 的筛选条。
            Row {
                id: detChips
                x: root.detPadX + root.vsecPadX
                y: root.detPadT
                spacing: 6
                Repeater {
                    model: root.kinds
                    delegate: AssetsChip {
                        required property var modelData
                        text: modelData
                        count: root.countOf(modelData)
                        active: root.kindFilter === modelData
                        onPicked: root.kindFilter = modelData
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

                    AssetsSeg {
                        options: root.viewOptions
                        value: "detail"
                        onPicked: function(v) { root.overview = (v === "overview") }
                    }
                    AssetsBtn {
                        text: "生成完整链"
                        glyph: "▶"
                        variant: "primary"
                        sm: true
                    }
                    AssetsBtn {
                        text: "导出整版"
                        glyph: "↓"
                        sm: true
                    }
                    AssetsBtn {
                        variant: "ghost"
                        sm: true
                        glyph: "↻"
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
                    AssetsArt {
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

                    AssetsKv {
                        id: sheetKv
                        x: 0
                        y: 0
                        width: sheetRight.width
                        rows: [
                            { k: "类别", v: root.current.kind + " · " + root.current.role },
                            { k: "别名", v: root.aliasOf(root.current.name) },
                            { k: "出处", v: "第 " + (1 + (root.current.art % 8)) + " 章（见下方时间线）" },
                            { k: "降级策略", v: "允许超期降级（B 级）" }
                        ]
                    }

                    Row {                        // .row gap-2 wrap
                        id: sheetBtns
                        x: 0
                        y: sheetKv.implicitHeight + 12      // .col gap-3
                        spacing: 8
                        AssetsBtn {
                            text: "导入图片"
                            glyph: "↑"
                            sm: true
                        }
                        AssetsBtn {
                            text: "绑定当前实体"
                            glyph: "↔"
                            sm: true
                        }
                        AssetsBtn {
                            text: "查看大图"
                            glyph: "◎"
                            variant: "ghost"
                            sm: true
                        }
                    }

                    Text {                        // .tiny.strong（color text-secondary）
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 12
                        text: "V0 派生链 · 正脸 → 四视图 → 基础身体 → 服装"
                        color: ThemeBridge.colors["text.secondary"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    AssetsDerive {
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 12 + 19 + 6
                        width: sheetRight.width
                        height: root.deriveH
                        items: root.deriveChain
                    }
                    Text {                        // .tiny.dim（margin-top 4）
                        x: 0
                        y: sheetBtns.y + sheetBtns.height + 12 + 25 + root.deriveH + 4
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

            // —— ② 一致性对比 ——
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
                    tagText: "结论：轻微差异"
                    tagTone: "ok"
                    meta: "拖动中线对比"
                }
                AssetsCompare {
                    id: cmpView
                    x: root.vsecPadX
                    y: root.vsecPadT + 26 + root.vsecGap
                    width: parent.width - root.vsecPadX * 2
                    height: implicitHeight
                    entityName: root.current.name
                }
                Rectangle {
                    x: 0
                    y: parent.height - 1
                    width: parent.width
                    height: 1
                    color: ThemeBridge.colors["line.subtle"]
                }
            }

            // —— ③ 关联时间线（最后一段无下边框）——
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
                    meta: "外观基线 / 出处 / 绑定镜头 / 参考图 · 全部条目可点击"
                }
                AssetsTimeline {
                    id: tlView
                    x: root.vsecPadX
                    y: root.vsecPadT + 26 + root.vsecGap
                    width: parent.width - root.vsecPadX * 2
                    height: implicitHeight
                    artSeed: root.current.art
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
}
