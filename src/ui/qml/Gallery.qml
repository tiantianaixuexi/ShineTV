// src/ui/qml/Gallery.qml —— 组件画廊（对应 webui/src/views/Gallery.jsx）
//
// 设计稿：views.css:394-405 的 .gal-grid / .gal-row + views.css:132-152 的
// .vw / .vw-head，以及 ui.css 里逐个控件的定义。本页是**部件级复刻**：
// 卡片标题、几何、配色、字号全部取自 CSS 盒模型，不另立一套。
//
// ⚠️ 四条定位纪律（与 Parity.qml 同源，都是本仓实测踩出来的）：
//
// 1. 根对象是 Ctl（Rectangle），不是 Window。QQuickWidget 要求 setSource 的
//    根是能进 contentItem 的 Item，给 Window 只会得到
//    `QQuickWidget: invalid root object.`。尺寸由宿主给
//    （SizeRootObjectToView + QuickHost::FillWithRoot），所以根上不写
//    width / height / visible。
//
// 2. 页面这一层**不套 Flow/Row/Grid/Column**。定位器会接管子项的 x/y，
//    与 hover 位移抢 y 会打出 binding loop（卡片全叠在第一行）。卡片一律
//    显式 x/y，网格由下面的 gridSlots() 按 CSS grid 的行规则算出来。
//
// 3. 每张 Card 内部只放一个填满的 Item 再绝对定位 —— Card.qml 的 body 是
//    Column，直接子项的 y 归它管（见 GalleryCard.qml）。
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

    // 卡片清单：ch = .card-b 内容盒高度（不含标题栏与 16 下内边距）
    // 高度按 CSS 盒模型算：行内 gap 10，行间 .col gap-3 = 12。
    readonly property var cards: [
        { key: "button", title: "Button 按钮 · 变体 4 × 尺寸 3",    icon: "zap",      ch: 112 },
        { key: "tag",    title: "Tag / Badge / Kbd / StatusDot",   icon: "list",     ch: 76  },
        { key: "seg",    title: "Segmented / Tabs / 进度",          icon: "target",   ch: 147 },
        { key: "field",  title: "Field 表单项 · 五态控件",           icon: "settings", ch: 234 },
        { key: "table",  title: "DataTable 表格 · 排序 / 筛选 / 选中", icon: "grid",    ch: 206 },
        { key: "flow",   title: "StageFlow 阶段流 · 节点四态",        icon: "flow",     ch: 162 },
        { key: "art",    title: "Art 占位画 · 程序化生成",           icon: "image",    ch: 154 },
        { key: "ink",    title: "水墨装饰 ArtInk · 随主题渲染",      icon: "palette",  ch: 174 },
        { key: "empty",  title: "空态 / 反馈",                       icon: "info",     ch: 244 }
    ]

    // CSS grid：repeat(auto-fit, minmax(340px, 1fr)) + gap 16 + align-items: start
    // —— align-items:start 意味着同一行的卡片**顶边对齐**，行高取该行最高者。
    //
    // ⚠️ 宽度未定（QQuickWidget 还没给尺寸，root.width 还是 0）时**也必须返回
    //    满长度数组**：delegate 绑的是 slots[index].x/y/w/h，一旦这里返回短
    //    数组，那一轮求值就会抛一串 `Cannot read property 'x' of undefined`。
    //    宽度为 0 就把几何全给 0，等宿主给完尺寸自然重算。
    readonly property var slots: gridSlots()

    function gridSlots() {
        var out = []
        var n = cards.length
        var w = root.width - root.padX * 2
        var cw = 0
        var cols = 1
        if (w > 0) {
            cols = Math.max(1, Math.floor((w + root.gap) / (root.minCol + root.gap)))
            cw = (w - root.gap * (cols - 1)) / cols
        }
        var rowTop = 0
        var rowMax = 0
        for (var i = 0; i < n; ++i) {
            var r = Math.floor(i / cols)
            var c = i % cols
            if (c === 0) {
                if (r > 0) rowTop = rowTop + rowMax + root.gap
                rowMax = 0
            }
            out.push({
                x: root.padX + c * (cw + root.gap),
                y: root.padTop + root.headH + root.gap + rowTop,
                w: cw,
                h: cards[i].ch + 41 + 16              // headH(41) + .card-b 下内边距
            })
            rowMax = Math.max(rowMax, cards[i].ch)
        }
        return out
    }

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
        anchors.fill: parent
        contentWidth: width
        // .vw { padding: 20px 24px 26px }：上 20 + 头 37 + gap 16 + 网格 + 下 26
        readonly property real gridTop: root.padTop + root.headH + root.gap
        contentHeight: root.slots.length > 0
                        ? root.slots[root.slots.length - 1].y - gridTop
                          + root.slots[root.slots.length - 1].h + 26
                        : root.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // —— .vw-head ——
        Item {
            id: head
            x: root.padX
            y: root.padTop
            width: root.width - root.padX * 2
            height: root.headH

            GalleryIcon {
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
        Repeater {
            model: root.cards
            delegate: GalleryCard {
                id: card
                required property int index
                required property var modelData

                x: root.slots[index].x
                y: root.slots[index].y
                width: root.slots[index].w
                height: root.slots[index].h
                title: modelData.title
                icon: modelData.icon
                contentH: modelData.ch

                Loader {
                    width: parent.width
                    height: parent.height
                    sourceComponent: root.bodyFor(card.modelData.key)
                }
            }
        }
    }

    // ================================================================
    // 卡片内容：每张卡的 .card-b 内部按 .col / .row / .gal-row 显式定位
    // ================================================================

    // —— Card 1：Button 变体 4 × 尺寸 3 ——
    //
    // ⚠️ 第一行的四个按钮仍然把图标包在按钮**外面**（Item 垫在 GalleryFlow 里，
    //    图标贴按钮左侧 6px，ui.css `.btn .icon { width: 15px }`）。
    //    共享 Button 现在确实有 `glyph` 槽（Button.qml:60），但那个槽画的是**字符**，
    //    而这里的 GalleryIcon 是逐条照抄 Icon.jsx 的 PathSvg 描边图标 —— 换 glyph 等于
    //    把设计稿的 SVG 降级成一个字符。所以结构差异保留：图标落在 14px 内边距之外，
    //    而不是 CSS 的 padding 里面。这是本页的已知取舍。
    Component {
        id: cButton
        Item {
            GalleryFlow {
                x: 0; y: 0
                width: parent.width
                Item {
                    width: 15 + 6 + b1.width; height: 30
                    GalleryIcon {
                        y: Math.round((30 - 15) / 2)
                        width: 15; height: 15; name: "play"
                        glyphColor: ThemeBridge.colors["accent.primary.fg"]
                    }
                    Button { id: b1; x: 21; text: "主要"; variant: "primary" }
                }
                Item {
                    width: 15 + 6 + b2.width; height: 30
                    GalleryIcon {
                        y: Math.round((30 - 15) / 2)
                        width: 15; height: 15; name: "layers"
                        glyphColor: ThemeBridge.colors["text.primary"]
                    }
                    Button { id: b2; x: 21; text: "次要"; variant: "secondary" }
                }
                Item {
                    width: 15 + 6 + b3.width; height: 30
                    GalleryIcon {
                        y: Math.round((30 - 15) / 2)
                        width: 15; height: 15; name: "eye"
                        glyphColor: ThemeBridge.colors["text.primary"]
                    }
                    Button { id: b3; x: 21; text: "幽灵"; variant: "ghost" }
                }
                Item {
                    width: 15 + 6 + b4.width; height: 30
                    GalleryIcon {
                        y: Math.round((30 - 15) / 2)
                        width: 15; height: 15; name: "alert"
                        glyphColor: ThemeBridge.colors["text.primary"]
                    }
                    // 设计稿是 variant="danger"（ui.css:77-85）。共享 Button 的 variant
                    // 只有 primary/secondary/ghost 三个槽（Button.qml:38-44 记着「不补
                    // 第四套配色」），所以本页拿 secondary 顶 —— 是页面的取舍，
                    // 不在共享件里加 danger。
                    Button { id: b4; x: 21; text: "危险"; variant: "secondary" }
                }
            }
            // 第 2 行：设计稿这行**没有** icon（Gallery.jsx:41-47），只有 loading
            // 那个按钮内嵌转圈（UI.jsx:11）。同样因为冻结 Button 没有 loading
            // 变体，转圈贴在按钮左侧。
            GalleryFlow {
                x: 0; y: 42
                width: parent.width
                // ⚠️ 设计稿这三档是 sm / md / lg（Gallery.jsx:42-44），共享 Button 只有
                //    sm 一档（Button.qml:62 记着「没有 lg 槽位」），所以三颗都是 30px 高 ——
                //    尺寸档的缺口在本页，不在共享件。「加载中」的转圈同理：共享 Button
                //    没有 loading 变体（Button.qml:38-44），转圈仍贴在按钮左侧。
                Button { text: "小号"; variant: "primary" }
                Button { text: "中号"; variant: "primary" }
                Button { text: "大号"; variant: "primary" }
                Item {
                    // 设计稿的 loading 转圈是 13×13（UI.jsx:11），共享 Spinner
                    // 的自然尺寸是 14，这里按 14 排位。
                    width: 14 + 6 + bl.width; height: 30
                    Spinner { y: Math.round((30 - 14) / 2) }
                    Button { id: bl; x: 20; text: "加载中"; variant: "primary" }
                }
                Button { text: "禁用"; variant: "primary"; enabled: false }
            }
            GalleryFlow {
                x: 0; y: 84
                width: parent.width
                // ⚠️ 共享 IconBtn 的槽位是 `glyph`（字符位）不是 `icon`（图标名），
                //    字符取自 Widgets 侧同一张卡片的同一组图标（WidgetGalleryView.cpp:538-545），
                //    tip 文案照 Gallery.jsx:49-51。设计稿的 SVG 描边图标在本仓没有资源，
                //    共享件统一降级成字符（IconBtn.qml:35）。
                // ⚠️ `active` 归调用方：共享 IconBtn 只发 clicked()、不自翻转
                //    （IconBtn.qml:87-90），所以第三颗要自己绑。
                IconBtn { glyph: "↻"; tip: "图标钮 · tooltip" }        // Icon name="refresh"
                IconBtn { glyph: "⤓"; tip: "下载" }                      // Icon name="download"
                IconBtn {
                    glyph: "⚙"                                            // Icon name="settings"
                    tip: "设置"
                    active: true
                }
            }
        }
    }

    // —— Card 2：Tag / Badge / Kbd / StatusDot ——
    //
    // 三行都是 .gal-row（gap 10，flex-wrap: wrap），行间 .col gap-3 = 12。
    // ch = 20 + 12 + 14 + 12 + 18 = 76。
    Component {
        id: cTag
        Item {
            GalleryFlow {
                x: 0; y: 0
                width: parent.width
                Tag { text: "已提交"; tone: "ok" }
                Tag { text: "草稿"; tone: "warn" }
                Tag { text: "失败"; tone: "danger" }
                Tag { text: "运行中"; tone: "busy" }
                Tag { text: "参考就绪"; tone: "info" }
                Tag { text: "降级 B"; tone: "accent" }
                Tag { text: "待出图"; tone: "" }
            }
            GalleryFlow {
                x: 0; y: 32
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
            GalleryFlow {
                x: 0; y: 58
                width: parent.width
                gap: 5                          // 「快捷键：」后面紧跟一串键帽
                Text {
                    text: "快捷键："
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                }
                GalleryKbd { text: "Ctrl" }
                GalleryKbd { text: "K" }
                GalleryKbd { text: "Ctrl" }
                GalleryKbd { text: "B" }
                GalleryKbd { text: "Ctrl" }
                GalleryKbd { text: "Enter" }
            }
        }
    }

    // —— Card 3：Segmented / Tabs / 进度 ——
    Component {
        id: cSeg
        Item {
            // ⚠️ 共享 Seg 的槽位是 `options: [{value, label}]` + `value: string` +
            //    `picked(v)` 信号 —— 组件**不改**自己的 value（Seg.qml:52-53），
            //    真值归调用方。旧的页私有 Seg 是 `string[] + current: int` 且组件
            //    **自改** current，语义不一样，所以页面状态也换成了字符串 value
            //    （对应 Gallery.jsx:16 的 useState('列表')）。
            Seg {
                x: 0; y: 0
                options: [
                    { value: "list",   label: "列表" },
                    { value: "board",  label: "看板" },
                    { value: "period", label: "日/周/月" }
                ]
                value: root.segValue
                onPicked: (v) => { root.segValue = v }
            }
            GalleryTabs {
                x: 0; y: 46
                tabs: ["剧本", "分镜", "渲染", "设定集"]
                current: root.tabIndex
            }
            // 进度行：.row gap-3（Gallery.jsx:89-94）—— grow 进度条 + 两个步进钮 + 右对齐百分比。
            //
            // ⚠️ 这一行的**定位方向换成了从右往左**：两个步进钮先按自身宽度从右端排开，
            //    进度条再由 minusBtn.x 倒推宽度。旧写法是
            //    `progBar.width = minusBtn.x - 12` 且 `minusBtn.x = progBar.width + 12`
            //    —— width ↔ x 互相依赖，是一条真绑定环；新 Progress 的 implicitWidth
            //    是 90（旧页私有件是 160），环一断，宽度就停在隐式值上。
            Item {
                x: 0; y: 91
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
                    // 共享 Button 有 `signal clicked()`（Button.qml:63），这里直接接。
                    // 旧写法是在按钮上叠一层 hoverEnabled:false 的透明 MouseArea 抢点击
                    // （命中测试按 z 序找最上层的 mouse handler，所以点击归它、hover 仍落到
                    // 按钮自己的 MouseArea 上）—— 那是旧 Button 没有对外点击钩子时的绕法，
                    // 现在两行都删掉，hover 与点击都由组件内部那一层 MouseArea 负责。
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
            // GalleryFlow 自己按 align-items:center 垂直居中，所以子项
            // **不能**再挂 anchors.verticalCenter（会和 flow 写的 y 打架）。
            GalleryFlow {
                x: 0; y: 133
                width: parent.width
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
    // 逐块高度（.field 是 column gap 6）：
    //   Field1 「项目名称」  label 14 + 6 + input 30 + 6 + help 14 = 70
    //   + .col gap-3 = 12
    //   Row2               18(paddingTop) + 19(开关行) + 8(gap-2) + 17(复选框) = 62
    //   + 12
    //   Field3 「备注」     label 14 + 6 + textarea 58 = 78
    //   → ch = 70 + 12 + 62 + 12 + 78 = 234
    //
    // ⚠️ 上一版把 Row2 放在 y=62，正好压进 Field1 的 help（56..70）里 ——
    //    「例如：灯语回声」和「供应商」共用一条基线。现在各块高度按盒模型排开。
    Component {
        id: cField
        Item {
            GalleryField {
                x: 0; y: 0
                width: parent.width
                label: "项目名称"
                help: "例如：灯语回声"
                GalleryInput { width: parent.width; placeholder: "输入项目名称…" }
            }
            // .row gap-4：左 Field(flex:1) + 右侧 col(paddingTop 18)
            Item {
                x: 0; y: 82
                width: parent.width
                height: 62

                GalleryField {
                    x: 0; y: 0
                    // 右侧列自然宽 ≈ 95（「允许超期降级」复选框），行间距 gap-4 = 16
                    width: parent.width - 16 - 100
                    label: "供应商"
                    GallerySelect {
                        width: parent.width
                        options: ["小米 MiMo", "OpenAI 兼容", "自定义端点"]
                    }
                }
                // ⚠️ 这一列内部**不能用 anchors.verticalCenter**：Column 会接管
                //    子项的 y，两者同时赋 y 会 binding loop（上一版整列因此没画出来）。
                //    改成把 y 算出来直接写。
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
                        GalleryToggle { x: 48; y: 0; on: root.sw }
                    }
                    GalleryToggle { check: true; on: root.ck; text: "允许超期降级" }
                }
            }
            GalleryField {
                x: 0; y: 156
                width: parent.width
                label: "备注"
                controlH: 58
                GalleryTextArea {
                    width: parent.width
                    rows: 2
                    placeholder: "TextArea · 可拖拽调整高度…"
                }
            }
        }
    }

    // —— Card 5：DataTable ——
    //
    // th = 8 + 14(11px 行高) + 8 + 1 = 31；td = 9 + 16(13px 行高) + 9 + 1 = 35
    // 5 行 → ch = 31 + 5×35 = 206。
    // ⚠️ 上一版用 anchors.fill 让表格去吃父高度，末行被卡片底边切掉。
    //    这里改成表格**自己算高**（height: implicitHeight），父只给宽度 ——
    //    5 行由模型决定，不再依赖外部高度是否够。
    Component {
        id: cTable
        Item {
            GalleryTable {
                x: 0; y: 0
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
    }

    // —— Card 6：StageFlow 阶段流 ——
    Component {
        id: cFlow
        Item {
            StageFlow {
                x: 0; y: 0
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
                //    stageStates 永远是 []，stAt() 全部回落到 todo ——
                //    四态样式写得再对也不显形。qmllint 查不出「调用方属性名
                //    ≠ 组件声明名」这类错。
                // Gallery.jsx: i<2 done / i===2 run / i===4 fail
                stageStates: ["done", "done", "run", "todo", "fail", "todo"]
            }
            Text {
                x: 0; y: 50
                width: parent.width
                height: 28
                wrapMode: Text.Wrap
                text: "todo / running / done / failed / skipped —— 小说 T1–T17、初始化 I1–I16、分镜 V1–V8、连续性 C1–C12 通用。"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
            // ⚠️ 共享 Kv 的 rows 键名是 `{key, value}`（不是旧私有件的 `[k, v]` 数组对，
            //    Kv.qml:52），且**键列宽由组件自己测**（TextMetrics + 延后重算）——
            //    页面不再回填 keyW，也不再挂 ThemeBridge 的 Connections 监听字体族变化。
            // ⚠️ 行高从旧私有件的 14 变成设计稿档的 20（12.5 × 1.6，Kv.qml:23-24），
            //    三行高 = 3×20 + 2×6 = 72，所以下面这张卡的 ch 从 144 抬到 162。
            Kv {
                x: 0; y: 90
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
    // 6 张 92×60 的 .gal-row。⚠️ 6×92 + 5×10 = 602px，**装不进**卡片内容区
    //   （~330–420px）—— 设计稿靠 `.gal-row { flex-wrap: wrap }`（views.css:405）
    //   换行，所以这里用 GalleryFlow：卡片宽度放 4 张，第二行 2 张。
    //   行高 60 + gap 10 + 60 = 130，再加说明行 marginTop 10 + 14 → ch = 154。
    Component {
        id: cArt
        Item {
            GalleryFlow {
                x: 0; y: 0
                width: parent.width
                // ⚠️ 共享 Art 的 `seed` 槽位与旧私有件同名同义；调色板统一成设计稿的
                //    12 组（Art.qml:45-53，旧私有件只有 6 组），几何/绘制都走共享件那份。
                //    设计稿这里写了 className="hoverable"（Gallery.jsx:151），但
                //    `.art.hoverable` 在 CSS 里**没有规则**（ui.css:196 只定义了
                //    `.card.hoverable`，1.07 的缩放挂在 `.asset-card:hover .thumb svg`），
                //    所以本页不开 zoom/zoomed。
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
                x: 0; y: 140
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
        Item {
            GalleryArtInk {
                x: 0; y: 0
                width: parent.width
                height: 150
                seed: 2
            }
            Text {
                x: 0; y: 160
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
        Item {
            GalleryEmpty {
                x: 0; y: 0
                width: parent.width
                icon: "folder"
                title: "还没有项目"
                text: "新建一个项目，开始你的第一部作品"
            }
            GalleryFlow {
                x: 0; y: 194
                width: parent.width
                // Toast 四色（ui.css 的 .toast-ok/warn/err/info）在本仓是 Widgets 侧的
                // QML toast，QML 页只摆四个按钮示意；variant 沿用旧页的
                // false/true → secondary/primary 映射，语义不表示四色。
                Button { text: "Info"; variant: "secondary" }
                Button { text: "Success"; variant: "primary" }
                Button { text: "Warning"; variant: "secondary" }
                Button { text: "Error"; variant: "secondary" }
            }
            Text {
                x: 0; y: 230
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
