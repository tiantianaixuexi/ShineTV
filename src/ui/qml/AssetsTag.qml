pragma ComponentBehavior: Bound
// src/ui/qml/AssetsTag.qml —— 页面私有：.tag.sm
//
// 对照 webui/src/styles/ui.css:120-145：
//   .tag     h20 / p0 8 / r-pill / f11.5 w600 / line-normal 边 / fill-muted 底
//   .tag.sm  h17 / p0 6 / f10.5
//   tone 变体：12% 混色底 / 35% 混色边 / tone 满强度文字
//
// ⚠️ 本页设计稿所有 Tag 都带 `sm`（`<Tag tone={…} sm>`），而冻结的 Tag.qml 只做默认
// 尺寸（h20 / f11），所以 sm 尺寸在这里复刻；底/边/字三条线仍走 ThemeBridge 的
// toneBg / toneEdge —— 与 QSS 的 FillToneMixes 是同一份 color-mix 公式。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string text: ""
    property string tone: ""   // accent/info/ok/warn/danger/busy/pending/idle；空串 = 默认

    // ⚠️ CSS 里 --accent 对应本仓 token 名 accent.primary，--info 是 accent.info；
    // 直接拼 "status." + tone 会给 accent/info 取到不存在的名字。
    readonly property string toneToken: tone === "accent" ? "accent.primary"
                                            : tone === "info"  ? "accent.info"
                                                              : "status." + tone
    readonly property bool plain: tone === "" || tone === "idle"

    readonly property color bgColor: root.plain ? ThemeBridge.colors["fill.muted"]
                                                : ThemeBridge.toneBg[tone]
    readonly property color fgColor: tone === ""    ? ThemeBridge.colors["text.secondary"]
                                  : tone === "idle" ? ThemeBridge.colors["text.muted"]
                                  : ThemeBridge.colors[root.toneToken]
    readonly property color edgeColor: root.plain ? ThemeBridge.colors["line.normal"]
                                                  : ThemeBridge.toneEdge[tone]

    implicitHeight: 17       // .tag.sm height: 17px
    implicitWidth: label.implicitWidth + 12   // padding: 0 6px
    radius: root.rPill
    antialiasing: true

    color: root.bgColor
    border.width: 1
    border.color: root.edgeColor

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.fgColor
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 10      // .tag.sm font-size: 10.5px → 取整 10（同 .tag 11.5→11 的规则）
        font.weight: Font.DemiBold
    }
}
