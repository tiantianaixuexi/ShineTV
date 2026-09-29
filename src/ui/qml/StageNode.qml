pragma ComponentBehavior: Bound   // 子项/层级里要用外层 root.id，显式绑定（pragma 必须在 import 之前）
// src/ui/qml/StageNode.qml —— 阶段流里的**一个**节点（ui.css:822-874 的 .snode + 875-893 的 .slink）
//
// ================================ 合并来源：整行版拆出单节点版 ================================
//   GalleryStageFlow.qml（整行 N 节点）→ 节点本体 + 连接线，来自 ui.css:822-893
//   StoryboardStage.qml  （单节点版）    → **本文件的骨架来源**：显式坐标、无 Row
//   → 差异变成属性：stAt(i)/index>0 判定 → hasLink + stageState；callers 的 stageStates → stageState。
//   本文件内部**不再手搓**子控件：转圈用共享 `Spinner { sm: true }`（StoryboardStage 已经用了，
//   它那份是对的圆弧），状态点用共享 `Dot { tone: … }`（GalleryStageFlow 原来指向 GalleryDot，
//   共享化之后就是 Dot）。⚠️ 依赖两个同目录共享件：Spinner.qml / Dot.qml。
// =================================================================================================
//
// 设计稿（StageFlow.jsx:14-25 的渲染顺序：i>0 时先 .slink 再 .snode，成对出现）：
//   .stageflow .snode { display:flex; align-items:center; gap:7px; height:30px;     ← ui.css:822-838
//                        padding:0 11px; border-radius:var(--r-pill);
//                        border:1px solid var(--line-normal);
//                        background:var(--fill-muted);
//                        font-size:12px; font-weight:600; color:var(--text-muted);
//                        white-space:nowrap; flex:none; cursor:pointer;
//                        transition: border-color var(--dur-2) var(--ease),
//                                    color var(--dur-2) var(--ease),
//                                    box-shadow var(--dur-2) var(--ease),
//                                    background-color var(--dur-2) var(--ease) }
//   .stageflow .snode:hover { border-color:var(--line-strong); color:var(--text-primary) } ← 839-842
//   .stageflow .snode .code { font-family:var(--font-mono); font-size:10.5px;          ← 843-848
//                             font-weight:700; opacity:.8 }
//   .stageflow .snode.done { color:var(--text-primary);                                 ← 849-856
//                            border-color:color-mix(in srgb, var(--ok) 40%, transparent);
//                            background:color-mix(in srgb, var(--ok) 10%, var(--fill-muted)) }
//                            .done .code { color:var(--ok) }
//   .stageflow .snode.run  { color:var(--text-primary); border-color:var(--accent);       ← 857-865
//                            background:var(--accent-dim);
//                            box-shadow:0 0 0 3px var(--accent-dim), var(--shadow-accent) }
//                            .run .code { color:var(--accent) }
//   .stageflow .snode.fail { color:var(--danger);                                       ← 866-870
//                            border-color:color-mix(in srgb, var(--danger) 45%, transparent);
//                            background:color-mix(in srgb, var(--danger) 10%, var(--fill-muted)) }
//   .stageflow .snode.skip { opacity:.45; border-style:dashed }                          ← 871-874
//   .stageflow .slink     { flex:none; width:18px; height:1.5px;                         ← 875-881
//                           background:var(--line-normal); position:relative }
//   .stageflow .slink::after { inset:0; background:var(--accent); transform:scaleX(0);   ← 882-890
//                           transform-origin:left; transition:transform var(--dur-3) var(--ease) }
//   .stageflow .slink.fill::after { transform:scaleX(1) }                                ← 891-893
//   状态点/转圈   st==='run' ? <span class="spin sm"/> : <StatusDot tone=…>              ← StageFlow.jsx:22
//   .dot          { width:7px; height:7px; border-radius:50% }                           ← ui.css:164-169
//   .spin.sm      { 11px + 1.5px 边 }                                                   ← ui.css:456-470（Spinner.qml）
//   --accent-dim: rgba(<accent>, .14)                                                     ← tokens.css:66/107/148/189/242
//   --shadow-accent: 0 4px 20px rgba(<accent>, .28) = 深空.json:32 的 shadow.accent       ← tokens.css:69
//   --dur-2: 200ms = durations.base   --dur-3: 320ms = durations.slow                   ← tokens.css:25/26
//
// 几何（border-box，1px 边画在盒内不额外占位）：
//   节点宽 = 11(p0 左) + 状态点位 + 7(gap) + 阶段码宽 + 7(gap) + 阶段名宽 + 11(p0 右)
//   ⚠️ 状态点位**恒取 7px**（.dot 的尺寸），即使 run 态换成 11px 的 Spinner 也只向两侧各溢出 2px。
//      这样四种状态的节点宽度完全一致，不会每次状态 tick 就横跳一下 —— StoryboardStage 的做法，
//      保留。
//   ⚠️ 阶段名：StageFlow.jsx:24 是 `name !== code && <span>{name}</span>` —— 相等时**不画**。
//      宽度公式也必须跟着收口，否则会凭空多出一段码宽的空隙。
//   字号 10.5 → 10（就近取整）；节点字号 12 / w600 → Font.DemiBold。
//
// ⚠️ 混色比例怎么落地（两份旧实现不一致，按 CSS 的字面比例取）：
//   CSS 的 `color-mix(in srgb, X N%, transparent)` 与 `color-mix(in srgb, X N%, var(--fill-muted))`
//   都等价于「X 压 N% 的 alpha 叠在底上」，所以统一用 `Qt.alpha(token, N)`：
//     done 边 .40 / 底 .10（ui.css:851-852）   fail 边 .45 / 底 .10（ui.css:868-869）
//     run  底 .14 = --accent-dim（ui.css:860） 光环边 3px 同样 .14（ui.css:861）
//   StoryboardStage 用的 ThemeBridge.toneEdge（35%，混 bg.surface）/ toneBg（12%）两档都不对：
//   比例差（35≠40、12≠10/14）**且**底色混的是 bg.surface 而不是 fill-muted。
//   本实现因此**不使用** toneBg/toneEdge。⚠️ 纸墨/水墨两套浅色主题的 --accent-dim 是 .10/.08
//   （tokens.css:148/189）而不是 .14，这里统一取主档 .14 —— 出图确认浅色主题下 run 底色是否偏浓。
//
// ⚠️ .skip 的虚线边复刻不了：QML 的 Rectangle 没有 dashed 边（要 QtQuick.Shapes 手画），
//    只落 opacity .45。两份旧实现都这样，保留。
//
// ⚠️ 字体栈：--font-mono（tokens.css:31）是一串带引号的回退族。QML 的 font.family 走
//    QFont::setFamily，逗号串不会被拆成字体栈，等于请求一个字面名相等的族名；沿用
//    GalleryStageFlow 既有写法（至少在本机装了 Cascadia Code / JetBrains Mono 时能命中第一项），
//    比 StoryboardStage 退化成裸 "Consolas" 好。⚠️ 哪一档真正命中要出图确认。
//
// ⚠️ 本组件**不改** stageState：只有 picked 信号，状态归调用方（StageFlow 转发成下标）。
//    组件内自改会出现「谁是真值」的双写。
import QtQuick
import QtQuick.Effects        // MultiEffect（.snode.run 的 --shadow-accent）
import Shine 1.0

