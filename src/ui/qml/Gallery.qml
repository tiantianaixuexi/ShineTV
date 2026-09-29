// src/ui/qml/Gallery.qml —— 组件画廊（对应 webui/src/views/Gallery.jsx）
//
// 设计稿：views.css:394-405 的 .gal-grid / .gal-row + views.css:132-152 的
// .vw / .vw-head，以及 ui.css 里逐个控件的定义。本页是**部件级复刻**：
// 卡片标题、几何、配色、字号全部取自 CSS 盒模型，不另立一套。
//
// ⚠️ 2026-09-29 重写了布局层。三条硬纪律，都是本仓实测踩出来的：
//
// 1. 根对象是 Ctl（Rectangle），不是 Window。QQuickWidget 要求 setSource 的
//    根是能进 contentItem 的 Item，给 Window 只会得到
//    `QQuickWidget: invalid root object.`。尺寸由宿主给
//    （SizeRootObjectToView + QuickHost::FillWithRoot），所以根上不写
//    width / height / visible。
//
// 2. **页面里不再出现任何高度常数**。旧版在 `cards` 数组里给 9 张卡各写死一个
//    `ch`（112 / 76 / 147 / 234 …），内容比常数高就溢出、**压住下一行的卡片**
//    （实测：Button 卡压住 Field 卡标题，中间一道黑带），比常数矮就留死白。
//    现在卡片高度 = `PanelCard.bodyH` = 加载后正文的 `implicitHeight`，
//    页面**一个高度数字都不用写**。共享组件改尺寸也不会再连带出布局错。
//
// 3. 卡片这一层交给共享 `AutoGrid.qml`（CSS `repeat(auto-fit, minmax(340px, 1fr))`
//    + `align-items: start` 的等价物），卡内横排交给共享 `Flex.qml`
//    （`flex-wrap: wrap` + align/justify）。**AutoGrid 只接管卡片的 x/y**，所以
//    卡片里面仍可以随便用 Column / 定位器。
//
// 4. 主题桥一律走**属性**（ThemeBridge.colors["bg.panel"]），不写方法调用：
//    方法调用在依赖图里是空的，求值一次就永久缓存，切主题不会重算。
import QtQuick
import Shine 1.0

// 卡片内容在 Component 里访问页面级 id（root.xxx）——显式声明 Bound，
// 别依赖默认的 pragma 行为。
//
// ⚠️ 刻意**不** import QtQuick.Controls：它自带一个 Button 类型，会把同目录
//    冻结的 Button.qml 遮掉（qmllint 直接报 `Could not find property "primary"`）。
//    代价是这一页没有 ScrollBar —— 设计稿本来也只有浏览器原生滚动条。
pragma ComponentBehavior: Bound

