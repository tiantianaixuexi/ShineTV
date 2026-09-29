pragma ComponentBehavior: Bound   // Repeater delegate 里引用 root，必须显式绑定（pragma 必须在 import 之前）
// src/ui/qml/StageFlow.qml —— 阶段流**一整行**（ui.css:814-821 的 .stageflow）
//
// ================================ 合并来源：两套形状收成两层 ================================
//   GalleryStageFlow.qml（整行 N 节点 + Flickable + Repeater）→ 本文件
//   StoryboardStage.qml  （单节点：.slink + .snode）          → 拆成 StageNode.qml
//   → 「一行」与「一个节点」本来就是两件事（StageFlow.jsx:15-25 每轮渲染一对），
//     本文件只管排布与横向滚动，节点的一切几何/配色都在 StageNode.qml 里。
//   → 差异变成属性：页面的 `stateOf(s, i)` → stageStates[]（下标同 stages）；
//     StageNode 内部由 stageState + hasLink 自己算，不用再传 i。
// =================================================================================================
//
// 设计稿：
//   .stageflow { display:flex; align-items:center; gap:0; overflow-x:auto;   ← ui.css:814-821
//                padding:4px 2px; scrollbar-width:thin }
//   节点本身（.snode / .slink / 五种态 / .code）见 StageNode.qml 的文件头。
//   stAt 越界兜底：stages 与状态数组**同下标**（StageFlow.jsx:11 `stateOf(s, i)`）；
//     设计稿是传函数进来，QML 侧收成状态数组，数组给短了就得有兜底 —— 缺的那一段按 todo 画。
//
// 几何（.stageflow 的 padding 是「4px 2px」= 上下 4 / 左右 2，别读成四边都 4）：
//   行高 = 节点 30 + 上下 padding 4×2 = 38。
//   ⚠️ padding 的 4px 不是装饰：`.snode.run` 的 3px 光环画在节点**外侧** -3…+3
//      （box-shadow: 0 0 0 3px 不占布局），上下各留 4 正好让它不被裁掉。
//   `.stageflow { gap: 0 }` —— 节点间距全靠节点自带的 .slink（18px），Row 的 spacing 必须是 0。
//
// ⚠️ 为什么套 Flickable：`.stageflow` 是 overflow-x: auto 的横向滚动区，6 个节点约 816px 宽
//    （6 × (11+7+7+码宽+7+名宽+11+18)），而卡片内容区只有 ~420px。QML 没有原生 overflow，
//    用横向 Flickable 承载。
//    ⚠️ 已知缺口：设计稿有 `scrollbar-width: thin`（一条细滚动条），QML 侧没画 —
//      GalleryStageFlow 也没画。滚动可用但看不到滚动条，属设计稿之外的已知缺口。
//
// ⚠️ 定位器（Row）会接管子项的 y —— 上一版整块不渲染的教训，三条都保留：
//    1. delegate 的高度必须显式给（上一版是 0，子项再一居中就全被顶到 y=0 之上）；
//    2. 定位器内部**一律不用 anchors.verticalCenter**（Row 赋 y 与 anchor 赋 y 互相抢，
//       同一类 binding loop）。StageNode 内部全部用显式 y = (nodeH - h) / 2，没有一个 anchor；
//    3. delegate 宽度显式钉在 node.implicitWidth 上（不靠 implicitWidth 链式推导到 Row 上）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 调用方 API ——
    // stages: [{ code: "T1", name: "章节初始化" }, …]
    property var stages: []
    // stageStates: 与 stages **同下标**的状态数组（"todo"/"run"/"done"/"fail"/"skip"）
    // ⚠️ 不能叫 `states`：Item 已有内建的 states 属性，重复声明会覆盖基类成员（QML 静默吞赋值）。
    property var stageStates: []
    // 节点被点：转发下标。Repeater 的 delegate 对调用方是匿名的，不带下标没法知道是哪一格。
    signal picked(int index)

    readonly property real nodeH: 30      // 与 StageNode.nodeH 一致（.snode { height: 30px }）
    readonly property real padY: 4         // .stageflow { padding: 4px 2px }
    readonly property real padX: 2

    implicitHeight: nodeH + padY * 2      // 30 + 4×2 = 38
    implicitWidth: 360                     // 设计稿没给内在宽度（是 100% 宽的滚动区），只作兜底
    color: "transparent"

    // 越界兜底：状态数组比 stages 短时，缺的那一段按 todo 画
    function stAt(i) {
        return stageStates.length > i ? stageStates[i] : "todo"
    }

    Item {
        id: clipBox
        anchors.fill: parent
        anchors.topMargin: root.padY
        anchors.bottomMargin: root.padY
        anchors.leftMargin: root.padX
        anchors.rightMargin: root.padX
        // ⚠️ 这里必须 clip：.snode.run 的 3px 光环在 padding 内，不 clip 会漏到卡片外面
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
                height: root.nodeH
                spacing: 0                          // .stageflow { gap: 0 } —— 间距全靠 .slink

                Repeater {
                    model: root.stages
                    delegate: Item {
                        id: cell
                        required property int index
                        required property var modelData

                        // ① 高度显式；② 宽度显式钉在节点实测宽上（见文件头三条纪律）
                        width: node.implicitWidth
                        height: root.nodeH

                        StageNode {
                            id: node
                            x: 0
                            y: 0
                            code: cell.modelData.code
                            stageName: cell.modelData.name
                            stageState: root.stAt(cell.index)
                            // i > 0 才前置一根 .slink（StageFlow.jsx:16）
                            hasLink: cell.index > 0
                            onPicked: root.picked(cell.index)
                        }
                    }
                }
            }
        }
    }
}
