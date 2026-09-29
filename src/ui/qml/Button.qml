// src/ui/qml/Button.qml —— 共享按钮：ui.css:6-85 的 .btn / .btn.sm / 三种 variant
//
// ================================ 合并来源（4 份 → 1 份） ================================
// 公共行为全部保留，差异一律降级成属性（不再各写一份实现）：
//   · Button.qml（旧冻结版）  h30 / p0 14 / f13 / w600 / radius rSm / :disabled opacity .45
//   · AssetsBtn.qml           variant 字符串 + sm 档 + glyph 位 + **显式定位**排版
//   · StoryboardButton.qml    三种 variant 的完整配色 + hover 态 accent 辉光 + clicked
//   · ImageFlowBtn.qml        图标盒尺寸（.btn .icon 15 / .btn.sm .icon 13）+ shadow.accent 色
// 差异 → 属性：`primary`(bool) → `variant`(string)；「无图标位」→ `glyph`；「只有 h30 一档」→
// `sm`；三份私有件各叫各的 `picked` / `clicked` → 统一 `clicked()`。
// ==========================================================================================
//
// 设计稿（webui/src/styles/ui.css，行号即真值）：
//   .btn               h30 / p0 14 / gap 6 / r-sm / fw600 / f13 / border 1px solid transparent  :6-21
//   .btn:active        transform: scale(0.97)                                                  :22-24
//   .btn:disabled      opacity 0.45 + transform: none                                            :25-29
//   .btn.sm            h24 / p0 10 / f12 / gap 4                                                :30-35
//   .btn .icon         15 × 15                                                                 :42-45
//   .btn.sm .icon      13 × 13                                                                 :46-49
//   .btn-primary       底 --accent / 字 --accent-fg / box-shadow: 0 1px 0 rgba(255,255,255,.12)
//                      inset **+ 0 0 0 0 transparent** ← 基态的外阴影是「0 模糊 0 扩展 + 全透明」，
//                      即基态**没有**可见外辉光，可见辉光只出现在 :hover                             :51-55
//   .btn-primary:hover 底 --accent-h / box-shadow var(--shadow-accent)                          :56-59
//   .btn-secondary     底 --bg-elevated / 边 --line-normal / 字 --text-primary                  :60-64
//                      hover: 底 --fill-hover / 边 --line-strong                                :65-68
//   .btn-ghost         字 --text-secondary / 边 transparent（**无底色**）                        :69-72
//                      hover: 底 --fill-hover / 字 --text-primary                              :73-76
//   动效：transition 走 --dur-1（tokens.css:24 = 120ms = Ctl 的 durFast）、--ease 是
//   cubic-bezier(0.2,0,0,1)（tokens.css:27）→ QML 侧沿用本仓既有的 Easing.OutCubic。
//   ⚠️ 按钮**没有任何 @keyframes**（base.css:130-164 只有 fade-up / spin / pulse-dot 等，
//   都与 .btn 无关）—— 唯一运动就是上面那条 transition 列表，不要自己再加动画。
//
// 几何推导（CSS 的 flex 盒 → QML 的 implicitWidth/Height）：
//   padding 是左右各一份、与内容无关 → 宽 = padH*2 + 内容宽；
//   gap 只在「图标和文字都在」时才成立（flex 里只排一个子项就没有 gap）→ 见下面 gapW 的写法。
//   字号两档都是整数（13 / 12），本仓的半像素取整规则在这里用不上。
//
// ⚠️ 设计稿里还有两处**本组件刻意不实现**，都已记进 docs/90-reference/ui-design-parity-gaps.md：
//   1) `.btn-primary` 的 `0 1px 0 rgba(255,255,255,0.12) inset` 内高光（ui.css:54）—— MultiEffect
//      只做外阴影，且那层白色的 alpha 不在任何一个 token 里，硬写会变成第二套颜色真值
//      （gaps 文档第 350 行：QSS 侧同样「未做」）。
//   2) `.btn-lg`（ui.css:36-41 h36 / p0 20 / f14 / r-md）与 `.btn-danger`（ui.css:77-85）——
//      冻结下来的 API 只有三个 variant、两档尺寸，**没有 lg / danger 槽位**。Gallery.qml 目前拿
//      secondary 画了个「危险」按钮，那是页面的事，不在共享件里补第四套配色。
import QtQuick
import QtQuick.Effects  // MultiEffect：.btn-primary:hover 的 accent 辉光
import Shine 1.0

// ⚠️ 本文件的 layer.effect 内**没有引用任何外层 id**（条件全部收在 layer.enabled 上），
// 所以不需要 `pragma ComponentBehavior: Bound`；一旦在 MultiEffect 里写 root.xxx 就必须补上
// —— 对照 StoryboardButton.qml:22-23。

