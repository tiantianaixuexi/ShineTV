// src/ui/qml/Gallery/Tag.qml —— 对照 webui ui.css:120-145 的 .tag
//
// 关键点：tone 底/边用 ThemeBridge.toneBackground/toneEdge，那条路径与
// QssBuilder::FillToneMixes 共用 ToneMix.h 的同一份 color-mix 算法
// （12% 底 / 35% 边，基准 bg.surface）。所以 QML 的 Tag 与 Widgets 的 Tag
// 在同一主题下**必然同色** —— 这是这一层存在的全部意义。
//
// ⚠️ 底色是 12% 混色、文字是 tone 满强度（设计稿 `color: var(--tone)`，不是反白字）。
// 搞反的话胶囊会变成一块纯色、文字与底同色 → 看起来「有胶囊但没字」。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    // accent / info / ok / warn / danger / busy / pending / idle；空串 = 默认态
    property string tone: ""

    // tone → token 点分名。⚠️ accent 在 CSS 里是 --accent，但本仓的 token 名是
    // accent.primary；直接拼 "status." + tone 会对 accent 取到不存在的名字。
    readonly property string toneToken: tone === "accent" ? "accent.primary"
                                            : tone === "info"  ? "accent.info"
                                                              : "status." + tone
    readonly property bool plain: root.tone === "" || root.tone === "idle"

    // 底 / 字 / 边 三条线分开：默认与 idle 走中性 token，其余走 color-mix 派生态
    readonly property color bgColor: root.plain ? ThemeBridge.colors["fill.muted"]
                                                : ThemeBridge.toneBg[root.tone]
    readonly property color fgColor: root.tone === ""  ? ThemeBridge.colors["text.secondary"]
                                    : root.tone === "idle" ? ThemeBridge.colors["text.muted"]
                                    : ThemeBridge.colors[root.toneToken]
    readonly property color edgeColor: root.plain ? ThemeBridge.colors["line.normal"]
                                                  : ThemeBridge.toneEdge[root.tone]

    implicitHeight: 20      // ui.css .tag height: 20px
    implicitWidth: label.implicitWidth + 16
    radius: root.rPill      // --r-pill

    color: root.bgColor
    border.width: 1
    border.color: root.edgeColor

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 11     // 11.5 就近取整（与 QSS 同一规则）
        font.weight: Font.DemiBold
        color: root.fgColor
    }
}
