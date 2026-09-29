pragma ComponentBehavior: Bound
// src/ui/qml/AssetsArt.qml —— 页面私有：过程艺术占位图
//
// 对照 webui：
//   * .art / .art-cover        styles/ui.css:1075-1089（r-sm 6 / 底 fill-muted / 裁切）
//   * Art 组件的构图           components/UI.jsx:198-220（viewBox 160x100 + xMidYMid slice）
//   * .asset-card:hover .thumb svg { transform: scale(1.07) }  styles/views.css:720-722
//
// ⚠️ 设计稿那 12 组配色是**硬编码 hex**（UI.jsx ART_PAL），QML 侧禁止硬编码色值，
// 所以这里保留「12 组 × 3 色」的结构与 `Math.abs(seed) % 12` 的取模顺序，
// 色值全部换成 ThemeBridge 的 token 名 —— 主题切换时占位图跟着换色。
//
// ⚠️ 波浪剪影用「上圆角矩形叠三层」近似 SVG 的 Q 曲线：本页的 QML 模块清单里
// QtQuick.Shapes / Canvas 都不在 find_package 组件里（只有 Quick/QuickWidgets/
// Qml/QuickEffects），引它们会在别的机器上整页加载失败，所以不引。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property int seed: 0
    // zoom/zoomed 分开：zoomed 由外部（如资产卡的 hover）驱动，
    // 组件自己只负责 0.5s ease-out 的 scale 过渡（.art svg 的 transition）
    property bool zoom: false
    property bool zoomed: false

    implicitWidth: 160
    implicitHeight: 100
    radius: root.rSm                      // --r-sm 6
    color: ThemeBridge.colors["fill.muted"]
    clip: true
    antialiasing: true

    // ⚠️ 属性名不能叫 palette —— QQuickItem 已有 palette 成员
    readonly property var artPalette: [
        ["bg.void", "accent.primary", "accent.secondary"],
        ["bg.void", "accent.secondary", "status.warn"],
        ["bg.void", "accent.info", "status.busy"],
        ["bg.void", "status.pending", "text.primary"],
        ["bg.void", "accent.secondary", "status.danger"],
        ["bg.void", "status.ok", "status.warn"],
        ["bg.void", "status.busy", "accent.info"],
        ["bg.void", "status.danger", "accent.secondary"],
        ["bg.void", "accent.primary", "status.pending"],
        ["bg.void", "status.warn", "status.ok"],
        ["bg.void", "text.secondary", "status.danger"],
        ["bg.void", "accent.secondary", "accent.info"]
    ]
    readonly property int slot: Math.abs(seed) % artPalette.length
    readonly property color c0: ThemeBridge.colors[artPalette[slot][0]]   // 暗底
    readonly property color c1: ThemeBridge.colors[artPalette[slot][1]]   // 天光
    readonly property color c2: ThemeBridge.colors[artPalette[slot][2]]   // 光源

    readonly property real sun: 30 + ((seed * 37) % 40)  // UI.jsx 的 sx

    // preserveAspectRatio="xMidYMid slice"：等比放大到刚好盖住方框，再居中裁切
    readonly property real k: Math.max(width / 160, height / 100)
    readonly property real offX: (width - 160 * k) / 2
    readonly property real offY: (height - 100 * k) / 2

    Item {
        id: scene
        x: root.offX
        y: root.offY
        width: 160 * root.k
        height: 100 * root.k

        scale: root.zoom && root.zoomed && !root.reduce ? 1.07 : 1.0
        Behavior on scale {
            NumberAnimation { duration: root.reduce ? 0 : 500; easing.type: Easing.OutCubic }
        }

        // 天空：dark → c1 55% 透明
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: root.c0 }
                GradientStop { position: 1.0; color: Qt.alpha(root.c1, 0.55) }
            }
        }

        // 光源：r13 实心 0.9 + r20 光晕 0.25（UI.jsx 两个 circle）
        Rectangle {
            x: root.sun - 20 * root.k
            y: 34 * root.k - 20 * root.k
            width: 40 * root.k
            height: 40 * root.k
            radius: width / 2
            color: root.c2
            opacity: 0.25
        }
        Rectangle {
            x: root.sun - 13 * root.k
            y: 34 * root.k - 13 * root.k
            width: 26 * root.k
            height: 26 * root.k
            radius: width / 2
            color: root.c2
            opacity: 0.9
        }

        // 三层剪影：路径 Q 曲线的近似（半径随 seed 变化，保持 12 张图互不相同）
        Rectangle {
            x: 0
            y: (76 + (root.seed % 4)) * root.k
            width: 160 * root.k
            height: 24 * root.k
            color: root.c0
            opacity: 0.75
            topLeftRadius: (10 + (root.seed % 6)) * root.k
            topRightRadius: (16 + (root.seed % 8)) * root.k
        }
        Rectangle {
            x: 0
            y: (86 + (root.seed % 3)) * root.k
            width: 160 * root.k
            height: 14 * root.k
            color: root.c1
            opacity: 0.3
            topLeftRadius: (14 + (root.seed % 5)) * root.k
            topRightRadius: (8 + (root.seed % 7)) * root.k
        }
        Rectangle {
            x: 0
            y: (92 + (root.seed % 2)) * root.k
            width: 160 * root.k
            height: 8 * root.k
            color: root.c0
            opacity: 0.9
            topLeftRadius: (6 + (root.seed % 4)) * root.k
            topRightRadius: (12 + (root.seed % 5)) * root.k
        }
    }
}
