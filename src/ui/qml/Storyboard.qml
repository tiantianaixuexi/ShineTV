// src/ui/qml/Storyboard.qml —— 分镜工作区（QML 版）
//
// 视觉真值 = webui 设计稿，逐条对着 CSS 的 px 值抄：
//   Storyboard.jsx            结构与内容（.vw / .vw-head / .shots-wrap / .timeline / 弹层）
//   views.css:133-152         .vw / .vw-head / .vw-title / .vw-sub
//   views.css:293-326         .dlist / .drow
//   views.css:670-699         .chips / .chip
//   views.css:945-1003        .shots-wrap / .timeline / .tl-card
//   ui.css:175-204            .card / .card-h / .card-title / .card-b
//   ui.css:814-893            .stageflow / .snode / .slink
//   ui.css:120-145            .tag / .tag.sm
//   ui.css:430-470            .prog(.thin) / .spin(.sm)
//   ui.css:1015-1028          .kv
//   ui.css:932-976            .scrim / .modal-h|b|f
//   ui.css:278-329            .field / .input / .textarea / .select
//   base.css:130-168          @keyframes（spin / pop-in / fade-in / pulse-dot）
//
// ⚠️ 定位纪律（docs/90-reference/ui-design-parity-gaps.md 第五之二节 ⑦）：
//  1. 根是 Ctl（Rectangle），尺寸由宿主 QQuickWidget 给，不写 width/height/visible。
//  2. 页面层不用 Flow/Row/Grid 当布局容器：定位器接管子项几何，会和 hover 的
//     translateY(-2px) 抢 y → binding loop。区块一律显式 x/y。
//  3. 每张 Card 内部只放**一个**填满的 Item（Card 的 body 是 Column，会接管直接子项的 y）。
//  4. hover 抬升挂在外层 Item 上，不挂在 anchors.fill 的 Rectangle 上。
//  5. 不用 Flow/Row 的地方只有两处，都是设计稿本身就是 flex 且子项**不做位移**的：
//     StageFlow 的行（.stageflow 是 display:flex）与连续性 chip 的换行（.chips 是 flex-wrap）。
//
// ⚠️ 色值：全部走 ThemeBridge（colors / toneBg / toneEdge），本文件不出现任何色值字面量。
//  设计稿的 --accent-glow(30%) / --accent-dim(14%) / tone 40%·10% 混色在桥上没有对应档，
//  统一取 toneEdge(35%) / toneBg(12%) 并在用到的地方注明。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    color: "transparent"          // 外壳（src/ui/pages/shell）负责画底，本页只画内容区

    // ═══════════════════ 数据（设计稿 mock：mock.js CH3_DEMO / C_CHECKS / V_STAGES）═══════════════════
    //
    // 默认取第 3 章的 10 个镜头。按 buildShots() 的全局计数器（ch1 4 个 + ch2 3 个）
    // 第 3 章的镜号是 S08–S17；默认选中 S12 = DEMO_SHOT「灯芯分裂出双生火苗」（running，
    // 头部 Tag 走 busy + 脉冲点）。宿主可整体替换 shots / cChecks。
    property var shots: [
        { id: 8,  code: "S08", chapter: 3, scene: "场景 1",  action: "雨夜，镜头推近亮起的灯笼",   mood: "静谧", dur: "3.5s", status: "done" },
        { id: 9,  code: "S09", chapter: 3, scene: "场景 2",  action: "少女指尖抚过灯罩裂纹",       mood: "温柔", dur: "2.8s", status: "done" },
        { id: 10, code: "S10", chapter: 3, scene: "场景 3",  action: "长街全景，灯火次第亮起",     mood: "怀旧", dur: "5.0s", status: "done" },
        { id: 11, code: "S11", chapter: 3, scene: "场景 4",  action: "老人回头，眼中映着灯影",     mood: "怅然", dur: "2.2s", status: "running" },
        { id: 12, code: "S12", chapter: 3, scene: "场景 5",  action: "灯芯分裂出双生火苗",         mood: "紧张", dur: "4.1s", status: "running" },
        { id: 13, code: "S13", chapter: 3, scene: "场景 6",  action: "风起，纸鸢脱手飞向夜空",     mood: "惊喜", dur: "3.0s", status: "todo" },
        { id: 14, code: "S14", chapter: 3, scene: "场景 7",  action: "俯拍：全城灯火连成星图",     mood: "恢弘", dur: "6.0s", status: "todo" },
        { id: 15, code: "S15", chapter: 3, scene: "场景 8",  action: "少女奔跑，裙角掠过水洼",     mood: "急切", dur: "2.6s", status: "todo" },
        { id: 16, code: "S16", chapter: 3, scene: "场景 9",  action: "特写：手机屏幕亮起无声来电", mood: "诡异", dur: "1.8s", status: "todo" },
        { id: 17, code: "S17", chapter: 3, scene: "场景 10", action: "灯影里两张重叠的面孔",       mood: "悬疑", dur: "3.4s", status: "todo" }
    ]
    property var cChecks: [
        { code: "C01", name: "空间连续" }, { code: "C02", name: "道具连续" },
        { code: "C03", name: "服饰连续" }, { code: "C04", name: "光线方向" },
        { code: "C05", name: "时间流" },   { code: "C06", name: "人物朝向" },
        { code: "C07", name: "伤妆痕迹" }, { code: "C08", name: "天气一致" },
        { code: "C09", name: "镜头轴线" }, { code: "C10", name: "场景出入" },
        { code: "C11", name: "画幅一致" }, { code: "C12", name: "色彩基调" }
    ]
    // Beat 时间轴默认内容（Storyboard.jsx:115-119）
    property string beatsJson: "[\n  { \"t\": 0.0, \"act\": \"推近\", \"camera\": \"C4 · 缓推\" },\n  { \"t\": 1.2, \"act\": \"火苗点头\", \"vfx\": \"灯芯裂纹微光\" },\n  { \"t\": 2.4, \"act\": \"切近景\", \"note\": \"眼中映着灯影\" }\n]"

    // ═══════════════════ 状态 ═══════════════════
    property int selectedIndex: 4                 // 默认 S12（DEMO_SHOT）
    property int vRun: 3                          // 已跑到的阶段数（设计稿初始 useState(3)）
    property bool vActive: false
    property string receipt: ""                   // 落库/重生成回执（设计稿里是 toast）
    property bool editOpen: false
    property var order: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]   // 时间线槽位 → shots 下标
    property int pressSlot: -1

    readonly property var shot: shots[selectedIndex]

    // ═══════════════════ 布局刻度 ═══════════════════
    // views.css:133-139 .vw { padding: 20px 24px 26px; display:flex; column; gap:16px }
    readonly property int padT: 20
    readonly property int padX: 24
    readonly property int padB: 26
    readonly property int gap: ThemeBridge.spaces["4"]          // 16（--sp-4）
    readonly property real lh: 1.6                              // base.css:19 body line-height
    readonly property real bodyW: Math.max(0, flick.width - padX * 2)

    // .vw-head：标题 18×1.6 + 副标题 12×1.6（设计稿两行之间没有 gap）
    readonly property real headH: 18 * root.lh + 12 * root.lh

    // 阶段卡：状态行 12×1.6 + gap-2(8) + .stageflow(4+30+4)
    readonly property real statusLH: 12 * root.lh
    readonly property real stageFlowH: 30 + 4 * 2
    readonly property real stageBodyH: statusLH + ThemeBridge.spaces["2"] + stageFlowH
    readonly property real stageCardH: stageBodyH + 32          // 32 = Card.qml body 上下边距

    // .shots-wrap { grid-template-columns: minmax(0,1.2fr) minmax(0,1fr); gap:16; align-items:start }
    readonly property real wrapGap: ThemeBridge.spaces["4"]
    readonly property real contW: (bodyW - wrapGap) / 2.2
    readonly property real detailW: (bodyW - wrapGap) * 1.2 / 2.2

    // 镜头详情卡：KV(7 行 · 行距 6) + 12 + Beat 标题行 + 6 + 文本域 + 8 + sm 按钮 24
    readonly property real kvLH: 12 * root.lh
    readonly property real kvH: 7 * kvLH + 6 * 6
    readonly property real beatsBoxH: 8 * 13 * 1.8 + 8 * 2 + 2   // rows 8 · lh 1.8 · p8 · 1px 边
    readonly property real detailBodyH: kvH + 12 + 12 * root.lh + 6 + beatsBoxH + 8 + 24
    readonly property real detailCardH: detailBodyH + 32 + 46  // +46 = .card-h 全块（含 -16 偏移）

    // 连续性卡：chips + 8 + 说明行 + 10 + dlist(4 行 · p8 2)
    readonly property real chipH: 24
    readonly property real chipsGap: 6
    readonly property real chipTextGap: 6
    readonly property real chipIconW: 11
    readonly property real chipPadX: 11
    readonly property real noteLH: 12 * root.lh
    readonly property real drowH: 8 * 2 + kvLH
    readonly property real dlistH: 4 * drowH
    readonly property real contChipsH: chipsHeight(contW - 32)
    readonly property real contBodyH: contChipsH + 8 + noteLH + 10 + dlistH
    readonly property real contCardH: contBodyH + 32 + 46

    // .timeline { padding: 8px 2px 12px } + .tl-card 宽 128
    readonly property int tlCardW: 128
    readonly property int tlPitch: tlCardW + 10
    // 行高是 1.6 倍字号，算出来是小数 → 属性必须是 real（int 会截断并报运行期警告）
    readonly property real tlCardH: 72 + 7 + 11 * root.lh + 2 + 11 * root.lh + 6 + 10 * root.lh + 9
    readonly property real tlBodyH: 8 + tlCardH + 12
    readonly property real timelineCardH: tlBodyH + 32 + 46
    readonly property real tlContentW: Math.max(0, shots.length * tlPitch - 6)   // 2 + n*128 + (n-1)*10 + 2

    // 纵坐标链（.vw 是 flex column，间距 16）
    readonly property real stageY: headH + gap
    readonly property real wrapY: stageY + stageCardH + gap
    readonly property real wrapH: Math.max(detailCardH, contCardH)              // align-items: start
    readonly property real timelineY: wrapY + wrapH + gap
    readonly property real contentH: timelineY + timelineCardH + padB

    // ═══════════════════ 小工具 ═══════════════════
    FontMetrics {
        id: chipFm
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
    FontMetrics {
        id: kvFm
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }
    readonly property var kvKeys: ["动作", "空间", "表演", "机位", "光线", "时长", "情绪"]
    readonly property real kvKeyW: {
        let w = 0
        for (let i = 0; i < root.kvKeys.length; ++i) {
            w = Math.max(w, kvFm.advanceWidth(root.kvKeys[i]))
        }
        return w
    }

    // .chip 宽 = 11(图) + 6(内 gap) + 文字 + 11×2(padding)
    function chipCellWidth(entry) {
        return root.chipPadX * 2 + root.chipIconW + root.chipTextGap
               + chipFm.advanceWidth(entry.code + " " + entry.name)
    }
    // Flow 只负责换行、不给 implicitHeight，高度按同一套宽度做一次贪心断行
    function chipsHeight(avail) {
        let rows = 1
        let x = 0
        for (let i = 0; i < root.cChecks.length; ++i) {
            const cell = root.chipCellWidth(root.cChecks[i]) + root.chipsGap
            if (x > 0 && x + cell > avail) {
                rows += 1
                x = cell
            } else {
                x += cell
            }
        }
        return rows * root.chipH + (rows - 1) * root.chipsGap
    }
    function shotStatusLabel(s) {
        return s === "running" ? "生成中" : s === "done" ? "完成" : s === "failed" ? "失败" : "待出图"
    }
    function shotStatusTone(s) {
        return s === "running" ? "busy" : s === "done" ? "ok" : s === "failed" ? "danger" : "idle"
    }
    // stateOf = (_, i) => (i < vRun ? 'done' : i === vRun && vActive ? 'run' : 'todo')
    function stageState(i) {
        return i < root.vRun ? "done" : (i === root.vRun && root.vActive ? "run" : "todo")
    }
    function secs(dur) { return parseFloat(dur) }

    // 阶段推进（Storyboard.jsx:26-41：650ms 一档，vRun 到 8 收尾）
    function runV() {
        if (root.vActive || root.vRun >= 8) {
            return
        }
        vActive = true
        tick.restart()
    }
    function stepV() {
        if (root.vRun >= 8) {
            vActive = false
            return
        }
        vRun += 1
        if (root.vRun >= 8) {
            vActive = false
            flash("V8 连续性校验通过 · C1–C12 未发现不一致")
        } else {
            tick.restart()
        }
    }
    function flash(msg) {
        receipt = msg
        receiptTimer.restart()
    }
    Timer { id: tick; interval: 650; onTriggered: root.stepV() }
    Timer { id: receiptTimer; interval: 2600; onTriggered: root.receipt = "" }

    // 时间线拖拽重排（Storyboard.jsx:43-53）
    function slotAt(contentX) {
        const raw = Math.round((contentX - 2 - root.tlCardW / 2) / root.tlPitch)
        return Math.max(0, Math.min(root.shots.length - 1, raw))
    }
    function moveSlot(from, to) {
        if (from < 0 || to < 0 || from === to) {
            return
        }
        const arr = root.order.slice()
        arr.splice(to, 0, arr.splice(from, 1)[0])
        order = arr                       // var 属性必须**重新赋值**才会触发重算
        flash("镜头顺序已重排 · Prompt 版本 +1")
    }
    function applyEdit(action, mood, dur, note) {
        const next = root.shots.slice()
        next[root.selectedIndex] = {
            id: root.shot.id, code: root.shot.code, chapter: root.shot.chapter,
            scene: root.shot.scene, action: action, mood: mood, dur: dur,
            status: root.shot.status, note: note
        }
        shots = next
        flash(root.shot.code + " 已保存 · Prompt 版本 +1（演示）")
    }

    // ═══════════════════ 滚动容器 ═══════════════════
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: root.padT + root.contentH
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true

        Item {
            id: body
            x: root.padX
            y: root.padT
            width: root.bodyW
            height: root.contentH

            // ─────────── views.css:140-152 .vw-head ───────────
            Item {
                id: head
                x: 0
                y: 0
                width: root.bodyW
                height: root.headH

                Text {
                    id: headIcon
                    x: 0
                    y: (parent.height - height) / 2
                    width: 20
                    height: 20
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: "▥"                                    // Icon name="clapper"
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 16
                    color: ThemeBridge.colors["accent.primary"]
                }
                Text {
                    id: headTitle
                    x: headIcon.width + 14                          // .vw-head gap 14
                    y: 0
                    width: parent.width - x - 14 - actionRow.width
                    text: root.shot.code + " · " + root.shot.action
                    elide: Text.ElideRight
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 18                             // .vw-title f18 w800
                    font.weight: Font.ExtraBold
                    lineHeight: root.lh
                    lineHeightMode: Text.FixedHeight
                    color: ThemeBridge.colors["text.primary"]
                }
                Text {
                    id: headSub
                    x: headTitle.x
                    y: 18 * root.lh
                    width: headTitle.width
                    text: "第 " + root.shot.chapter + " 章 · " + root.shot.scene
                          + " · 已提交场景 4 个 · Scene → Sequence → Shot"
                    elide: Text.ElideRight
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12                             // .vw-sub 12.5 → 12
                    lineHeight: root.lh
                    lineHeightMode: Text.FixedHeight
                    color: ThemeBridge.colors["text.muted"]
                }
                Row {
                    id: actionRow
                    x: parent.width - width
                    y: (parent.height - height) / 2
                    height: 30
                    spacing: 14                                   // .vw-head gap 14
                    Item {
                        width: headStatusTag.implicitWidth
                        height: 30
                        StoryboardTag {
                            id: headStatusTag
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.shotStatusLabel(root.shot.status)
                            tone: root.shotStatusTone(root.shot.status)
                            dot: root.shot.status === "running"   // UI.jsx:36
                        }
                    }
                    StoryboardButton {
                        text: "V9/V10 落库"
                        variant: "secondary"
                        onClicked: root.flash("叙事分镜已落库 shots / prompt_artifacts")
                    }
                    StoryboardButton {
                        text: "V10 重生成"
                        variant: "secondary"
                        onClicked: {
                            root.vRun = 0
                            root.vActive = false
                            root.flash("按当前输入状态重新生成 · Prompt 版本递增")
                        }
                    }
                    StoryboardButton {
                        text: "运行 V1–V8"
                        variant: "primary"
                        enabled: !root.vActive                      // vActive 时 disabled
                        onClicked: root.runV()
                    }
                }
            }

            // ─────────── 阶段卡（Storyboard.jsx:75-88，无 .card-h，正文从 y=0 起）───────────
            Card {
                id: stageCard
                x: 0
                y: root.stageY
                width: root.bodyW
                height: root.stageCardH
                // 设计稿：className={vActive ? 'glow' : ''} —— 只有运行期才有 hover 反馈
                // （.card.glow:hover 的 accent-glow 边 / shadow-accent 冻结的 Card 给不出，
                //   这里退化为冻结层的 line.strong + shadow-1）
                hoverable: root.vActive

                Item {
                    width: parent.width
                    height: parent.height

                    StoryboardSpin {
                        id: stageSpin
                        x: 0
                        y: (root.statusLH - height) / 2
                        visible: root.receipt === "" && root.vActive
                    }
                    Text {
                        id: stageCheck
                        x: 0
                        y: (root.statusLH - height) / 2
                        width: 16
                        horizontalAlignment: Text.AlignHCenter
                        text: "✓"                                    // Icon name="check"
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        color: ThemeBridge.colors["status.ok"]
                        visible: root.receipt === "" && !root.vActive && root.vRun >= 8
                    }
                    Text {
                        x: (stageSpin.visible || stageCheck.visible) ? 16 + 8 : 0
                        y: 0
                        width: parent.width - x - 80
                        text: root.receipt !== ""
                              ? root.receipt
                              : (root.vActive ? "V" + (root.vRun + 1) + " 运行中…"
                              : (root.vRun >= 8 ? "V1–V8 完成 · C1–C12 通过"
                                                : "V1–V7 按章节运行，V8 执行连续性校验"))
                        elide: Text.ElideRight
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12                        // .small 12.5 → 12
                        lineHeight: root.lh
                        lineHeightMode: Text.FixedHeight
                        color: root.receipt !== "" ? ThemeBridge.colors["text.secondary"]
                             : root.vActive        ? ThemeBridge.colors["accent.primary"]
                             : root.vRun >= 8      ? ThemeBridge.colors["status.ok"]
                                                    : ThemeBridge.colors["text.muted"]
                    }
                    StoryboardTag {
                        id: vTag
                        x: parent.width - implicitWidth
                        y: (root.statusLH - height) / 2
                        text: root.vRun >= 8 ? "已校验" : "V9/V10 未运行"
                        tone: root.vRun >= 8 ? "ok" : "idle"
                        sm: true
                    }

                    Flickable {
                        id: stageFlick
                        x: 0
                        y: root.statusLH + ThemeBridge.spaces["2"]
                        width: parent.width
                        height: root.stageFlowH
                        contentWidth: stageRow.width + 4
                        contentHeight: root.stageFlowH
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick
                        // .stageflow { display:flex; align-items:center; padding:4px 2px }
                        // 行内子项（.slink / .snode）**不做位移**，所以这里用 Row 是安全的
                        Row {
                            id: stageRow
                            x: 2
                            y: 4
                            height: 30
                            spacing: 0
                            StoryboardStage { code: "V1"; stageName: "场景输入"; stageState: root.stageState(0); hasLink: false }
                            StoryboardStage { code: "V2"; stageName: "节拍拆解"; stageState: root.stageState(1); hasLink: true }
                            StoryboardStage { code: "V3"; stageName: "镜头切分"; stageState: root.stageState(2); hasLink: true }
                            StoryboardStage { code: "V4"; stageName: "空间布局"; stageState: root.stageState(3); hasLink: true }
                            StoryboardStage { code: "V5"; stageName: "表演标注"; stageState: root.stageState(4); hasLink: true }
                            StoryboardStage { code: "V6"; stageName: "机位方案"; stageState: root.stageState(5); hasLink: true }
                            StoryboardStage { code: "V7"; stageName: "光线方案"; stageState: root.stageState(6); hasLink: true }
                            StoryboardStage { code: "V8"; stageName: "连续性校验"; stageState: root.stageState(7); hasLink: true }
                        }
                    }
                }
            }

            // ─────────── 镜头详情卡（views.css:1.2fr 那一列）───────────
            Card {
                id: detailCard
                x: 0
                y: root.wrapY
                width: root.detailW
                height: root.detailCardH
                hoverable: false                  // 设计稿本页没有 .card.hoverable

                Item {
                    id: detailInner
                    width: parent.width
                    height: parent.height

                    StoryboardCardHead {
                        title: root.shot.code + " · 镜头详情"
                        glyph: "▥"
                        Row {
                            spacing: 8                         // .row gap-2
                            StoryboardButton {
                                text: "编辑镜头"
                                glyph: "✦"                    // Icon name="wand"
                                variant: "ghost"
                                sm: true
                                onClicked: root.editOpen = true
                            }
                            StoryboardButton {
                                text: "送去出图 →"
                                variant: "ghost"
                                sm: true
                                onClicked: root.flash("已送去出图工作区（演示）")
                            }
                        }
                    }

                    // .kv { grid-template-columns: auto 1fr; gap: 6px 14px; f12.5→12 }
                    Grid {
                        id: kvGrid
                        x: 0
                        y: 46
                        width: parent.width
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 6
                        KvKey { width: root.kvKeyW; text: "动作" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: root.shot.action }
                        KvKey { width: root.kvKeyW; text: "空间" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: "前景：灯笼 · 背景：长街雨幕" }
                        KvKey { width: root.kvKeyW; text: "表演" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: "expression=怅然 · eyes=湿润 · breathing=急促" }
                        KvKey { width: root.kvKeyW; text: "机位" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: "camera_id=C" + root.shot.id + " · composition=中央偏左" }
                        KvKey { width: root.kvKeyW; text: "光线" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: "lighting_id=L2 · 逆光剪影" }
                        KvKey { width: root.kvKeyW; text: "时长" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: root.shot.dur + "（24fps）" }
                        KvKey { width: root.kvKeyW; text: "情绪" }
                        KvVal { width: kvGrid.width - root.kvKeyW - 14; text: root.shot.mood }
                    }

                    // Beat 时间轴（Storyboard.jsx:106-125）
                    Item {
                        x: 0
                        y: 46 + root.kvH + 12                   // inline marginTop 12
                        width: parent.width
                        height: parent.height - y
                        Row {
                            x: 0
                            y: 0
                            spacing: 8                          // .row gap-2
                            Text {
                                text: "Beat 时间轴（可编辑 JSON）"
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 12              // .tiny
                                font.weight: Font.DemiBold
                                color: ThemeBridge.colors["text.secondary"]
                            }
                            Text {
                                text: "保存后写入 prompt_artifacts · 格式：[{\"t\": 秒, act: 动作 }]"
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 12
                                color: ThemeBridge.colors["text.muted"]
                            }
                        }
                        Rectangle {
                            id: beatsBox
                            x: 0
                            y: 12 * root.lh + 6               // inline marginBottom 6
                            width: parent.width
                            height: root.beatsBoxH
                            radius: root.rSm
                            color: beatsArea.activeFocus ? ThemeBridge.colors["bg.surface"]
                                                         : ThemeBridge.colors["fill.muted"]
                            border.width: 1
                            border.color: beatsArea.activeFocus ? ThemeBridge.colors["line.focus"]
                                       : beatsHover.containsMouse ? ThemeBridge.colors["line.strong"]
                                       : ThemeBridge.colors["line.normal"]
                            Behavior on border.color {
                                ColorAnimation { duration: root.reduce ? 0 : root.durFast }
                            }
                            // :focus 的 3px accent-dim 环（自绘，避开 layer 的 software 后端问题）
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: parent.radius + 3
                                color: "transparent"
                                border.width: 3
                                border.color: ThemeBridge.toneBg["accent"]
                                visible: beatsArea.activeFocus
                            }
                            TextEdit {
                                id: beatsArea
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                wrapMode: TextEdit.Wrap
                                // ⚠️ inline 的 lineHeight 1.8 落不了地：QQuickTextEdit 没有
                                // lineHeight / lineHeightMode（只有 QQuickText 有），本 Qt 6.11.2
                                // 实测确认。框高仍按设计稿 1.8 算（beatsBoxH），文字按默认行距
                                // 顶对齐渲染 —— 见交付说明里的「未对齐」清单。
                                font.family: "Consolas"                  // --font-mono 栈里必定存在的一项
                                font.pixelSize: 13                      // inline fontSize 13
                                color: ThemeBridge.colors["text.primary"]
                                selectionColor: ThemeBridge.colors["fill.selected"]
                                selectedTextColor: ThemeBridge.colors["text.primary"]
                                selectByMouse: true
                                text: root.beatsJson
                            }
                            MouseArea {
                                id: beatsHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                        Row {
                            x: 0
                            y: beatsBox.y + beatsBox.height + 8    // inline marginTop 8
                            spacing: 8                              // .row gap-2
                            StoryboardButton {
                                text: "保存 Beat 时间轴"
                                glyph: "✓"                          // Icon name="check"
                                variant: "primary"
                                sm: true
                                onClicked: root.flash("Beat 时间轴已保存（演示）")
                            }
                            StoryboardButton {
                                text: "批量情绪 → 同场景"
                                variant: "secondary"
                                sm: true
                                onClicked: root.flash("已应用批量情绪「" + root.shot.mood + "」到同场景镜头（演示）")
                            }
                        }
                    }
                }
            }

            // ─────────── 连续性卡（views.css:1fr 那一列）───────────
            Card {
                id: contCard
                x: root.detailW + root.wrapGap
                y: root.wrapY
                width: root.contW
                height: root.contCardH
                hoverable: false

                Item {
                    id: contInner
                    width: parent.width
                    height: parent.height

                    StoryboardCardHead {
                        title: "连续性 · C1–C12"
                        glyph: "◎"                            // Icon name="target"
                        StoryboardTag {
                            text: "通过"
                            tone: "ok"
                            sm: true
                        }
                    }

                    // .chips { display:flex; gap:6; flex-wrap:wrap } —— chip 自身不做位移，
                    // 所以这里可以用 Flow 做换行（高度由 chipsHeight() 按同一套宽度算出）
                    Flow {
                        id: chipsFlow
                        x: 0
                        y: 46
                        width: parent.width
                        height: root.contChipsH
                        spacing: root.chipsGap
                        StoryboardChip { label: root.cChecks[0].code + " " + root.cChecks[0].name;  width: root.chipCellWidth(root.cChecks[0]) }
                        StoryboardChip { label: root.cChecks[1].code + " " + root.cChecks[1].name;  width: root.chipCellWidth(root.cChecks[1]) }
                        StoryboardChip { label: root.cChecks[2].code + " " + root.cChecks[2].name;  width: root.chipCellWidth(root.cChecks[2]) }
                        StoryboardChip { label: root.cChecks[3].code + " " + root.cChecks[3].name;  width: root.chipCellWidth(root.cChecks[3]) }
                        StoryboardChip { label: root.cChecks[4].code + " " + root.cChecks[4].name;  width: root.chipCellWidth(root.cChecks[4]) }
                        StoryboardChip { label: root.cChecks[5].code + " " + root.cChecks[5].name;  width: root.chipCellWidth(root.cChecks[5]) }
                        StoryboardChip { label: root.cChecks[6].code + " " + root.cChecks[6].name;  width: root.chipCellWidth(root.cChecks[6]) }
                        StoryboardChip { label: root.cChecks[7].code + " " + root.cChecks[7].name;  width: root.chipCellWidth(root.cChecks[7]) }
                        StoryboardChip { label: root.cChecks[8].code + " " + root.cChecks[8].name;  width: root.chipCellWidth(root.cChecks[8]) }
                        StoryboardChip { label: root.cChecks[9].code + " " + root.cChecks[9].name;  width: root.chipCellWidth(root.cChecks[9]) }
                        StoryboardChip { label: root.cChecks[10].code + " " + root.cChecks[10].name; width: root.chipCellWidth(root.cChecks[10]) }
                        StoryboardChip { label: root.cChecks[11].code + " " + root.cChecks[11].name; width: root.chipCellWidth(root.cChecks[11]) }
                    }

                    Text {
                        x: 0
                        y: 46 + root.contChipsH + 8              // inline marginTop 8
                        width: parent.width
                        text: "校验只比较状态，不让 LLM 自行猜测连续性。"
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 12
                        color: ThemeBridge.colors["text.muted"]
                    }

                    // .dlist / .drow（inline width 84 的键列）
                    Column {
                        x: 0
                        y: 46 + root.contChipsH + 8 + 12 * root.lh + 10
                        width: parent.width
                        spacing: 0
                        DRow { k: "基线章外观"; v: "第 1 章 · 一致" }
                        DRow { k: "出场实体";   v: "林晚 · 回声灯" }
                        DRow { k: "引用资产";   v: "3 项 · 全部就绪" }
                        DRow { k: "Prompt 版本"; v: "v7（重生成 +1）"; last: true }
                    }
                }
            }

            // ─────────── 故事板时间线（views.css:951-1003）───────────
            Card {
                id: timelineCard
                x: 0
                y: root.timelineY
                width: root.bodyW
                height: root.timelineCardH
                hoverable: false

                Item {
                    id: timelineInner
                    width: parent.width
                    height: parent.height

                    StoryboardCardHead {
                        title: "故事板时间线 · 拖拽重排"
                        glyph: "▤"                              // Icon name="film"
                        Text {
                            text: "按住卡片拖动 · 时长条按比例"      // .tiny.dim
                            font.family: ThemeBridge.fontFamily
                            font.pixelSize: 12
                            color: ThemeBridge.colors["text.muted"]
                        }
                    }

                    // .timeline { display:flex; gap:10; overflow-x:auto; padding:8px 2px 12px }
                    // 卡片是 flex 子项（flex:none, 宽 128），但要 hover 抬位 → 卡片显式给 x
                    Flickable {
                        id: tlFlick
                        x: 0
                        y: 46
                        width: parent.width
                        height: root.tlBodyH
                        contentWidth: root.tlContentW
                        contentHeight: root.tlBodyH
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick

                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(0) * root.tlPitch
                            y: 8
                            code: root.shots[0].code
                            action: root.shots[0].action
                            dur: root.shots[0].dur
                            seconds: root.secs(root.shots[0].dur)
                            status: root.shots[0].status
                            selected: root.selectedIndex === 0
                            onPicked: root.selectedIndex = 0
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(1) * root.tlPitch
                            y: 8
                            code: root.shots[1].code
                            action: root.shots[1].action
                            dur: root.shots[1].dur
                            seconds: root.secs(root.shots[1].dur)
                            status: root.shots[1].status
                            selected: root.selectedIndex === 1
                            onPicked: root.selectedIndex = 1
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(2) * root.tlPitch
                            y: 8
                            code: root.shots[2].code
                            action: root.shots[2].action
                            dur: root.shots[2].dur
                            seconds: root.secs(root.shots[2].dur)
                            status: root.shots[2].status
                            selected: root.selectedIndex === 2
                            onPicked: root.selectedIndex = 2
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(3) * root.tlPitch
                            y: 8
                            code: root.shots[3].code
                            action: root.shots[3].action
                            dur: root.shots[3].dur
                            seconds: root.secs(root.shots[3].dur)
                            status: root.shots[3].status
                            selected: root.selectedIndex === 3
                            onPicked: root.selectedIndex = 3
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(4) * root.tlPitch
                            y: 8
                            code: root.shots[4].code
                            action: root.shots[4].action
                            dur: root.shots[4].dur
                            seconds: root.secs(root.shots[4].dur)
                            status: root.shots[4].status
                            selected: root.selectedIndex === 4
                            onPicked: root.selectedIndex = 4
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(5) * root.tlPitch
                            y: 8
                            code: root.shots[5].code
                            action: root.shots[5].action
                            dur: root.shots[5].dur
                            seconds: root.secs(root.shots[5].dur)
                            status: root.shots[5].status
                            selected: root.selectedIndex === 5
                            onPicked: root.selectedIndex = 5
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(6) * root.tlPitch
                            y: 8
                            code: root.shots[6].code
                            action: root.shots[6].action
                            dur: root.shots[6].dur
                            seconds: root.secs(root.shots[6].dur)
                            status: root.shots[6].status
                            selected: root.selectedIndex === 6
                            onPicked: root.selectedIndex = 6
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(7) * root.tlPitch
                            y: 8
                            code: root.shots[7].code
                            action: root.shots[7].action
                            dur: root.shots[7].dur
                            seconds: root.secs(root.shots[7].dur)
                            status: root.shots[7].status
                            selected: root.selectedIndex === 7
                            onPicked: root.selectedIndex = 7
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(8) * root.tlPitch
                            y: 8
                            code: root.shots[8].code
                            action: root.shots[8].action
                            dur: root.shots[8].dur
                            seconds: root.secs(root.shots[8].dur)
                            status: root.shots[8].status
                            selected: root.selectedIndex === 8
                            onPicked: root.selectedIndex = 8
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                        StoryboardShotCard {
                            x: 2 + root.order.indexOf(9) * root.tlPitch
                            y: 8
                            code: root.shots[9].code
                            action: root.shots[9].action
                            dur: root.shots[9].dur
                            seconds: root.secs(root.shots[9].dur)
                            status: root.shots[9].status
                            selected: root.selectedIndex === 9
                            onPicked: root.selectedIndex = 9
                            onGrabbedAt: function (cx) { root.pressSlot = root.slotAt(cx) }
                            onReleasedAt: function (cx) { root.moveSlot(root.pressSlot, root.slotAt(cx)); root.pressSlot = -1 }
                        }
                    }
                }
            }
        }
    }

    // 滚动条（base.css:63-82：10px 槽 + pill 滑块 + 3px 透明描边）
    Rectangle {
        id: vbarTrack
        x: parent.width - width
        y: root.padT
        width: 10
        height: parent.height - root.padT
        color: "transparent"
        visible: flick.contentHeight > flick.height
        Rectangle {
            width: 10
            radius: width / 2
            height: Math.max(30, vbarTrack.height * vbarTrack.height / flick.contentHeight)
            y: flick.contentY / Math.max(1, flick.contentHeight - flick.height)
                 * (vbarTrack.height - height)
            color: ThemeBridge.colors["bg.elevated"]
            border.width: 3
            border.color: "transparent"
        }
    }

    // ═══════════════════ 页面内小部件（设计稿的 flex 子元素，设计稿样式已照抄）═══════════════════
    component KvKey: Text {
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12                      // .kv 12.5 → 12
        lineHeight: 1.6
        lineHeightMode: Text.FixedHeight
        color: ThemeBridge.colors["text.muted"]
    }
    component KvVal: Text {
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
        font.weight: Font.Medium               // .kv .v 500
        lineHeight: 1.6
        lineHeightMode: Text.FixedHeight
        elide: Text.ElideRight
        color: ThemeBridge.colors["text.primary"]
    }
    // .dlist .drow { padding: 8px 2px; gap: 8px; f12.5→12; border-bottom: 1px line-subtle;
    //                hover 铺 fill-hover }（键列 inline width 84）
    component DRow: Item {
        id: dr
        property string k: ""
        property string v: ""
        property bool last: false
        width: parent ? parent.width : 0
        height: 8 * 2 + 12 * 1.6
        Rectangle {
            anchors.fill: parent
            color: ThemeBridge.colors["fill.hover"]
            visible: drHover.containsMouse
        }
        Rectangle {
            y: parent.height - 1
            width: parent.width
            height: 1
            color: ThemeBridge.colors["line.subtle"]
            visible: !dr.last                   // .dlist .drow:last-child { border-bottom: none }
        }
        Text {
            x: 2
            y: 8
            width: 84
            text: dr.k
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
            color: ThemeBridge.colors["text.muted"]
        }
        Text {
            x: 2 + 84 + 8
            y: 8
            width: parent.width - x
            text: dr.v
            elide: Text.ElideRight
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
            color: ThemeBridge.colors["text.primary"]
        }
        MouseArea {
            id: drHover
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
    }

    // 编辑镜头弹层（Storyboard.jsx:184-222）
    StoryboardDialog {
        anchors.fill: parent
        visible: root.editOpen
        shot: root.shot
        onApplied: function (action, mood, dur, note) { root.applyEdit(action, mood, dur, note) }
        onCancelled: root.editOpen = false
    }
}
