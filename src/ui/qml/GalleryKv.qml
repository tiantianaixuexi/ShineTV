// src/ui/qml/GalleryKv.qml —— 键值表（ui.css:1015-1028 的 .kv）
//
// 两列 grid：auto / 1fr，列距 14、行距 6、字号 12.5px。
// 键列宽 = 全部键里最宽的一个（这就是 CSS `auto` 的含义），值列吃掉剩余宽度。
// 行内元素都是静态文本、不做 hover 位移，所以这里用显式坐标而不是 Grid：
// 页面层的纪律是「不要给需要位移的元素套定位器」，反过来也成立 ——
// 自己算列宽比让 Grid 猜更可控。
import QtQuick
import Shine 1.0

pragma ComponentBehavior: Bound

Ctl {
    id: root

    // rows: [["键", "值"], ...]
    property var rows: []

    readonly property int rowH: 14          // 12px 行高取整
    readonly property int rowGap: 6
    readonly property int colGap: 14

    // CSS auto 列：取最宽的键。
    //
    // ⚠️ 这里**故意不是绑定**。绑定环检测器跟 item 树无关，它管的是
    //    「一个绑定在求值过程中写了它自己所依赖的属性」：
    //        keyW = f(... measure(s))   // measure 写 probe.text 再读 probe.width
    //    求值顺序是 写 probe.text → probe.width 变脏 → 紧接着读 probe.width，
    //    Qt 看到「keyW 求值期间它依赖的 probe.width 被改了」就报环。
    //    换成 TextMetrics 只消掉了「写 item 树」那一半，写-读同族属性这一半
    //    原封不动 —— 所以必须让 keyW 变成**普通属性 + 显式重算**。
    property real keyW: 0

    implicitHeight: rows.length * rowH + Math.max(0, rows.length - 1) * rowGap
    implicitWidth: 220

    color: "transparent"

    function recomputeKeyW() {
        var w = 0
        for (var i = 0; i < root.rows.length; ++i)
            w = Math.max(w, root.measure(root.rows[i][0]))
        root.keyW = w
    }

    Component.onCompleted: recomputeKeyW()
    onRowsChanged: recomputeKeyW()

    // 换主题会换 ThemeBridge.fontFamily，字宽随之变化 —— 漏掉这条，
    // 键列宽会永远停在第一套主题的字体度量上。
    // 延到下一轮事件循环：themeChanged 发出时 fontFamily 的绑定可能还没落定。
    Connections {
        target: ThemeBridge
        function onThemeChanged() { defer.restart() }
    }
    Timer {
        id: defer
        interval: 0
        onTriggered: root.recomputeKeyW()
    }

    Column {
        spacing: root.rowGap

        Repeater {
            model: root.rows
            delegate: Item {
                id: kvRow
                required property int index
                required property var modelData

                width: root.width
                height: root.rowH

                Text {
                    x: 0
                    width: root.keyW
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: kvRow.modelData[0]
                    color: ThemeBridge.colors["text.muted"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12          // 12.5px → 就近取整
                }
                Text {
                    x: root.keyW + root.colGap
                    width: Math.max(0, root.width - x)
                    height: parent.height
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    text: kvRow.modelData[1]
                    color: ThemeBridge.colors["text.primary"]
                    font.family: ThemeBridge.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.Medium     // 500
                }
            }
        }
    }

    // 字号测量器：只负责量字宽，不参与 keyW 的依赖图（keyW 现在是普通属性）。
    // 用 TextMetrics 而不是隐藏 Text：它不是 Item，不进 item 树。
    TextMetrics {
        id: probe
        font.family: ThemeBridge.fontFamily
        font.pixelSize: 12
    }
    function measure(s) {
        probe.text = s
        return probe.width
    }
}
