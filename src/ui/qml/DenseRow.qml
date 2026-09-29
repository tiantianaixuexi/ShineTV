// src/ui/qml/DenseRow.qml —— 浮动面板里的密集行（webui views.css:294-320 的
// .dlist > .drow：p8 2 + 底部发丝线 + hover fill.hover，无卡片框）
//
// 为什么独立成组件：出片页的三个页签都要它 —— 首尾帧链的链段行、视频任务的
// 任务行、成片的镜头视频行。迁移前这三处各写一份 QSS + 一份 QHBoxLayout，
// 三份的 hover / 分隔线 / 内距已经开始漂（行高与 gap 靠人工对齐）。
//
// 纪律：
//   * 色值只走 ThemeBridge（qml-kit 纪律 2）。hover 底用 fill.hover。
//   * 最后一行由调用方置 last=true —— CSS 的 border-bottom 是逐行画的，
//     最后一行的线要么不画，要么画在容器底上，两种都能接受，但要**显式选一个**。
//   * ⚠️ 子项一律用**显式 x / width**，不要往里塞 Row：本页三行里有一行带
//     「随剩余宽度伸展」的进度条（设计稿 .grow），而 Row 不做 flex 分配 ——
//     塞进去只能手算总宽，而手算的那份会随字号/语言漂。显式坐标是这个仓
//     已有的做法（见 Kv.qml：「显式坐标，不套 Column/Grid」）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // 行内容直接写进本组件（default property 别名到内部容器）。
    default property alias content: bodyItem.data

    property bool last: false          // 末行不画分隔线
    // 供子项量宽度：行内容区宽度（扣掉左右各 2px 内距）。
    // 子项要 elide 就必须显式给宽度 —— 忘了就是文字溢出。
    readonly property real bodyW: Math.max(0, width - 4)

    implicitHeight: Math.max(32, bodyItem.childrenRect.height + 16)  // 至少 8+16+8
    height: implicitHeight
    color: "transparent"

    // hover 底：.drow 的 transition background-color
    Rectangle {
        anchors.fill: parent
        color: ThemeBridge.colors["fill.hover"]
        opacity: hover.containsMouse ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: ThemeBridge.reduceMotion ? 0 : ThemeBridge.durations.fast
                easing.type: Easing.OutCubic
            }
        }
    }

    Item {
        id: bodyItem
        x: 2                              // .drow padding 左右 2px
        width: root.bodyW
        height: parent.height
    }

    // 底部发丝线（1px --line-subtle）
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: ThemeBridge.colors["line.subtle"]
        visible: !root.last
    }

    // 只吃 hover 事件、不吃点击：Qt.NoButton 让点击穿透到子项
    // （迁移前这三行的提示/按钮都在行内，吞掉点击就点不动了）。
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
