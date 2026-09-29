// src/ui/qml/GalleryToggle.qml —— 开关 / 复选框（ui.css:360-427）
//
// `check: false` → .switch：34×19 / r-pill / bg-elevated + line-normal 边，
//   滑块 13px 圆点在 top2 left2，开态平移 15px、底色走 accent、滑块走 accent-fg。
// `check: true`  → .check：文字 f12.5 text-secondary，前置 15px 方框
//   （r-xs / 1.5px line-strong 边），开态方框转 accent 底、勾显形。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property bool on: false
    property bool check: false
    property string text: ""

    implicitWidth: check ? box.width + 8 + textLabel.implicitWidth : 34
    implicitHeight: check ? 17 : 19

    color: "transparent"

    // —— 复选框分支 ——
    Rectangle {
        id: box
        visible: root.check
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        width: 15; height: 15
        radius: ThemeBridge.radii.xs
        color: root.on ? ThemeBridge.colors["accent.primary"] : "transparent"
        border.width: 1.5
        border.color: root.on ? ThemeBridge.colors["accent.primary"] : ThemeBridge.colors["line.strong"]
        Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durFast } }

        // 勾：M4.5 12.5 10 18 19.5 6.5，stroke 3.4 round（UI.jsx:137）
        Item {
            anchors.fill: parent
            opacity: root.on ? 1 : 0
            scale: root.on ? 1 : 0.5
            Behavior on opacity { NumberAnimation { duration: root.reduce ? 0 : root.durFast } }
            Behavior on scale { NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic } }

            Rectangle {
                x: 2.2; y: 6.0
                width: 3.4; height: 3.4
                color: ThemeBridge.colors["accent.primary.fg"]
                rotation: 45
            }
            Rectangle {
                x: 5.4; y: 7.2
                width: 7.0; height: 3.4
                color: ThemeBridge.colors["accent.primary.fg"]
                rotation: -45
            }
        }
    }

    Text {
        id: textLabel
        visible: root.check
        x: box.width + 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: root.on ? ThemeBridge.colors["text.primary"] : ThemeBridge.colors["text.secondary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12          // 12.5px → 就近取整
    }

    // —— 开关分支 ——
    Rectangle {
        visible: !root.check
        anchors.fill: parent
        radius: height / 2
        color: root.on ? ThemeBridge.colors["accent.primary"] : ThemeBridge.colors["bg.elevated"]
        border.width: 1
        border.color: root.on ? ThemeBridge.colors["accent.primary"] : ThemeBridge.colors["line.normal"]
        Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }

        Rectangle {
            x: root.on ? 15 : 0
            y: 0
            width: 13; height: 13
            radius: 6.5
            color: root.on ? ThemeBridge.colors["accent.primary.fg"] : ThemeBridge.colors["text.secondary"]
            Behavior on x { NumberAnimation { duration: root.reduce ? 0 : root.durBase; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }
        }
    }

    TapHandler { onTapped: root.on = !root.on }
}