Ctl {
    id: root

    // —— 对外 API：调用方要改外观只改属性，不改实现 ——
    property string text: ""
    // 字符图标占位。设计稿是内联 SVG，本仓不引外部图标资源，kit 既有约定就是用字符
    // （ImageFlowWorkspace.cpp「与 kit 字符图标约定一致」同一条）。字号见 iconPx。
    property string glyph: ""
    property string variant: "secondary"   // "primary" | "secondary" | "ghost"
    property bool sm: false
    signal clicked()

    // ⚠️ 旧的 `primary: true/false`（Gallery.qml:231/240/249/258、Parity.qml:141-150）已被
    // variant 取代，由页面侧迁移调用点；本组件内部只暴露只读派生量，别再声明可写的
    // `property bool primary` —— 那是旧 API 的名字，留着会让两个语义并存。

    readonly property bool isPrimary: variant === "primary"
    readonly property bool isGhost: variant === "ghost"

    // —— 几何档（ui.css:10-15 / :30-35 / :42-49）——
    readonly property int padH: sm ? 10 : 14                    // .btn.sm p0 10 / .btn p0 14
    readonly property real gap: sm ? 4 : 6                      // .btn.sm gap 4 / .btn gap 6
    readonly property int labelPx: sm ? 12 : ThemeBridge.baseFontPx  // f12 / f13
    // ⚠️ 字形字号按 CSS 的 **icon 盒尺寸**出（15 / 13），不再做「字形在盒内」的二次缩放：
    // 旧 StoryboardButton.qml:87 写的是 9 / 10，那没有任何设计稿依据（盒本身就是 15/13），
    // 缩小的后果是同一个「保存」在两个页面上大小不一致。
    readonly property int iconPx: sm ? 13 : 15                  // .btn.sm .icon 13 / .btn .icon 15

    // ⚠️ 图标与文字**显式定位**，不套 Row（旧的两份实现套了 Row）：
    //   1) 设计稿的 `padding: 0 14px` + `gap` 是固定值，而定位器的 spacing 会给不可见子项留位
    //      （AssetsChip.qml:39-40 记的就是「不确定」）—— 纯图标按钮会凭空多出一截 gap；
    //   2) 旧实现把 implicitWidth 绑在 Row.implicitWidth 上，而那个 Row 又 anchors.centerIn
    //      在父里，调用方一给显式 width，两者就互相拆台。
    // 现在排版由这两条 readonly 算死：gap 只在两者都非空时计入。
    readonly property real glyphW: glyph === "" ? 0 : glyphText.implicitWidth
    readonly property real labelW: text === "" ? 0 : labelText.implicitWidth
    readonly property real gapW: glyphW > 0 && labelW > 0 ? gap : 0

    implicitHeight: sm ? 24 : 30                              // .btn.sm 24 / .btn 30
    implicitWidth: padH * 2 + glyphW + labelW + gapW

    radius: root.rSm                                          // --r-sm 6（ui.css:13）
    // .btn:disabled { opacity: 0.45 }（ui.css:26）
    // ⚠️ enabled / visible 用 Item 内建的，**不要重复声明**：重复声明会触发
    //   "Member enabled of the object Button_QMLTYPE_1 overrides a member of the base object"
    // 并被 QML 静默吞掉赋值（旧 Button.qml:15-18 的注释记的就是这个）。内建 enabled 顺带把
    // 子树里的 MouseArea 一起禁用，正是 .btn:disabled 想要的。
    opacity: enabled ? 1.0 : 0.45
    // ⚠️ Ctl 的基类是 Rectangle，color 默认是**白色**。本组件三条分支都显式给了 color，
    // 不会出现「白底方块」；新增分支时记得一起给。

    // 底色（ui.css:51-76）。hover 一律先问 variant，别让次色/幽灵串味。
    color: {
        // .btn-primary:hover { background: var(--accent-h) }（ui.css:57）
        if (root.isPrimary)
            return area.containsMouse ? ThemeBridge.colors["accent.primary.hover"]
                                     : ThemeBridge.colors["accent.primary"]
        // .btn-ghost:hover { background: var(--fill-hover) }（ui.css:74）
        if (root.isGhost)
            return area.containsMouse ? ThemeBridge.colors["fill.hover"] : "transparent"
        // .btn-secondary:hover { background: var(--fill-hover) }（ui.css:66）
        return area.containsMouse ? ThemeBridge.colors["fill.hover"]
                                 : ThemeBridge.colors["bg.elevated"]
    }
    // `.btn { border: 1px solid transparent }`（ui.css:16）：**边框恒为 1px**，
    // 变体只改颜色 —— 宽度归零会让 hover 的换色看起来像「按钮忽然变胖了」。
    border.width: 1
    border.color: root.isPrimary ? ThemeBridge.colors["accent.primary"]     // .btn-primary 无边色 → 同底色
                  : root.isGhost   ? "transparent"                          // .btn-ghost（ui.css:71）
                  // .btn-secondary:hover { border-color: var(--line-strong) }（ui.css:67）
                  : (area.containsMouse ? ThemeBridge.colors["line.strong"]
                                        : ThemeBridge.colors["line.normal"])

    // .btn:active { transform: scale(0.97) }（ui.css:23）
    // ⚠️ :disabled 另有 `transform: none`（ui.css:28），这里**不用**额外写：enabled 为 false 时
    // 子树里的 MouseArea 收不到 press，pressed 恒 false，缩放自然不发生。
    scale: area.pressed ? 0.97 : 1.0

    // 三条 Behavior 都读 root.reduce 短路；ui.css:19-20 的 transition 覆盖
    // background-color / border-color / color / box-shadow / transform，逐条对上了。
    Behavior on color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on border.color {
        ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }
    Behavior on scale {
        NumberAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
    }

    // .btn-primary:hover { box-shadow: var(--shadow-accent) }（ui.css:58）
    // ⚠️ layer.enabled **必须**带 `root.shadows &&`：software 场景图后端下 layer.effect 会把
    // 整个 item 吞掉（不是「阴影没画出来」，是按钮整排不显示）—— Ctl.qml:27-31 记着这条，
    // 旧冻结 Button.qml 曾经整排消失就是这个原因。
    // ⚠️ 只在 hover 时挂 layer：设计稿基态的外阴影是 `0 0 0 0 transparent`（ui.css:54），
    // 可见辉光只属于 :hover。旧冻结版 / AssetsBtn 是「primary 就常亮」，那是实现漂移，已改掉。
    // `&& !root.reduce`：减少动效时连 layer 都不分配（ImageFlowBtn.qml:62-63 是同一写法）。
    layer.enabled: root.shadows && root.isPrimary && area.containsMouse && !root.reduce
    layer.effect: MultiEffect {
        // ⚠️ 必须显式给 shadowColor：MultiEffect 的默认阴影色是**黑**，而 `--shadow-accent`
        // 是 accent 的 28% 透明（tokens.css:69）。不设的话 teal 按钮外面套一圈黑晕。
        shadowColor: ThemeBridge.colors["shadow.accent"]
        // ⚠️ CSS 写的是 `0 4px 20px`（tokens.css:69），四份 QML 旧实现却都只给了 blur 1 /
        // scale 1、无纵向偏移。这里保持旧值（四份一致，不是某一版的笔误），**辉光的范围
        // 偏小是已知缺口，需要出图确认**；shadow.accent 的颜色是对的（Token.h:60 的注释
        // 就写着它就是主按钮的强调辉光）。
        shadowBlur: 1.0
        shadowScale: 1.0
    }

    // 字色（ui.css:53 / :63 / :70 / :75）。图标继承同一个色（设计稿的 .icon 不设 color）。
    readonly property color fgColor: root.isPrimary ? ThemeBridge.colors["accent.primary.fg"]
                                      // .btn-ghost:hover { color: var(--text-primary) }（ui.css:75）；
                                      // secondary 常态本来就是 text-primary，所以两者可并成一条
                                      : (area.containsMouse ? ThemeBridge.colors["text.primary"]
                                                            : ThemeBridge.colors["text.secondary"])

    Text {
        id: glyphText
        x: root.padH
        y: (root.height - height) / 2
        visible: root.glyph !== ""
        text: root.glyph
        color: root.fgColor
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.iconPx          // .btn .icon 15 / .btn.sm .icon 13
        // 同上：ui.css:20 的 transition 列了 color，图标字色跟着 fgColor 走。
        Behavior on color {
            ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
        }
    }

    Text {
        id: labelText
        x: root.padH + root.glyphW + root.gapW
        y: (root.height - height) / 2
        visible: root.text !== ""
        text: root.text
        color: root.fgColor
        font.family: ThemeBridge.fontFamily
        font.pixelSize: root.labelPx         // f13 / .btn.sm f12
        font.weight: Font.DemiBold           // font-weight: 600
        // ui.css:20 的 transition 里含 color（ghost 悬停要换字色），所以字色也走动画。
        Behavior on color {
            ColorAnimation { duration: root.reduce ? 0 : root.durFast; easing.type: Easing.OutCubic }
        }
    }

    // ⚠️ 可交互组件必须**发信号**（kit 契约第 3 条）：调用方既能 `onClicked:`，也能挂
    // Connections。不要改成在 MouseArea 里写业务逻辑 —— 旧 GalleryIconBtn.qml:39 那种
    // 「onClicked 里自己 root.active = !root.active」就是反例，见 IconBtn.qml 的说明。
    // ⚠️ id 只能**不加限定**地引用：写成 root.area 会让 qmllint 报
    //   "Member area not found on type Button"（它按属性表查 id，不查作用域链）。
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
