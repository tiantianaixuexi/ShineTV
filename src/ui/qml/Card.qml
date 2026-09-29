// src/ui/qml/Gallery/Card.qml —— 对照 webui ui.css:182-200 的 .card / .card-b
//
// hover 的 translateY(-2px) + box-shadow：Widgets 侧是 Tween 移位 +
// QGraphicsDropShadowEffect，QML 侧是一行 y 绑定 + layer.effect。
//
// ⚠️ 本组件**不自己绑 y**。Card 总是被放在 Flow/Row/Grid 这类定位器里，
// 而定位器会接管子项的 x/y —— 两者同时赋 y 会打出 binding loop、布局直接错乱
// （实测：卡片全部叠在第一行）。抬升因此放在 Gallery.qml 的 HoverLift 里，
// 那里的卡片用显式坐标定位，不属于任何定位器。
import QtQuick
import QtQuick.Effects  // MultiEffect（box-shadow）
import Shine 1.0

Ctl {
    id: root

    property bool hoverable: true
    // .card-b 的 padding（ui.css:194）。**0 = 本组件不排版内容**，调用方自己按
    // .card-h / .card-b 两套不同内边距（12 16 / 16）搭结构时把它关掉，
    // 否则会在 Card 的 16 之上再叠一层 16，只能靠负 y 去抵消（那正是魔法数字的来源）。
    property int bodyPad: 16
    default property alias content: body.data

    implicitHeight: body.implicitHeight + bodyPad * 2   // .card-b padding 16 上下各一份
    implicitWidth: 320
    radius: root.rMd                            // --r-md 10
    color: ThemeBridge.colors["bg.panel"]

    border.width: 1
    border.color: hover.containsMouse ? ThemeBridge.colors["line.strong"]
                                      : ThemeBridge.colors["line.subtle"]
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast }
    }

    layer.enabled: root.shadows && hover.containsMouse
    layer.effect: MultiEffect {
        shadowEnabled: root.hoverable && hover.containsMouse && !root.reduce
        shadowBlur: 1.0
        shadowScale: 1.0
    }

    // .card-b { padding: 16px }
    Column {
        id: body
        anchors.fill: parent
        anchors.margins: root.bodyPad
        spacing: ThemeBridge.spaces["3"]   // 12px
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: root.hoverable
        acceptedButtons: Qt.NoButton
    }
}
