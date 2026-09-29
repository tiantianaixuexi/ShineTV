// src/ui/qml/Progress.qml —— 进度条（共享件：ui.css:430-454 的 .prog）
//
// ================================ 合并来源：两份手搓实现收成一份 ================================
//   GalleryProgress.qml : value: real ✓ / 过渡走 durSlow ✓ / shimmer 铺**整条槽**
//   ImageFlowProg.qml   : value: **int** ✗ / 过渡直接写 ThemeBridge.durations.slow ✗
//                         / shimmer 塞在**填充条内**（按设计稿是错的，见下）
//   → 统一：value 必须是 real、动效一律 root.reduce/root.dur*、shimmer 铺整条槽。
// =================================================================================================
//
// 设计稿：
//   .prog          { height:6px; border-radius:var(--r-pill); background:var(--fill-muted);  ← ui.css:430-436
//                    overflow:hidden; position:relative }
//   .prog > i      { display:block; height:100%; border-radius:var(--r-pill);                ← ui.css:437-443
//                    background:var(--grad-accent);
//                    transition: width var(--dur-3) var(--ease-out) }
//   .prog.run > i::after { content:""; position:absolute; inset:0;                        ← ui.css:444-451
//                    background:linear-gradient(100deg, transparent 20%, rgba(255,255,255,.35) 50%, transparent 80%);
//                    background-size:200% 100%; animation:shimmer 1.4s linear infinite }
//   .prog.thin     { height:4px }                                                          ← ui.css:452-454
//   @keyframes shimmer { from{background-position:-200% 0} to{background-position:200% 0} } ← base.css:150-153
//   --grad-accent: linear-gradient(120deg, #35d0b4, #6fa8ff)                               ← tokens.css:70
//                  =（DeepSpace 的 --accent #35d0b4 → --info #6fa8ff，tokens.css:46/50）
//   --dur-3: 320ms = ThemeBridge.durations.slow                                               ← tokens.css:26
//
// 几何（border-box；6 / 4 两档高度、r-pill 圆角、overflow hidden）：
//   填充宽 = 槽宽 × clamp(value, 0, 100) / 100，clamp 是为了 value 越界时不画到槽外。
//   ⚠️ value 必须是 **real**：ImageFlowProg 声明成 int 时，`Math.max(0, v - 18)` 这类算出的小数
//      会被 QML 静默截断（int 属性赋值走整数量化），表现为进度条在 0.x% 段完全不动。
//
// ⚠️ shimmer 铺的是**整条槽**，不是填充条（这一点两份旧实现不一致，按 CSS 判）：
//   `.prog.run > i::after` 里 `position:absolute; inset:0` 的包含块是**最近的定位祖先**，
//   `i` 自己没有 position（ui.css:437-441 只有 display/height/border-radius/background），
//   所以包含块是 `.prog`（唯一写了 `position:relative` 的，ui.css:435）—— 高光扫过整条槽。
//   ImageFlowProg 把 shimmer 塞在填充条里，与 CSS 不符。
//   高光几何照 CSS 换算（底图宽 = 200% × 槽宽 = 2W，亮带占底图的 20%–80% = 1.2W）：
//     带位置 x：background-position -200% → +200% 换算成「带心 -W → +3W」，
//               减去半个带宽 0.6W → **x 从 -1.6W 到 +2.4W，1.4s 线性一趟**（base.css:150-153）。
//     两端带子都完全在槽外（[-1.6W,-0.4W] / [2.4W,3.6W]），所以 infinite 循环的接缝看不见。
//     ⚠️ GalleryProgress 的旧写法是 700ms 去 + 700ms 回，一来回 1.4s —— 周期只有设计稿的一半，
//        而且只有 ±W 的行程（可见段 2.2W vs 这里的 4W）。这里照设计稿改成单趟 1.4s。
//
// ⚠️ 白色高光的来源（QML 侧的第二套颜色真值问题）：
//   CSS 是 `rgba(255,255,255,.35)` 的**纯白** 35%，而纯白不是 token —— 直接搬进 QML 就等于
//   多了一套颜色真值。所以取 **ThemeBridge.colors["text.primary"] 压 alpha 0.35**：
//   亮部随主题走（深色主题下 text.primary 是近白，水墨主题下是近黑的墨色，压在填充条上
//   读起来就是「提亮/压暗」而不是「贴了一块白」），且不新增任何字面色。
//   渐变角 100deg 在 QML 只能取 6 个正交朝向，降级成水平（两端色与顺序不变）。
//
// ⚠️ --grad-accent 的 120deg 同理降级成水平：轴向偏角只有 30°，在 4–6px 高的条上与水平渐变
//    肉眼无差（ImageFlowProg.qml:5-6 的判断，成立）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 调用方 API ——
    property real value: 0              // 0–100，⚠️ 必须是 real（见文件头）
    property bool run: false            // .prog.run 的 shimmer
    property bool thin: false           // .prog.thin 4px；默认 6px

    readonly property real barH: root.thin ? 4 : 6
    implicitHeight: barH
    implicitWidth: 90       // .q-prog { width:90px }（views.css:1116-1121）——设计稿里 .prog 旁
                            // 唯一的进度条宽度，只作兜底：两个调用点（Gallery.qml:363、
                            // ImageFlow.qml:664）都显式给了 width。
    color: "transparent"    // 底色在 track 上，根不画

    // shimmer 的驱动量。⚠️ 不是调用方 API。
    // 动画打在这个**普通属性**上而不是打在高光条的 x 上：
    //   · 直接对有绑定的属性做动画会把绑定打死；
    //   · `from`/`to` 是动画启动那一刻取的常量，中途改槽宽就不会跟着走。
    // 归一化成 0→1 后，x 由它换算，缩放/换宽都不跑偏。
    property real sweep: 0

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2                                  // --r-pill（>半边长时 Qt 自钳）
        color: ThemeBridge.colors["fill.muted"]             // .prog { background }
        clip: true                                         // .prog { overflow: hidden }

        // .prog > i
        Rectangle {
            id: fill
            x: 0
            height: track.height
            width: track.width * Math.max(0, Math.min(100, root.value)) / 100
            radius: track.radius                            // .prog > i { border-radius: var(--r-pill) }
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: ThemeBridge.colors["accent.primary"] }
                GradientStop { position: 1.0; color: ThemeBridge.colors["accent.info"] }
            }
            // transition: width var(--dur-3) var(--ease-out)
            Behavior on width {
                NumberAnimation { duration: root.reduce ? 0 : root.durSlow; easing.type: Easing.OutCubic }
            }
        }

        // .prog.run > i::after —— 亮带宽度 = 底图 200% 里的 20%–80% 段
        Rectangle {
            id: sheen
            width: track.width * 1.2
            height: track.height
            x: (-1.6 + 4 * root.sweep) * track.width
            // ⚠️ 减少动效下整层隐藏，而不是「停在半路」：带宽 1.2W 停在 x=0 时会歪在槽的正中，
            //    变成一条固定亮带（假的高光）。少动效偏好下宁可不画。
            visible: root.run && !root.reduce
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.2; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.alpha(ThemeBridge.colors["text.primary"], 0.35) }
                GradientStop { position: 0.8; color: "transparent" }
            }
        }
    }

    // animation: shimmer 1.4s linear infinite（base.css:150-153）—— 单趟，不是来回
    NumberAnimation on sweep {
        from: 0
        to: 1
        duration: 1400
        loops: Animation.Infinite
        easing.type: Easing.Linear
        running: root.run && !root.reduce
    }
}