Ctl {
    id: root

    color: ThemeBridge.colors["bg.void"]

    // —— .vw { padding: 20px 24px 26px; gap: 16px } ——
    readonly property int padX: 24
    readonly property int padTop: 20
    readonly property int padBottom: 26
    readonly property int gap: ThemeBridge.spaces["4"]          // 16
    readonly property int minCol: 340                            // grid minmax(340px, 1fr)

    // .vw-head 高 = 标题 18px 行高(≈22) + 副标题 12.5px 行高(≈15) ≈ 37
    readonly property int headH: 37

    // —— 页面交互态（对应 Gallery.jsx 的 useState）——
    // Segmented 的真值是**选项的 value 字符串**（Gallery.jsx 的 useState('列表')），
    // 不是下标 —— 下标只是位置身份，重排一次就全错。
    property string segValue: "列表"
    property int tabIndex: 1            // Tabs 初值「分镜」
    property bool sw: true              // Switch 自动运行
    property bool ck: true              // Checkbox 允许超期降级
    property int prog: 64               // Progress 初值

    // 卡片清单。注意：**没有任何高度字段** —— 高度由各卡片正文的 implicitHeight
    // 自己决定（见文件头纪律 2）。
    readonly property var cards: [
        { key: "button", title: "Button 按钮 · 变体 4 × 尺寸 3",    icon: "zap"      },
        { key: "tag",    title: "Tag / Badge / Kbd / StatusDot",   icon: "list"     },
        { key: "seg",    title: "Segmented / Tabs / 进度",          icon: "target"   },
        { key: "field",  title: "Field 表单项 · 五态控件",           icon: "settings" },
        { key: "table",  title: "DataTable 表格 · 排序 / 筛选 / 选中", icon: "grid"    },
        { key: "flow",   title: "StageFlow 阶段流 · 节点四态",        icon: "flow"     },
        { key: "art",    title: "Art 占位画 · 程序化生成",           icon: "image"    },
        { key: "ink",    title: "水墨装饰 ArtInk · 随主题渲染",      icon: "palette"  },
        { key: "empty",  title: "空态 / 反馈",                       icon: "info"     }
    ]

    function bodyFor(k) {
        switch (k) {
        case "button": return cButton
        case "tag":    return cTag
        case "seg":    return cSeg
        case "field":  return cField
        case "table":  return cTable
        case "flow":   return cFlow
        case "art":    return cArt
        case "ink":    return cInk
        case "empty":  return cEmpty
        }
        return cEmpty
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        // .vw { padding: 20px 24px 26px }：上 20 + 头 37 + gap 16 + 网格 + 下 26
        // 网格高度由 Grid 自己算（每行取该行最高卡片），这里不再有任何常数。
        readonly property real gridTop: root.padTop + root.headH + root.gap
        contentHeight: gridTop + galGrid.implicitHeight + root.padBottom
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // —— .vw-head ——
        Item {
            id: head
            x: root.padX
            y: root.padTop
            width: root.width - root.padX * 2
            height: root.headH

            Icon {
                id: headIcon
                x: 0
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                name: "grid"
                glyphColor: ThemeBridge.colors["accent.primary"]
            }
            Text {
                x: headIcon.width + 14                  // .vw-head gap 14
                y: 0
                height: 22
                verticalAlignment: Text.AlignVCenter
                text: "组件画廊"
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 18
                font.weight: Font.Bold                 // 800
            }
            Text {
                // 标题与副标题同在 .vw-head 的那个 div 里，左边缘对齐
                x: headIcon.width + 14                  // .vw-head gap 14
                y: 22
                height: 15
                verticalAlignment: Text.AlignVCenter
                text: "设计稿组件总览 · 对应 Qt 端 verify/gallery · 36 个组件 / 5 组"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12                     // 12.5px → 就近取整
            }
            Tag {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "Web 设计稿"
                tone: "accent"
            }
        }

        // —— .gal-grid ——
        // CSS: `display:grid; grid-template-columns: repeat(auto-fit, minmax(340px,1fr));
        //        gap:16px; align-items:start`（views.css:394-405）
        //
        // AutoGrid.qml 负责列数自适应 + 1fr 列宽 + 「行高取该行最高、卡片顶边对齐」。
        // 卡片宽度读 `galGrid.cellW`，**不要**自己算 —— 少一个子项少一列时
        // 自己算的那份立刻就和 Grid 的排版对不上。
        AutoGrid {
            id: galGrid
            x: root.padX
            y: flick.gridTop
            width: flick.width - root.padX * 2
            minCell: root.minCol
            gap: root.gap

            Repeater {
                model: root.cards
                delegate: PanelCard {
                    id: card
                    required property var modelData

                    width: galGrid.cellW
                    title: modelData.title
                    icon: modelData.icon
                    body: root.bodyFor(modelData.key)
                }
            }
        }
    }

    // ================================================================
    // 卡片内容
    //
    // 每张卡的正文都是一个 Column：行内元素给 `width: parent.width`，
    // 行高由各自的 implicitHeight 决定，行距由 Column 的 spacing 给。
    // **不再有任何 `y:`** —— 纵向位置全部由 Column 算。
    // ================================================================

    // —— Card 1：Button 变体 4 × 尺寸 3 ——
    //
    // ⚠️ 2026-09-29 重写。旧版第一行把 SVG 图标**摆在按钮外面**（Item 垫在 Flex 里、
    //    图标贴按钮左侧 6px、x 手写 21），第四颗「危险」拿 secondary 顶，三档尺寸全是
    //    30px。那是共享 Button 没有图标槽 / danger / lg 时的绕法，现在共享件补齐了三样，
    //    页面直接用设计稿的形态：图标在按钮**内部**（.btn .icon 15 + gap 6），
    //    四种 variant 各归各位，尺寸 sm / md / lg 三档。
    Component {
        id: cButton
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 12                          // .col gap-3

            Flex {
                width: parent.width
                gap: ThemeBridge.spaces["4"]      // .gal-row gap 10

                Button {
                    id: b1
                    text: "主要"
                    variant: "primary"
                    iconSlot: Component {
                        Icon { anchors.fill: parent; name: "play"; glyphColor: b1.fgColor }
                    }
                }
                Button {
                    id: b2
                    text: "次要"
                    variant: "secondary"
                    iconSlot: Component {
                        Icon { anchors.fill: parent; name: "layers"; glyphColor: b2.fgColor }
                    }
                }
                Button {
                    id: b3
                    text: "幽灵"
                    variant: "ghost"
                    iconSlot: Component {
                        Icon { anchors.fill: parent; name: "eye"; glyphColor: b3.fgColor }
                    }
                }
                Button {
                    id: b4
                    text: "危险"
                    variant: "danger"               // Gallery.jsx:39 / ui.css:77-85
                    iconSlot: Component {
                        Icon { anchors.fill: parent; name: "alert"; glyphColor: b4.fgColor }
                    }
                }
            }
            // 第 2 行：Gallery.jsx:42-46 —— sm / md / lg 三档 + loading + disabled
            Flex {
                width: parent.width
                gap: ThemeBridge.spaces["4"]
                Button { text: "小号"; variant: "primary"; sm: true }
                Button { text: "中号"; variant: "primary" }
                Button { text: "大号"; variant: "primary"; lg: true }
                Button { text: "加载中"; variant: "primary"; loading: true }
                Button { text: "禁用"; variant: "primary"; enabled: false }
            }
            Flex {
                width: parent.width
                // ⚠️ 共享 IconBtn 的槽位是 `glyph`（字符位）不是 `icon`（图标名），
                //    字符取自 Widgets 侧同一张卡片的同一组图标（WidgetGalleryView.cpp:538-545），
                //    tip 文案照 Gallery.jsx:49-51。设计稿的 SVG 描边图标在本仓没有资源，
                //    共享件统一降级成字符。
                // ⚠️ `active` 归调用方：共享 IconBtn 只发 clicked()、不自翻转，
                //    所以第三颗要自己绑。
                IconBtn { glyph: "↻"; tip: "图标钮 · tooltip" }        // Icon name="refresh"
                IconBtn { glyph: "⤓"; tip: "下载" }                      // Icon name="download"
                IconBtn { glyph: "⚙"; tip: "设置"; active: true }         // Icon name="settings"
            }
        }
    }

    // —— Card 2：Tag / Badge / Kbd / StatusDot ——
    //
    // 三行都是 .gal-row（gap 10，flex-wrap: wrap），行间 .col gap-3 = 12。
    Component {
        id: cTag
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 12
            Flex {
                width: parent.width
                Tag { text: "已提交"; tone: "ok" }
                Tag { text: "草稿"; tone: "warn" }
                Tag { text: "失败"; tone: "danger" }
                Tag { text: "运行中"; tone: "busy" }
                Tag { text: "参考就绪"; tone: "info" }
                Tag { text: "降级 B"; tone: "accent" }
                Tag { text: "待出图"; tone: "" }
            }
            Flex {
                width: parent.width
                Dot { tone: "ok" }
                Dot { tone: "warn" }
                Dot { tone: "danger" }
                Dot { tone: "busy"; run: true }
                Dot { tone: "idle" }
                Dot { tone: "pending" }
                Text {
                    text: "状态六态"
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
            }
            Flex {
                width: parent.width
                gap: 5                          // 「快捷键：」后面紧跟一串键帽
                Text {
                    text: "快捷键："
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
                Kbd { text: "Ctrl" }
                Kbd { text: "K" }
                Kbd { text: "Ctrl" }
                Kbd { text: "B" }
                Kbd { text: "Ctrl" }
                Kbd { text: "Enter" }
            }
        }
    }

    // —— Card 3：Segmented / Tabs / 进度 ——
    Component {
        id: cSeg
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 12
            // ⚠️ 共享 Seg 的槽位是 `options: [{value, label}]` + `value: string` +
            //    `picked(v)` 信号 —— 组件**不改**自己的 value，真值归调用方。
            Seg {
                width: parent.width
                options: [
                    { value: "list",   label: "列表" },
                    { value: "board",  label: "看板" },
                    { value: "period", label: "日/周/月" }
                ]
                value: root.segValue
                onPicked: (v) => { root.segValue = v }
            }
            Tabs {
                width: parent.width
                tabs: ["剧本", "分镜", "渲染", "设定集"]
                current: root.tabIndex
            }
            // 进度行：.row gap-3（Gallery.jsx:89-94）—— grow 进度条 + 两个步进钮 +
            // 右对齐百分比。
            //
            // ⚠️ 这一行的**定位方向是从右往左**：两个步进钮先按自身宽度从右端排开，
            //    进度条再由 minusBtn.x 倒推宽度。写成
            //    `progBar.width = minusBtn.x - 12` 且 `minusBtn.x = progBar.width + 12`
            //    —— width ↔ x 互相依赖，是一条真绑定环。
            Item {
                width: parent.width
                height: 30
                Progress {
                    id: progBar
                    x: 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(40, minusBtn.x - 12)
                    value: root.prog
                    run: true
                }
                Button {
                    id: plusBtn
                    x: parent.width - 34 - 12 - width
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+"
                    variant: "secondary"
                    onClicked: root.prog = Math.min(100, root.prog + 18)
                }
                Button {
                    id: minusBtn
                    x: plusBtn.x - 12 - width
                    anchors.verticalCenter: parent.verticalCenter
                    text: "-"
                    variant: "secondary"
                    onClicked: root.prog = Math.max(0, root.prog - 18)
                }
                Text {
                    x: parent.width - 34
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34
                    horizontalAlignment: Text.AlignRight
                    text: root.prog + "%"
                    color: ThemeBridge.colors["text.muted"]
                    font.family: "Cascadia Code, JetBrains Mono, Consolas, monospace"
                    font.pixelSize: 12
                }
            }
            // justify: "center" —— 这一行是「Spinner / 加载态」的说明行，末行居中比
            // 挂左边像样。设计稿 .gal-row 没写 justify-content（默认 flex-start），
            // 这是本页刻意的一处视觉取舍，其余 .gal-row 一律保持 start。
            Flex {
                width: parent.width
                justify: "center"
                Spinner { }
                Spinner { sm: true }
                Text {
                    text: "Spinner / 加载态"
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
            }
        }
    }

    // —— Card 4：Field 表单项 · 五态控件 ——
    //
    // 三块：Field1「项目名称」/ 两栏行（供应商 + 自动运行/复选框）/ Field3「备注」。
    // 纵向位置由 Column 管，行内那两条列宽由内部的 Item 管。
    Component {
        id: cField
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 12
            Field {
                width: parent.width
                label: "项目名称"
                help: "例如：灯语回声"
                Input { width: parent.width; placeholder: "输入项目名称…" }
            }
            // .row gap-4：左 Field(flex:1) + 右侧 col(paddingTop 18)
            Item {
                width: parent.width
                // 高 = 右列（18 上内边距 + 19 开关行 + 8 + 17 复选框 = 62）
                height: 62
                Field {
                    x: 0
                    y: 0
                    // 右侧列自然宽 ≈ 95（「允许超期降级」复选框），行间距 gap-4 = 16
                    width: parent.width - 16 - 100
                    label: "供应商"
                    Select {
                        width: parent.width
                        options: ["小米 MiMo", "OpenAI 兼容", "自定义端点"]
                    }
                }
                // ⚠️ 这一列内部**不能用 anchors.verticalCenter**：外层若再套定位器，
                //    两者同时赋 y 会 binding loop。这里它挂在一个普通 Item 上，
                //    x 显式写、y 由自己的内容高度决定。
                Column {
                    x: parent.width - 100
                    y: 18                                  // paddingTop: 18
                    width: 100
                    spacing: 8                             // .col gap-2

                    Item {
                        width: 100
                        height: 19
                        Text {
                            x: 0
                            y: Math.round((19 - height) / 2)
                            text: "自动运行"
                            color: ThemeBridge.colors["text.secondary"]
                            font.family: ThemeBridge.fontFamily
                            font.pixelSize: 12
                        }
                        Toggle { x: 48; y: 0; on: root.sw }
                    }
                    Toggle { check: true; on: root.ck; text: "允许超期降级" }
                }
            }
            Field {
                width: parent.width
                label: "备注"
                controlH: 58
                TextArea {
                    width: parent.width
                    rows: 2
                    placeholder: "TextArea · 可拖拽调整高度…"
                }
            }
        }
    }

    // —— Card 5：DataTable ——
    //
    // ⚠️ 表格**自己算高**（height: implicitHeight），父只给宽度 —— 行数由模型决定，
    //    不再依赖外部高度是否够（上一版用 anchors.fill，末行被卡片底边切掉）。
    Component {
        id: cTable
        Table {
            width: parent.width
            height: implicitHeight
            headers: ["#", "镜号", "情绪", "时长", "状态"]
            headerWidths: [18, 28, 30, 26]
            selectedIndex: 2
            rows: [
                { cells: ["01", "S01", "静谧", "3.5s"], tone: "ok",   label: "完成",   sel: false },
                { cells: ["02", "S02", "温柔", "2.8s"], tone: "ok",   label: "完成",   sel: false },
                { cells: ["03", "S04", "怅然", "2.2s"], tone: "busy", label: "生成中", sel: true  },
                { cells: ["04", "S05", "紧张", "4.1s"], tone: "busy", label: "生成中", sel: false },
                { cells: ["05", "S06", "惊喜", "3.0s"], tone: "idle", label: "待出图", sel: false }
            ]
        }
    }

    // —— Card 6：StageFlow 阶段流 ——
    Component {
        id: cFlow
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 12
            StageFlow {
                width: parent.width
                stages: [
                    { code: "T1",  name: "章节初始化" },
                    { code: "T2",  name: "本章目标" },
                    { code: "T3",  name: "大纲" },
                    { code: "T11", name: "正文写作" },
                    { code: "T12", name: "章节评审" },
                    { code: "T16", name: "状态提交" }
                ]
                // ⚠️ 属性名必须是 **stageStates**，不能写 `states`：
                //    `states` 撞 Item 内建成员，赋值会静默落到基类属性上，
                //    stageStates 永远是 []，stAt() 全部回落到 todo —— 四态样式
                //    写得再对也不显形。qmllint 查不出「调用方属性名 ≠ 组件声明名」。
                // Gallery.jsx: i<2 done / i===2 run / i===4 fail
                stageStates: ["done", "done", "run", "todo", "fail", "todo"]
            }
            Text {
                width: parent.width
                height: 28
                wrapMode: Text.Wrap
                text: "todo / running / done / failed / skipped —— 小说 T1–T17、初始化 I1–I16、分镜 V1–V8、连续性 C1–C12 通用。"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
            // ⚠️ 共享 Kv 的 rows 键名是 `{key, value}`，且**键列宽由组件自己测**
            //    （TextMetrics + 延后重算）—— 页面不再回填 keyW，也不再挂
            //    ThemeBridge 的 Connections 监听字体族变化。
            Kv {
                width: parent.width
                rows: [
                    { key: "阶段产物", value: "214 个" },
                    { key: "LLM 调用", value: "86 次 · 高档 12" },
                    { key: "估算成本", value: "¥12.40" }
                ]
            }
        }
    }

    // —— Card 7：Art 占位画 ——
    //
    // 6 张 92×60 的缩略图。6×92 + 5×10 = 602px，**装不进**卡片内容区
    // （~330–420px）—— 设计稿靠 `.gal-row { flex-wrap: wrap }`（views.css:405）
    // 换行。卡片宽时放 4 张、第二行 2 张。
    //
    // ⚠️ justify: "center"：末行那 2 张若按 CSS 默认的 flex-start 挂左边，
    //    右边空一大块，在「组件陈列」语境下明显破相（用户点名过）。
    //    这是本页刻意偏离设计稿默认值的一处，其余 .gal-row 保持 start。
    Component {
        id: cArt
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 10
            Flex {
                width: parent.width
                justify: "center"
                // ⚠️ 共享 Art 的 `seed` 槽位与旧页私有件同名同义；调色板统一成设计稿
                //    的 12 组。设计稿这里写了 className="hoverable"（Gallery.jsx:151），
                //    但 `.art.hoverable` 在 CSS 里**没有规则**（1.07 的缩放挂在
                //    `.asset-card:hover .thumb svg`），所以本页不开 zoom/zoomed。
                Repeater {
                    model: 6
                    delegate: Art {
                        required property int index
                        width: 92
                        height: 60
                        seed: index
                    }
                }
            }
            Text {
                width: parent.width
                height: 14
                verticalAlignment: Text.AlignVCenter
                text: "无外部资源 · SVG 渐变 + 山形剪影，种子决定配色与构图。"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
        }
    }

    // —— Card 8：水墨装饰 ArtInk（Widgets 侧缺失，本页补齐）——
    Component {
        id: cInk
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 10
            ArtInk {
                width: parent.width
                height: 150
                seed: 2
            }
            Text {
                width: parent.width
                height: 14
                verticalAlignment: Text.AlignVCenter
                text: "远山淡墨 → 中景浓墨 → 近景焦墨 + 印章，颜色取自当前主题 Token。"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
        }
    }

    // —— Card 9：空态 / 反馈 ——
    Component {
        id: cEmpty
        Column {
            height: implicitHeight      // 正文自钉高度，别让 Loader 接管（见 PanelCard 注释）
            width: parent.width
            spacing: 10
            Empty {
                width: parent.width
                icon: "folder"
                title: "还没有项目"
                text: "新建一个项目，开始你的第一部作品"
            }
            Flex {
                width: parent.width
                justify: "center"
                // Toast 四色（ui.css 的 .toast-ok/warn/err/info）在本仓是 Widgets 侧的
                // QML toast，QML 页只摆四个按钮示意。variant 照 Gallery.jsx:170-173：
                // secondary / primary / secondary / **danger**（旧版 Error 也是
                // secondary，共享 Button 没有 danger 时的凑数，现在归位）。
                Button { text: "Info"; variant: "secondary" }
                Button { text: "Success"; variant: "primary" }
                Button { text: "Warning"; variant: "secondary" }
                Button { text: "Error"; variant: "danger" }
            }
            Text {
                width: parent.width
                height: 14
                verticalAlignment: Text.AlignVCenter
                text: "Toast 四色 · 右下角弹出 · 3.4s 自动消失。"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
        }
    }
}
