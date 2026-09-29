// src/ui/qml/FilmCell.qml —— 底部连播胶片条的一格
// （webui views.css:262-270 的 .float-strip .film-cell：w118 / r-sm /
// line-normal 边 / .fthumb h56 / .fmeta p6 9 gap7 f11）
//
// ⚠️ **没有缩略图**。设计稿用 `<Art seed={s.id+4} cover>` 画程序化占位图，
// 那是**假缩略**；迁移前的 FilmStrip 有个 QImage thumb 字段，但从来没被填过，
// 页面一直显示「无首帧 / 待出片」空态。成片列表最怕看错镜头，所以这里坚持
// 没有首帧就是没有 —— 要接真缩略图时，走「worker 解码 → 宿主注入 file:// URL」，
// 不要在本组件里同步读盘。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    property string code: ""            // 镜号（S01）
    property string duration: ""        // 时长/帧率（空 = 未出片）
    property bool ready: false          // 视频是否就绪
    property bool playing: false        // 正在查看这一格
    property string videoPath: ""       // 就绪时的产物路径（tooltip）
    signal picked()

    readonly property int thumbH: 56     // .fthumb height 56

    width: 118                          // .film-cell width 118
    implicitHeight: root.thumbH + 30    // 缩略 56 + meta 30（.fmeta p6 9 + f11 行）
    height: implicitHeight
    radius: root.rSm                    // --r-sm 6
    color: "transparent"
    // hover 边转 accent-glow（迁移前 FilmStrip 的 QSS 同款）
    border.width: 1
    border.color: hit.containsMouse ? Qt.alpha(ThemeBridge.colors["accent.primary"], 0.3)
                                   : ThemeBridge.colors["line.normal"]
    Behavior on border.color {
        ColorAnimation {
            duration: ThemeBridge.reduceMotion ? 0 : ThemeBridge.durations.fast
            easing.type: Easing.OutCubic
        }
    }
    // 未出片的格子按设计稿降透明度（.film-cell opacity .5）
    opacity: root.ready ? 1 : 0.5

    // —— .fthumb：显式空态，不复用别的镜头的图 ——
    Item {
        id: thumb
        x: 0
        y: 0
        width: parent.width
        height: root.thumbH
        clip: true

        Text {
            anchors.centerIn: parent
            width: parent.width - 8
            horizontalAlignment: Text.AlignHCenter
            // 就绪但没有首帧 → 「无首帧」；未就绪 → 「待出片」。两态都要说清楚，
            // 含糊成「暂无」会让人以为镜头丢了。
            text: root.ready ? "无首帧" : "待出片"
            elide: Text.ElideRight
            font.family: ThemeBridge.fontFamily
            font.pixelSize: 11
            color: ThemeBridge.colors["text.muted"]
        }

        // 正在查看：遮罩 + 转圈（设计稿的 .spin 叠在缩略上）
        Rectangle {
            anchors.fill: parent
            visible: root.playing
            color: Qt.alpha(ThemeBridge.colors["bg.void"], 0.35)
        }
        Spinner {
            anchors.centerIn: parent
            visible: root.playing
        }
    }

    // —— .fmeta：p6 9 / gap 7 / f11；镜号 accent mono，右侧状态点 ——
    Item {
        id: meta
        x: 0
        y: root.thumbH
        width: parent.width
        height: 30

        Row {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            Text {
                text: root.code
                font.family: root.monoFont
                font.pixelSize: 11
                font.weight: Font.Bold
                color: ThemeBridge.colors["accent.primary"]
            }
            Text {
                text: root.ready ? root.duration : "—"
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 11
                color: ThemeBridge.colors["text.muted"]
            }
        }
        Dot {
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            tone: root.ready ? "ok" : "idle"
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        // 未出片的格不可点（设计稿 cursor: default + 不可触发 onClick）
        enabled: root.ready
        onClicked: { root.picked() }
    }

    // ⚠️ ThemeBridge 只桥出了每主题的 UI 字体族，没有 --font-mono 那一档
    // （tokens.css:31 的 Cascadia Code / JetBrains Mono / Consolas 链）。
    // 与 ImageFlow / CanvasNode 取同一个替身，不要每页各挑一个。
    readonly property string monoFont: "Consolas"
}
