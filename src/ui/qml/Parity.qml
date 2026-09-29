// src/ui/qml/Gallery/Gallery.qml —— QML 端到端链路的验证页
//
// 这个页面存在的目的不是「产品功能」，而是**一次性验证架构层三件事**：
//   1. 主题桥：所有色值/圆角/动效/字体是否真的来自既有 ColorToken
//   2. 混色一致性：QML 的 Tag 与 Widgets 的 Tag 是否同色
//   3. 场景图抓图：harness 能否拿到非全黑图（本机无桌面会话）
//
// 取证方式与 Widgets 侧完全一致（同一套 review harness），
// 出图后在 %TEMP% 下与 Widgets 画廊的同名控件并排比对。
//
// ⚠️ 三条定位纪律，都是本文件踩出来的：
//
// 1. 根对象必须是 **Item**（这里用 Rectangle），不能是 Window。
//    QQuickWidget 要求 setSource 的根是能放进 contentItem 的 Item，
//    给 Window 只会得到 `QQuickWidget: invalid root object.`。
//    尺寸由宿主给：QQuickWidget::SizeRootObjectToView + QuickHost::FillWithRoot。
//
// 2. 页面这一层**不套 Flow/Row/Grid**。定位器接管子项几何，会和
//    hover 的 translateY(-2px) 抢 y（binding loop，卡片全叠在第一行）。
//    卡片一律显式 x/y，同时让截图与设计稿逐像素可比。
//
// 3. 每张 Card 内部**只放一个填满的 Item**，再在它里面绝对定位。
//    Card 的 body 是 Column（真实卡片要自动竖排），它会接管直接子项的 y ——
//    给直接子项写 y 一样会 binding loop。套一层 Item 就两清了。
import QtQuick
import QtQuick.Effects  // MultiEffect（hover 抬升的 box-shadow）
import QtQuick.Shapes    // Shape（圆角画质对照，见 CornerProbe）
import Shine 1.0

