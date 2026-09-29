pragma ComponentBehavior: Bound
// src/ui/qml/AssetsPolicyPanel.qml —— 页面私有：依赖等待与降级策略（QML 迁移）
//
// 数据源：C++ 注入的 `Page`（AssetPageModel::policy，取值见 ProjectPolicy）：
//   {allowDegrade, strict, timeoutMinutes, timeoutMs, summary, phase,
//    layer, detail, active, degraded}
// 这两个开关**真的进管线**（不是页面上的装饰）：setAllowDegrade /
// setSuspendMinutes 直接改 AssetPolicy，changed() 再回灌本面板。
//
// ⚠️ 开关为什么不是「一句 `on: Page.policy.allowDegrade`」：
//   Toggle 内部 TapHandler 直接写自己的 `on`（Toggle.qml:90）。绑上去以后
//   **第一次点击就把绑定打断**，之后 C++ 改了它也不跟 —— 而这两个开关
//   恰恰是「页面显示必须是真值」的那两个，不容漂移。
//   → 本地值当**真值**、点完立刻推给 C++，再由 Page.changed 回灌纠偏
//     （见 onOnChanged 与 Connections；两处都带相等判断，不会自激）。
//   顺带是 ThemeBridge.h:18-25 那条纪律的又一个实例：**方法调用在 QML 绑定里
//   不建依赖**，所以「回灌」只能挂 Connections(onChanged)，不能靠绑定表达式。
//
// 文案沿用迁移前那一版（允许超期降级（关闭 = 严格模式）/
// 挂起超时（分钟）/ 0 = 立即进入降级判定…），两个视图的验收按文案取值。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 真值（空态也带全键，见 AssetPageModel.cpp:ProjectPolicy）——
    readonly property var policy: Page.policy

    // —— 本地真值（见头注「开关为什么不是绑定」）——
    property bool allowDegrade: false
    property int minutes: 30

    readonly property int spXs: ThemeBridge.spaces["1"]   // 4
    readonly property int spSm: ThemeBridge.spaces["2"]   // 8
    readonly property int spMd: ThemeBridge.spaces["3"]   // 12

    // —— 盒模型（行高/数字框宽度是本页自己的排版选择，不是设计稿的）——
    readonly property int summaryH: 18
    readonly property int rowH: 30           // Toggle 19 / 数字框 30，取齐
    readonly property int inputH: 30         // 与 Input.qml 同档（ui.css:288 h30）
    readonly property int inputW: 64
    readonly property int runtimeH: 18
    readonly property int auditH: 32         // 两行 .tiny 说明

    readonly property real summaryY: 0
    readonly property real toggleRowY: summaryY + summaryH + spSm
    readonly property real timeoutRowY: toggleRowY + rowH + spSm
    readonly property real runtimeY: timeoutRowY + rowH + spSm
    readonly property real auditY: runtimeY + runtimeH + spSm

    implicitHeight: auditY + auditH
    implicitWidth: 720
    color: "transparent"

    // —— 数字域的三个入口（手输 / 两个步进钮）———
    function typedMinutes() {
        var v = parseInt(minutesInput.text, 10)
        // 空串或非数字**不写库**：回落到真值，否则一次误清空就会把超时设成 0
        return isNaN(v) ? root.minutes : v
    }
    function setMinutes(v) {
        var clamped = Math.max(0, Math.min(120, Math.round(v)))
        root.minutes = clamped
        minutesInput.text = "" + clamped
        Page.setSuspendMinutes(clamped)
    }
    function stepMinutes(delta) { root.setMinutes(root.typedMinutes() + delta) }

    Connections {
        target: Page
        // C++ 是真值：任何一侧被别处改动，这里都纠回来。
        // ⚠️ 数字框**有焦点时**不回灌 —— 否则用户正打着「12」就被改写成
        // 库里那个 30（回车前只剩一次机会）。
        function onChanged() {
            if (Page.policy.allowDegrade !== root.allowDegrade) {
                root.allowDegrade = Page.policy.allowDegrade
                if (degradeToggle.on !== root.allowDegrade) {
                    degradeToggle.on = root.allowDegrade
                }
            }
            if (Page.policy.timeoutMinutes !== root.minutes && !minutesInput.activeFocus) {
                root.minutes = Page.policy.timeoutMinutes
                minutesInput.text = "" + root.minutes
            }
        }
    }

    Component.onCompleted: {
        root.allowDegrade = Page.policy.allowDegrade
        degradeToggle.on = root.allowDegrade
        root.minutes = Page.policy.timeoutMinutes
        minutesInput.text = "" + root.minutes
    }

    // —— 摘要：C++ 拼好的一句话（两档策略各自的判词）——
    Text {
        x: 0
        y: root.summaryY
        width: root.width
        height: root.summaryH
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.policy.summary
        color: ThemeBridge.colors["text.secondary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }

    // —— 允许超期降级 ——
    Item {
        x: 0
        y: root.toggleRowY
        width: root.width
        height: root.rowH

        Text {
            id: allowLabel
            x: 0
            y: 0
            width: Math.max(0, parent.width - degradeToggle.width - root.spMd - policyTag.width - root.spSm)
            height: root.rowH
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: "允许超期降级（关闭 = 严格模式）"
            color: ThemeBridge.colors["text.primary"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: ThemeBridge.baseFontPx
        }
        Tag {
            id: policyTag
            x: allowLabel.width + root.spSm
            y: (root.rowH - height) / 2
            text: root.allowDegrade ? "允许降级" : "严格模式"
            tone: root.allowDegrade ? "warn" : "ok"
            sm: true
        }
        Toggle {
            id: degradeToggle
            x: parent.width - width
            y: (root.rowH - height) / 2
            on: root.allowDegrade
            onOnChanged: {
                // 相等 = C++ 回灌写回来的，不是用户点的（见 Connections）
                if (degradeToggle.on === root.allowDegrade) { return }
                root.allowDegrade = degradeToggle.on
                Page.setAllowDegrade(root.allowDegrade)
            }
        }
    }

    // —— 挂起超时（分钟）——
    Item {
        x: 0
        y: root.timeoutRowY
        width: root.width
        height: root.rowH

        Text {
            id: timeoutLabel
            x: 0
            y: 0
            width: Math.max(0, parent.width - root.stepperW)
            height: 16
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: "挂起超时（分钟）"
            color: ThemeBridge.colors["text.primary"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: ThemeBridge.baseFontPx
        }
        Text {                      // help（.tiny.dim，与 Field.qml:46-56 同一档）
            x: 0
            y: timeoutLabel.height + 2
            width: Math.max(0, parent.width - root.stepperW)
            height: 14
            elide: Text.ElideRight
            text: "0 = 立即进入降级判定；严格模式仍不会降级"
            color: ThemeBridge.colors["text.muted"]
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
        }

        // 数字框：视觉档同 Input.qml（ui.css:288-299）+ focus 光圈。
        // 数值输入自绘 TextInput 而不是套 Input —— Input 的 text 是单向绑定
        // （Input.qml:32），敲进去的值回不到组件上，提交时只会读到旧值。
        Rectangle {
            id: minutesBox
            x: parent.width - root.stepperW
            y: 0
            width: root.inputW
            height: root.inputH
            radius: root.rSm
            color: minutesInput.activeFocus ? ThemeBridge.colors["bg.surface"]
                                            : ThemeBridge.colors["fill.muted"]
            border.width: 1
            border.color: minutesInput.activeFocus ? ThemeBridge.colors["line.focus"]
                          : minutesHover.containsMouse ? ThemeBridge.colors["line.strong"]
                                                      : ThemeBridge.colors["line.normal"]
            Behavior on border.color {
                ColorAnimation { duration: root.reduce ? 0 : root.durFast }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: parent.radius + 3
                color: "transparent"
                border.width: 3
                border.color: ThemeBridge.toneBg["accent"]
                visible: minutesInput.activeFocus
            }
            TextInput {
                id: minutesInput
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: TextInput.AlignVCenter
                horizontalAlignment: TextInput.AlignRight
                selectByMouse: true
                clip: true
                // .mono 12px（StoryboardDialog 的时长框同档）：数字对齐才看得出
                // 「30」有没有被改成「3」
                font.family: "Consolas"
                font.pixelSize: 12
                color: ThemeBridge.colors["text.primary"]
                selectionColor: ThemeBridge.colors["fill.selected"]
                selectedTextColor: ThemeBridge.colors["text.primary"]
                // 0–120 是 C++ setSuspendMinutes 自己的钳制范围，输入层先挡住
                validator: IntValidator { bottom: 0; top: 120 }
                onEditingFinished: root.setMinutes(root.typedMinutes())
            }
            MouseArea {
                id: minutesHover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
        }

        // 步进钮（Widgets 侧 NumberInput 的增减按钮，QML 侧用 .btn.sm 顶）
        Button {
            id: stepDown
            x: minutesBox.x + minutesBox.width + root.spSm
            y: 0
            glyph: "−"
            sm: true
            onClicked: root.stepMinutes(-1)
        }
        Button {
            id: stepUp
            x: stepDown.x + stepDown.width + root.spXs
            y: 0
            glyph: "+"
            sm: true
            onClicked: root.stepMinutes(1)
        }
    }

    // 步进区宽度：数字框 + 8 + 两个 .btn.sm + 4
    readonly property real stepperW: inputW + spSm + stepDown.width + spXs + stepUp.width

    // —— 当前依赖等待的运行态（与迁移前的 Widgets 版同一口径）——
    Text {
        x: 0
        y: root.runtimeY
        width: root.width
        height: root.runtimeH
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: {
            if (!root.policy.active && root.policy.phase === "none") {
                return "当前无依赖等待任务。"
            }
            var prefix = root.policy.degraded ? "已降级"
                       : root.policy.phase === "PENDING" ? "C 挂起" : "处理中"
            return prefix + " · " + root.policy.phase + " · "
                 + (root.policy.detail === "" ? "无附加说明" : root.policy.detail)
        }
        color: root.policy.degraded ? ThemeBridge.colors["status.danger"]
                                    : ThemeBridge.colors["text.secondary"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }

    // —— 审计口径说明（固定两行，别让它把版面顶开）——
    Text {
        x: 0
        y: root.auditY
        width: root.width
        height: root.auditH
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        text: "缺依赖先按 C 挂起，不阻塞其他资产；超期后按 B 降级并写 degradations.jsonl 与 audit_logs。"
        color: ThemeBridge.colors["text.muted"]
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }
}
