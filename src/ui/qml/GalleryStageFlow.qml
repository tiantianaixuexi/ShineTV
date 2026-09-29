// src/ui/qml/GalleryStageFlow.qml —— 阶段流（ui.css:813-893 的 .stageflow）
//
// 节点 h30 / p0 11 / r-pill / 1px line-normal / fill-muted / f12 w600；
// 前置一枚 7px 状态点（run → 转圈 / done → ok / fail → danger / skip / todo → idle），
// 阶段码走等宽 10.5px w700。节点间 18×1.5 连接线，done 时铺满 accent。
//
// .stageflow 是 overflow-x: auto 的横向滚动区 —— 6 个节点约 816px 宽，
// 卡片内容区只有 ~420px，所以这里用横向 Flickable 承载。
//
// ⚠️ 上一版整块不渲染的教训：**定位器（Row）会接管子项的 y**，所以
//    1. delegate 高度必须显式给（上一版是 0，子项再 anchors.verticalCenter
//       就全被顶到 y=0 之上）；
//    2. 定位器内部**一律不用 anchors.verticalCenter**（Row 赋 y 与 anchor 赋 y
//       互相抢，同一类 binding loop）；
//    3. 尺寸全部显式钉死，不靠 implicitWidth 链式推导。
import QtQuick
import Shine 1.0

// 节点在 Repeater delegate 里访问页面级 id（root.xxx）
pragma ComponentBehavior: Bound

Ctl {
    id: root

    // stages: [{ code: "T1", name: "章节初始化" }, ...]
    // stageStates: 与 stages 等长的状态数组（"done"/"run"/"fail"/"skip"/"todo"）
    //   —— 不能叫 `states`：Item 已有内建的 states 属性，重复声明会覆盖基类成员。
    property var stages: []
    property var stageStates: []

    readonly property int nodeH: 30
    readonly property int linkW: 18
    readonly property int stripPad: 4           // .stageflow padding 4

    implicitHeight: nodeH + stripPad * 2
    implicitWidth: 360

    color: "transparent"

    function stAt(i) {
        return stageStates.length > i ? stageStates[i] : "todo"
    }

    Item {
        id: clipBox
        anchors.fill: parent
        anchors.margins: root.stripPad
        clip: true

        Flickable {
            id: strip
            anchors.fill: parent
            contentWidth: flow.implicitWidth
            contentHeight: root.nodeH
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: flow
                width: implicitWidth
                height: root.nodeH
                spacing: 0

                Repeater {
                    model: root.stages
                    delegate: Item {
                        id: cell
                        required property int index
                        required property var modelData

                        readonly property string st: root.stAt(cell.index)
                        width: (cell.index > 0 ? root.linkW : 0) + nodeRow.implicitWidth + 22
                        height: root.nodeH

                        // —— 连接线：done 时 accent 由左向右铺满（CSS scaleX）——
                        Item {
                            x: 0
                            y: (root.nodeH - 1.5) / 2
                            visible: cell.index > 0
                            width: root.linkW
                            height: 1.5
                            Rectangle {
                                anchors.fill: parent
                                color: ThemeBridge.colors["line.normal"]
                            }
                            Rectangle {
                                width: parent.width * (cell.st === "done" ? 1 : 0)
                                height: parent.height
                                color: ThemeBridge.colors["accent.primary"]
                                Behavior on width {
                                    NumberAnimation { duration: root.reduce ? 0 : root.durSlow; easing.type: Easing.OutCubic }
                                }
                            }
                        }

                        Rectangle {
                            id: node
                            x: cell.index > 0 ? root.linkW : 0
                            y: 0
                            width: nodeRow.implicitWidth + 22          // p0 11
                            height: root.nodeH
                            radius: height / 2
                            opacity: cell.st === "skip" ? 0.45 : 1
                            color: cell.st === "done" ? root.mix(ThemeBridge.colors["status.ok"], ThemeBridge.colors["fill.muted"], 0.10)
                                 : cell.st === "fail" ? root.mix(ThemeBridge.colors["status.danger"], ThemeBridge.colors["fill.muted"], 0.10)
                                 : cell.st === "run"  ? Qt.alpha(ThemeBridge.colors["accent.primary"], 0.14)   // --accent-dim
                                 : ThemeBridge.colors["fill.muted"]
                            border.width: 1
                            border.color: cell.st === "done"  ? Qt.alpha(ThemeBridge.colors["status.ok"], 0.40)
                                      : cell.st === "fail"  ? Qt.alpha(ThemeBridge.colors["status.danger"], 0.45)
                                      : cell.st === "run"   ? ThemeBridge.colors["accent.primary"]
                                      : hover.hovered ? ThemeBridge.colors["line.strong"] : ThemeBridge.colors["line.normal"]
                            Behavior on border.color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }

                            Row {
                                id: nodeRow
                                x: 11                                 // p0 11
                                y: 0
                                width: implicitWidth
                                height: root.nodeH
                                spacing: 7

                                // ⚠️ Row 会接管子项的 y，所以每个内容都套一层
                                //    撑满 nodeH 的 Item，垂直居中在 Item 内部做 ——
                                //    直接给 Text/Dot 设 y 或 anchors.verticalCenter
                                //    都会和 Row 抢 y。
                                Item {
                                    width: sp.width
                                    height: root.nodeH
                                    visible: cell.st === "run"
                                    GallerySpinner {
                                        id: sp
                                        y: Math.round((root.nodeH - height) / 2)
                                        small: true
                                    }
                                }
                                Item {
                                    width: dt.width
                                    height: root.nodeH
                                    visible: cell.st !== "run"
                                    GalleryDot {
                                        id: dt
                                        y: Math.round((root.nodeH - height) / 2)
                                        tone: cell.st === "done" ? "ok" : cell.st === "fail" ? "danger" : "idle"
                                    }
                                }
                                Item {
                                    width: cd.implicitWidth
                                    height: root.nodeH
                                    Text {
                                        id: cd
                                        y: Math.round((root.nodeH - height) / 2)
                                        text: cell.modelData.code
                                        color: cell.st === "done" ? ThemeBridge.colors["status.ok"]
                                             : cell.st === "run" ? ThemeBridge.colors["accent.primary"]
                                             : ThemeBridge.colors["text.muted"]
                                        opacity: 0.8
                                        font.family: root.mono
                                        font.pixelSize: 10        // 10.5px → 就近取整
                                        font.weight: Font.Bold
                                    }
                                }
                                Item {
                                    width: nm.implicitWidth
                                    height: root.nodeH
                                    Text {
                                        id: nm
                                        y: Math.round((root.nodeH - height) / 2)
                                        text: cell.modelData.name
                                        color: (cell.st === "done" || cell.st === "run")
                                              ? ThemeBridge.colors["text.primary"] : ThemeBridge.colors["text.muted"]
                                        font.family: ThemeBridge.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }

                            HoverHandler { id: hover }
                        }
                    }
                }
            }
        }
    }

    readonly property string mono: "Cascadia Code, JetBrains Mono, Consolas, monospace"

    // CSS color-mix(in srgb, A p%, B) 的等价换算：结果本身是一个不透明色
    function mix(a, b, t) {
        var x = Qt.rgba(a.r, a.g, a.b, 1)
        var y = Qt.rgba(b.r, b.g, b.b, 1)
        return Qt.rgba(x.r * t + y.r * (1 - t), x.g * t + y.g * (1 - t), x.b * t + y.b * (1 - t), 1)
    }
}