Ctl {
    id: root

    // —— 调用方 API ——
    property string code: ""
    property string stageName: ""
    // "todo" | "run" | "done" | "fail" | "skip"（StageFlow.jsx:3 的五种态）
    // ⚠️ 属性名不能叫 state：Item 已有内建 state 成员，重复声明会覆盖基类成员（QML 静默失败）。
    property string stageState: "todo"
    property bool hasLink: false         // i > 0 时前置一根 .slink
    signal picked()                      // StageFlow.jsx:19 的 onClick

    readonly property real nodeH: 30     // .snode { height: 30px }
    readonly property real linkW: root.hasLink ? 18 : 0   // .slink { width: 18px }
    readonly property real dotW: 7        // .dot { width/height: 7px }
    readonly property real gap: 7         // .snode { gap: 7px }
    readonly property real padX: 11       // .snode { padding: 0 11px }
    readonly property string mono: "Cascadia Code, JetBrains Mono, Consolas, monospace"

    // StageFlow.jsx:24 —— 阶段名与阶段码相同就不画（宽度公式也跟着收口）
    readonly property bool showName: root.stageName !== "" && root.stageName !== root.code
    readonly property real nodeW: root.padX * 2 + root.dotW + root.gap + codeText.implicitWidth
                                  + (root.showName ? root.gap + nameText.implicitWidth : 0)

    implicitWidth: root.linkW + root.nodeW
    implicitHeight: root.nodeH
    color: "transparent"                 // 底色在 base 上，根不画

    // —— 状态取色：面色两张表（token + 比例）+ skip / linkFill ——
    // 面色 = state 色压 alpha 叠在 fill.muted 上（CSS 的 color-mix(x N%, fill-muted)）
    readonly property real tintA: root.stageState === "run"  ? 0.14
                                : root.stageState === "done" ? 0.10
                                : root.stageState === "fail" ? 0.10
                                                                : 0
    // ⚠️ tintToken 的 fill.muted 兜底永远用不到（tintA=0 时 node 的色走 "transparent"），
    //    留着只是为了这张表在五个态上都有定义。
    readonly property string tintToken: root.stageState === "run"  ? "accent.primary"
                                    : root.stageState === "done" ? "status.ok"
                                    : root.stageState === "fail" ? "status.danger"
                                                                  : "fill.muted"
    readonly property bool skipA: root.stageState === "skip"
    // StageFlow.jsx:16 —— 连接线 fill 由**当前节点**自己的状态决定（不是前一根）
    readonly property real linkFill: root.hasLink && root.stageState === "done" ? 1 : 0

    // —— .slink —— 高 1.5px 的横线，y 垂直居中 ——
    Rectangle {
        id: link
        x: 0
        y: (root.nodeH - height) / 2
        width: 18                                        // .slink { width: 18px }
        height: 1.5                                      // .slink { height: 1.5px }
        // ⚠️ .slink 在 CSS 里**没有**圆角（ui.css:875-881）。这里按 --r-sm 写，但 Qt 会把半径
        //    钳到自身半高 0.75 —— 与 --r-pill 的观感相同，1.5px 的线上两者肉眼无差。
        radius: Math.min(root.rSm, height / 2)
        color: ThemeBridge.colors["line.normal"]
        visible: root.hasLink
        // .slink::after { transform: scaleX(0→1); transform-origin: left } —— 用宽度代替 scaleX
        Rectangle {
            width: parent.width * root.linkFill
            height: parent.height
            color: ThemeBridge.colors["accent.primary"]
            Behavior on width {   // transition: transform var(--dur-3)
                NumberAnimation { duration: root.reduce ? 0 : root.durSlow; easing.type: Easing.OutCubic }
            }
        }
    }

    // —— .snode.run 的 3px 光环（box-shadow: 0 0 0 3px var(--accent-dim)）——
    // 用一圈 border.width 3 的同心矩形画在节点底下（比 layer 便宜，且不依赖场景图后端）。
    Rectangle {
        id: ring
        x: root.linkW - 3
        y: -3
        width: root.nodeW + 6
        height: root.nodeH + 6
        radius: height / 2
        color: "transparent"
        border.width: 3
        border.color: Qt.alpha(ThemeBridge.colors["accent.primary"], 0.14)
        visible: root.stageState === "run"
    }

    // —— .snode 底：background: var(--fill-muted) ——
    // 面色单独一层（base 在下、node 在上），这样 1px 边不会被面色盖住。
    Rectangle {
        id: base
        x: root.linkW
        y: 0
        width: root.nodeW
        height: root.nodeH
        radius: height / 2                    // --r-pill
        color: ThemeBridge.colors["fill.muted"]
        opacity: root.skipA ? 0.45 : 1.0      // .skip { opacity: .45 }
    }

    Rectangle {
        id: node
        x: root.linkW
        y: 0
        width: root.nodeW
        height: root.nodeH
        radius: height / 2                    // --r-pill
        // 面色层：run .14 / done .10 / fail .10，其余透明（透出底下的 fill.muted）
        color: root.tintA > 0 ? Qt.alpha(ThemeBridge.colors[root.tintToken], root.tintA) : "transparent"
        opacity: root.skipA ? 0.45 : 1.0       // .skip { opacity: .45 }
        border.width: 1                       // .snode { border: 1px solid var(--line-normal) }
        border.color: root.stageState === "run"  ? ThemeBridge.colors["accent.primary"]
                           : root.stageState === "done" ? Qt.alpha(ThemeBridge.colors["status.ok"], 0.40)
                           : root.stageState === "fail" ? Qt.alpha(ThemeBridge.colors["status.danger"], 0.45)
                           : area.containsMouse     ? ThemeBridge.colors["line.strong"]
                                                       : ThemeBridge.colors["line.normal"]
        // transition: border-color / background-color var(--dur-2) var(--ease)
        Behavior on border.color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }
        Behavior on color {       ColorAnimation { duration: root.reduce ? 0 : root.durBase } }

        // .snode.run { box-shadow: …, var(--shadow-accent) } —— shadow.accent 就是那条
        // rgba(accent, .28)（深空.json:32 = #35D0B447），偏移/模糊照 tokens.css:69 的 4px / 20px。
        // ⚠️ layer.enabled 必须带 root.shadows：ThemeBridge 探测不到场景图后端时（software），
        //    挂 layer 的 item 会被**整个吞掉** —— 不是阴影没画出来，是节点整块消失。
        // ⚠️ --shadow-accent 只有一层，MultiEffect 也只承载一层，这里是 1:1 复刻（不像 --shadow-1
        //    是两层要取舍）。
        layer.enabled: root.shadows && root.stageState === "run"
        layer.effect: MultiEffect {
            shadowEnabled: root.stageState === "run"
            shadowColor: ThemeBridge.colors["shadow.accent"]
            shadowVerticalOffset: 4
            shadowBlur: 20
            shadowScale: 1.0
        }

        // 状态点：run → .spin.sm（11px 居中在 7px 的点位上，向两侧各溢出 2px），
        // 其余 → 共享 Dot（.dot 7×7，StageFlow.jsx:22）
        // ⚠️ Dot 的 run 保持 false：设计稿里 pulse-dot 只挂在 .dot.run 上（ui.css:170-172），
        //    而 StageFlow 的 run 态走的是 .spin.sm，不是脉动的点。
        Spinner {
            x: root.padX - (height - root.dotW) / 2
            y: (node.height - height) / 2
            visible: root.stageState === "run"
            sm: true
            running: root.stageState === "run"
        }
        Dot {
            x: root.padX
            y: (node.height - height) / 2
            width: root.dotW
            height: root.dotW
            visible: root.stageState !== "run"
            // Dot 的 tone 是语义档（"ok"/"danger"/"idle" → status.* 三个 token），
            // 与设计稿 StageFlow.jsx:22 的 StatusDot tone 逐档对得上。
            tone: root.stageState === "done" ? "ok"
                : root.stageState === "fail" ? "danger"
                                             : "idle"
        }

        // .snode .code
        Text {
            id: codeText
            x: root.padX + root.dotW + root.gap
            y: (node.height - height) / 2
            text: root.code
            font.family: root.mono
            font.pixelSize: 10            // 10.5 → 就近取整 10
            font.weight: Font.Bold        // 700
            opacity: 0.8                  // .code { opacity: .8 }
            color: root.stageState === "done" ? ThemeBridge.colors["status.ok"]
                 : root.stageState === "run"  ? ThemeBridge.colors["accent.primary"]
                 : area.containsMouse        ? ThemeBridge.colors["text.primary"]
                                              : ThemeBridge.colors["text.muted"]
            Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }
        }

        // 阶段名：.snode 的 font-size 12 / font-weight 600
        Text {
            id: nameText
            x: codeText.x + codeText.implicitWidth + root.gap
            y: (node.height - height) / 2
            text: root.stageName
            visible: root.showName
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold     // 600
            color: root.stageState === "todo" && !area.containsMouse
                   ? ThemeBridge.colors["text.muted"]
                   : ThemeBridge.colors["text.primary"]
            Behavior on color { ColorAnimation { duration: root.reduce ? 0 : root.durBase } }
        }
    }

    // .snode { cursor: pointer }（ui.css:836）—— 一层 MouseArea 同时管 hover 与点击。
    // ⚠️ 覆盖范围**只到节点**，不含前面那 18px 的 .slink：设计稿的 onClick（StageFlow.jsx:19）
    //    和 cursor:pointer 都只挂在 .snode 上，.slink 是没有 handler 的兄弟节点。铺满整个
    //    StageNode 会让 18px 的连接线上也出现手型光标并可点。
    // ⚠️ 只发信号不自己改 stageState（见文件头）。
    MouseArea {
        id: area
        x: root.linkW
        y: 0
        width: root.nodeW
        height: root.nodeH
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.picked()
    }
}