Rectangle {
    id: win

    color: ThemeBridge.colors["bg.void"]

    // —— 布局常量全部取自桥，不在 QML 里另立一套 ——
    readonly property int pad: ThemeBridge.spaces["4"]        // 16
    readonly property int cardW: 340
    readonly property int colGap: ThemeBridge.spaces["4"]     // 16
    readonly property int rowGap: ThemeBridge.spaces["4"]
    readonly property int cardH: 230
    readonly property int col2: cardW + colGap
    readonly property real gridTop: header.height + pad

    // —— 自绘标题栏（不用 QML 窗口装饰，避免各平台差异干扰截图比对）——
    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 56
        color: ThemeBridge.colors["bg.surface"]
        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1
            color: ThemeBridge.colors["line.subtle"]
        }
        Text {
            anchors { left: parent.left; leftMargin: win.pad; verticalCenter: parent.verticalCenter }
            text: "QML 画廊 · " + ThemeBridge.themeName
            color: ThemeBridge.colors["text.primary"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: ThemeBridge.baseFontPx + 3   // 16
            font.weight: Font.DemiBold
        }
        Text {
            anchors { right: parent.right; rightMargin: win.pad; verticalCenter: parent.verticalCenter }
            text: "架构层验证 · 主题桥 / color-mix / 场景图抓图"
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
        }
    }

    // —— Tag 七态：验证 color-mix 派生态与 QSS 同色 ——
    Card {
        x: win.pad
        y: win.gridTop
        width: win.cardW
        height: win.cardH
        Item {
            width: parent.width
            height: parent.height
            Text {
                x: 0; y: 0
                text: "Tag 七态（color-mix 12% 底 / 35% 边）"
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Row {
                x: 0; y: 28
                spacing: 6
                Tag { text: "默认"; tone: "" }
                Tag { text: "强调"; tone: "accent" }
                Tag { text: "信息"; tone: "info" }
            }
            Row {
                x: 0; y: 60
                spacing: 6
                Tag { text: "成功"; tone: "ok" }
                Tag { text: "警告"; tone: "warn" }
                Tag { text: "失败"; tone: "danger" }
            }
            Row {
                x: 0; y: 92
                spacing: 6
                Tag { text: "忙碌"; tone: "busy" }
                Tag { text: "排队"; tone: "pending" }
                Tag { text: "闲置"; tone: "idle" }
            }
            Row {
                x: 0; y: 124
                spacing: 6
                Tag { text: "已审核"; tone: "ok" }
                Tag { text: "待审核"; tone: "warn" }
                Tag { text: "已驳回"; tone: "danger" }
                Tag { text: "生成中"; tone: "busy" }
            }
        }
    }

    // —— Button：验证 :active scale(0.97) 与 :disabled opacity ——
    Card {
        x: win.pad + win.col2
        y: win.gridTop
        width: win.cardW
        height: win.cardH
        Item {
            width: parent.width
            height: parent.height
            Text {
                x: 0; y: 0
                text: "Button（:active scale 0.97）"
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Row {
                x: 0; y: 28
                spacing: 8
                Button { text: "主要"; primary: true }
                Button { text: "次要"; primary: false }
                Button { text: "禁用"; primary: true; enabled: false }
            }
            Row {
                x: 0; y: 70
                spacing: 8
                Button { text: "生成"; primary: true }
                Button { text: "重试"; primary: false }
                Button { text: "禁用次要"; primary: false; enabled: false }
            }
            Text {
                x: 0; y: 116
                text: "禁用态 opacity 0.45（设计稿 .btn:disabled）"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
        }
    }

    // —— 动画：验证 keyframes 语义在 QML 里是一行 ——
    Card {
        x: win.pad
        y: win.gridTop + win.cardH + win.rowGap
        width: win.cardW
        height: win.cardH
        Item {
            width: parent.width
            height: parent.height
            Text {
                x: 0; y: 0
                text: "动画（float-y，QML 原生）"
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            // 对照 base.css:161-164 的 @keyframes float-y：
            // 0%/100% translateY(0)、50% translateY(-6px)，4s 一轮
            Rectangle {
                x: 40; y: 44
                width: 40; height: 40; radius: 20
                color: ThemeBridge.colors["accent.primary"]
                SequentialAnimation on y {
                    running: !ThemeBridge.reduceMotion
                    loops: Animation.Infinite
                    NumberAnimation { from: 44; to: 38; duration: 2000; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 38; to: 44; duration: 2000; easing.type: Easing.InOutQuad }
                }
            }
            Rectangle {
                x: 96; y: 63; width: 200; height: 2
                color: ThemeBridge.colors["line.subtle"]
            }
        }
    }

    // —— token 直读：验证 ThemeBridge.token 与 QSS %N 同源 ——
    Card {
        x: win.pad + win.col2
        y: win.gridTop + win.cardH + win.rowGap
        width: win.cardW
        height: win.cardH
        Item {
            width: parent.width
            height: parent.height
            Text {
                x: 0; y: 0
                text: "色板（直接取自 ColorToken）"
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Row {
                x: 0; y: 28
                spacing: 6
                Swatch { tokenName: "bg.surface" }
                Swatch { tokenName: "bg.panel" }
                Swatch { tokenName: "bg.elevated" }
                Swatch { tokenName: "bg.overlay" }
                Swatch { tokenName: "line.normal" }
                Swatch { tokenName: "text.primary" }
                Swatch { tokenName: "text.muted" }
                Swatch { tokenName: "accent.primary" }
                Swatch { tokenName: "accent.info" }
            }
            Row {
                x: 0; y: 66
                spacing: 6
                Swatch { tokenName: "status.ok" }
                Swatch { tokenName: "status.warn" }
                Swatch { tokenName: "status.danger" }
                Swatch { tokenName: "status.busy" }
                Swatch { tokenName: "status.pending" }
                Swatch { tokenName: "fill.hover" }
                Swatch { tokenName: "fill.selected" }
                Swatch { tokenName: "fill.muted" }
                Swatch { tokenName: "line.focus" }
            }
        }
    }

    // —— hover translateY(-2px)：只有不在定位器里才做得到（见文件头纪律 2）——
    //
    // ⚠️ 位移必须挂在**外层 Item** 上，不能挂在 anchors.fill 的 Rectangle 上：
    // anchors 会接管 y，绑 y 会和锚点打架（实测整块直接不显示）。
    component HoverLift: Item {
        id: liftRoot
        property string caption: ""
        width: win.cardW
        height: 150
        Item {
            width: parent.width
            height: parent.height
            y: area.containsMouse && !ThemeBridge.reduceMotion ? -2 : 0
            Behavior on y {
                NumberAnimation { duration: ThemeBridge.durations.base; easing.type: Easing.OutCubic }
            }
            Rectangle {
                anchors.fill: parent
                radius: ThemeBridge.radii.md
                color: ThemeBridge.colors["bg.panel"]
                border.width: 1
                border.color: ThemeBridge.colors["line.subtle"]
                layer.enabled: ThemeBridge.layerEffectsAvailable
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowBlur: 1.0
                    shadowScale: 1.0
                }
                Text {
                    x: 16; y: 16
                    text: liftRoot.caption
                    color: ThemeBridge.colors["text.primary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
            }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
    }

    // 不用 Repeater + modelData：色板就 18 个色，静态列出更直白，
    // 也不为它引一层委托作用域（委托里的 token(tokenName) 调用要额外处理
    // modelData 是「可变对象」而非 QString 的类型问题）。
    //
    // ⚠️ 尺寸是算出来的，不是随手填的：卡片内容宽 = 340 - 16*2 = 308，
    // 一行 9 个 → 9*28 + 8*6 = 300 < 308。填 30 会到 318，右侧被卡片切掉。
    component Swatch: Rectangle {
        width: 28; height: 28
        radius: ThemeBridge.radii.sm
        border.width: 1
        border.color: ThemeBridge.colors["line.normal"]
        property string tokenName: ""
        color: ThemeBridge.colors[tokenName]
        // 纯 Rectangle 不是 Ctl 派生，拿不到基类的 antialiasing（该属性不向子节点传）
        antialiasing: true
    }

    // —— 圆角画质对照：Rectangle（细分多边形） vs Shape（路径+抗锯齿）——
    // 同尺寸同半径同色并排放，3× 放大看差异。
    component CornerProbe: Item {
        id: probe
        width: 28; height: 28
        property color fill: ThemeBridge.colors["accent.primary"]
        property color stroke: ThemeBridge.colors["line.normal"]
        property real r: ThemeBridge.radii.sm
        // 左：Rectangle 圆角
        Rectangle {
            x: 0; y: 0; width: 28; height: 28
            radius: probe.r
            color: probe.fill
            border.width: 1
            border.color: probe.stroke
            antialiasing: true
        }
        // 右：Shape 圆角（同一半径）。⚠️ PathQuad 的属性是 x/y（终点）
        // + controlX/controlY（控制点），**没有** x2/y2（那是 PathArc 的）。
        Shape {
            x: 34; y: 0; width: 28; height: 28
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: 1
                strokeColor: probe.stroke
                fillColor: probe.fill
                startX: 0; startY: probe.r
                PathLine { x: 0; y: 14 }
                PathQuad { x: probe.r; y: 28; controlX: 0; controlY: 28 }
                PathLine { x: 28 - probe.r; y: 28 }
                PathQuad { x: 28; y: 28 - probe.r; controlX: 28; controlY: 28 }
                PathLine { x: 28; y: probe.r }
                PathQuad { x: 28 - probe.r; y: 0; controlX: 28; controlY: 0 }
                PathLine { x: probe.r; y: 0 }
                PathQuad { x: 0; y: probe.r; controlX: 0; controlY: 0 }
            }
        }
    }

    HoverLift {
        x: win.pad
        y: win.gridTop + (win.cardH + win.rowGap) * 2
        caption: "hover 抬升（translateY -2px + box-shadow）"
    }

    // 圆角画质对照（左 Rectangle / 右 Shape），放在右下空白处
    Item {
        x: win.pad + win.col2
        y: win.gridTop + (win.cardH + win.rowGap) * 2
        width: 340
        height: 150
        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: ThemeBridge.spaces["2"]
            Text {
                text: "圆角画质对照（左 Rectangle / 右 Shape）"
                color: ThemeBridge.colors["text.primary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Row {
                spacing: 0
                CornerProbe {}
                Item { width: 30; height: 1 }
                CornerProbe { r: ThemeBridge.radii.md }
                Item { width: 30; height: 1 }
                CornerProbe { r: ThemeBridge.radii.lg }
            }
            Text {
                text: "r-sm 6 / r-md 10 / r-lg 14"
                color: ThemeBridge.colors["text.muted"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
            }
        }
    }

    Component.onCompleted: console.log(
        "[qml] Gallery ready theme=" + ThemeBridge.themeName
        + " bg.surface=" + ThemeBridge.colors["bg.surface"]
        + " r.md=" + ThemeBridge.radii.md
        + " font=" + ThemeBridge.fontFamily
        + " reduceMotion=" + ThemeBridge.reduceMotion)
}
