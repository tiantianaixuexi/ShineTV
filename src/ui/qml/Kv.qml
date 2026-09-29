pragma ComponentBehavior: Bound   // Repeater delegate 里引用 root，必须显式绑定（pragma 必须在 import 之前）
// src/ui/qml/Kv.qml —— 键值表（共享件：ui.css:1015-1028 的 .kv）
//
// ================================ 合并来源：两份手搓实现收成一份 ================================
//   GalleryKv.qml : rows: [[k, v], …] + Timer/Connections 延后重算键列宽 + rowH 14
//   AssetsKv.qml  : rows: [{k, v}, …] + **让调用方回填** keyW（noteKeyWidth）+ rowH 20
//   → 统一：rows: [{ key, value }] + **组件自己测** + rowH 20（设计稿档）+ keyToken/valToken 可换色。
// =================================================================================================
//
// 设计稿：
//   .kv     { display:grid; grid-template-columns:auto 1fr; gap:6px 14px;   ← ui.css:1015-1020
//             font-size:12.5px }
//   .kv .k  { color:var(--text-muted); white-space:nowrap }                 ← ui.css:1021-1024
//   .kv .v  { color:var(--text-primary); font-weight:500 }                   ← ui.css:1025-1028
//   行高     body line-height 1.6（base.css:16）被 .kv 继承 → 12.5 × 1.6 = 20
//
// 几何（CSS grid auto/1fr → 显式坐标，不用 Grid 定位器）：
//   · 第一列 `auto` = 全部键里最宽的一个；第二列 `1fr` = 吃掉剩余宽度。
//     自己算列宽比让 Grid 猜更可控，而且行内都是静态文本、不做 hover 位移，
//     页面层那条「不要给需要位移的元素套定位器」的纪律这里反过来也成立。
//   · 列距 14 / 行距 6（grid 的 `gap: 6px 14px` 是「行 列」的顺序，别读反）。
//   · 字号 12.5 → 12（就近取整，与 QssBuilder 的同档一致）；值列 w500 → Font.Medium。
//   · 行高 20 = 12.5 × 1.6。⚠️ GalleryKv 的 14 是「12px 行高取整」的口径，比设计稿矮 6px，
//     本组件取 20 为默认；紧凑处调用方可以用 rowH 覆盖（那一档是页面自己的排版选择，不是设计稿的）。
//
// ⚠️ 键列宽度的测量（本仓踩过的坑，两份旧实现各错一半）：
//   「CSS auto 列的宽度」必须由组件自己量，但**绝不能**写成绑定：
//       keyW = max(measure(k) for k)     // measure 写 probe.text 再读 probe.width
//   绑定环检测器管的是「一个绑定在求值过程中写了它自己所依赖的属性」：求值顺序是
//   写 probe.text → probe.width 变脏 → 紧接着读 probe.width，Qt 看到「keyW 求值期间它依赖的
//   probe.width 被改了」就报环。换成 TextMetrics 只消掉了「写 item 树」那一半，写-读同族属性
//   那一半原封不动。
//   → 所以 keyW 是**普通属性 + 显式重算**（recomputeKeyW），重算只在三个时机发生：
//       ① Component.onCompleted（首帧）        ② onRowsChanged（换行集）
//       ③ ThemeBridge.themeChanged —— 延到下一轮事件循环（Timer interval 0）再算。
//     ③ 不能省：colors 变了不影响字体度量，但 **fontFamily 会随主题变**（水墨是衬线族，
//     Theme.h:118），漏掉它键列宽会永远停在第一套主题的字体度量上。
//     也不能在 onThemeChanged 里直接同步算：信号发出时 fontFamily 的绑定可能还没落定。
//   ⚠️ 另外 AssetsKv 那套「每一行回报自己的 implicitWidth，调用方回填」也一并去掉了：
//     它把 keyW 变成只增不减，rows 换掉后仍是旧的最大值（AssetsKv:24 自己承认了）。
//
// ⚠️ 值列的 ElideRight：`.kv .v` 本身没写 text-overflow（ui.css:1025-1028），
//    但 `1fr` 列在 grid 里不会自己收口，长值会把整张表撑宽。两份旧实现都 ElideRight，
//    这里保留（`:nowrap + ellipsis` 的等价物）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    // —— 调用方 API ——
    // rows: [{ key: "书", value: "…" }, …]（⚠️ 键名是 key/value，不是旧实现的 k/v 或数组对）
    property var rows: []
    property real rowH: 20              // 12.5 × 1.6；紧凑处调用方可覆盖
    property real rowGap: 6             // grid 的行间距
    property real colGap: 14           // grid 的列间距
    property string keyToken: "text.muted"      // .kv .k
    property string valToken: "text.primary"   // .kv .v

    // ⚠️ 不是调用方 API。CSS auto 列的实测宽度，**必须**是普通属性（见文件头「键列宽度」），
    //    所以它既不是 readonly 也不是绑定，由 recomputeKeyW() 显式写。
    property real keyW: 0

    implicitHeight: rows.length > 0 ? rows.length * rowH + (rows.length - 1) * rowGap : 0
    implicitWidth: 200                  // .kv 是 100% 宽的 grid，设计稿没有内在宽度；只作兜底
    color: "transparent"

    // —— 键列重算：三个时机（首帧 / 换 rows / 换主题的字体族）——
    function recomputeKeyW() {
        var w = 0
        for (var i = 0; i < root.rows.length; ++i)
            w = Math.max(w, root.measure(root.rows[i].key))
        root.keyW = w
    }

    Component.onCompleted: recomputeKeyW()
    onRowsChanged: recomputeKeyW()

    Connections {
        target: ThemeBridge
        function onThemeChanged() { defer.restart() }
    }
    Timer {
        id: defer
        interval: 0
        onTriggered: root.recomputeKeyW()
    }

    // 显式坐标，不套 Column/Grid：行数是数据、列宽是实测值，两者都要能单独变
    Repeater {
        model: root.rows
        delegate: Item {
            id: kvRow
            required property int index
            required property var modelData

            x: 0
            y: kvRow.index * (root.rowH + root.rowGap)
            width: root.width
            height: root.rowH

            Text {
                id: keyText
                x: 0
                width: root.keyW
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                // .k { white-space: nowrap } —— 键列永不折行。keyW 是实测值，但字体族在
                // 主题切换的那一帧可能还没重算完，此时不锁 NoWrap 会把键名折成两行、
                // 顶破行高（WordWrap 是 QML Text 的默认值）。
                wrapMode: Text.NoWrap
                text: kvRow.modelData.key
                color: ThemeBridge.colors[root.keyToken]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12      // .kv f12.5px → 就近取整 12
            }
            Text {
                id: valText
                x: root.keyW + root.colGap
                width: Math.max(0, root.width - x)
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: kvRow.modelData.value
                color: ThemeBridge.colors[root.valToken]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 12
                font.weight: Font.Medium     // 500
            }
        }
    }

    // 字号测量器：只量字宽。用 TextMetrics 而不是隐藏 Text —— 它不是 Item，不进 item 树。
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
