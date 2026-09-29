pragma ComponentBehavior: Bound
// src/ui/qml/AssetNav.qml —— 资产页左栏「实体树」（QML 迁移）
//
// 数据全部来自 C++ 注入的 `Page`（AssetPageModel）。本页**不持有任何 mock**：
// entityGroups / kinds 都是 model 上的 Q_PROPERTY，随 changed() 重算。
//
// 定位：替代 Widgets 侧 AssetWorkspace 的 QTreeWidget 导航。结构照设计稿
// views.css:716 起的 .assets-shell 220px 侧栏：4 个 kind 分组，组头可点做过滤，
// 组内是实体；点实体即选中并把资产网格过滤到该实体。
//
// 纪律（docs/10-modules/qml-kit.md）：
//   1. 继承 Ctl，尺寸由 QQuickWidget 宿主给，不写 width/height/visible；
//   2. 颜色一律 ThemeBridge 属性，不写字面色值；
//   3. 动效读 root.reduce；layer.enabled 走 root.shadows；
//   4. 树是「列高自适应」，不写行高常数。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    color: ThemeBridge.colors["bg.panel"]

    // 间距别名：Ctl 只转发圆角/动效，不带 spaces；键是 CSS 刻度名（见 ThemeBridge.cpp）
    readonly property int spXs: ThemeBridge.spaces["1"]   // 4
    readonly property int spSm: ThemeBridge.spaces["2"]   // 8
    readonly property int spMd: ThemeBridge.spaces["3"]   // 12

    // 选中某个实体时让内容区滚到顶部（换实体等于换了一整个网格）
    function ensureTop() { flick.contentY = 0 }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.implicitHeight + root.spMd
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // 整体滚动容器：不让每组各自滚，实体树是短列表
        Column {
            id: col
            width: parent.width
            spacing: 0

            // —— 顶部：书名 + 计数 ——
            Item {
                width: parent.width
                implicitHeight: head.implicitHeight + root.spMd * 2
                Column {
                    id: head
                    x: root.spMd
                    y: root.spMd
                    width: parent.width - root.spMd * 2
                    spacing: 2
                    Text {
                        text: Page.hasBook ? Page.bookName : "未打开书库"
                        color: ThemeBridge.colors["text.primary"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: ThemeBridge.baseFontPx
                        font.weight: Font.DemiBold
                        width: parent.width
                        elide: Text.ElideRight
                    }
                    Text {
                        text: Page.openError ? Page.openError
                                             : "实体 " + Page.kinds.length
                                               + " 组 · " + Page.assets.length + " 个资产"
                        color: ThemeBridge.colors["status.danger"]
                        visible: text !== "" && Page.openError !== ""
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 11
                        width: parent.width
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // —— 4 个 kind 分组 ——
            Repeater {
                model: Page.entityGroups

                delegate: Item {
                    id: group
                    required property var modelData
                    width: col.width
                    implicitHeight: kids.implicitHeight

                    Column {
                        id: kids
                        width: parent.width
                        spacing: 0

                        // 组头：点一下 = 按 kind 过滤
                        Item {
                            width: parent.width
                            implicitHeight: 30
                            Rectangle {
                                anchors.fill: parent
                                color: groupArea.containsMouse
                                       ? ThemeBridge.colors["fill.hover"]
                                       : "transparent"
                                radius: ThemeBridge.radii.sm
                                Behavior on color { ColorAnimation { duration: root.durFast } }
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: root.spMd
                                anchors.verticalCenter: parent.verticalCenter
                                text: group.modelData.label
                                color: ThemeBridge.colors["text.secondary"]
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: root.spMd
                                anchors.verticalCenter: parent.verticalCenter
                                text: group.modelData.count
                                color: ThemeBridge.colors["text.muted"]
                                font.family: ThemeBridge.fontFamily
                                font.pixelSize: 11
                            }
                            MouseArea {
                                id: groupArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    // 再点一次同一个组 = 取消过滤
                                    Page.setKindFilter(Page.kindFilter === group.modelData.key
                                                       ? "" : group.modelData.key)
                                    root.ensureTop()
                                }
                            }
                        }

                        // 组内实体
                        Repeater {
                            model: group.modelData.items

                            delegate: Item {
                                id: ent
                                required property var modelData
                                width: col.width
                                implicitHeight: 26

                                readonly property bool on: Page.selectedEntityId === modelData.id

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.leftMargin: root.spSm
                                    anchors.rightMargin: root.spSm
                                    radius: ThemeBridge.radii.sm
                                    // 键名是 fill.selected（5 套主题 JSON 里的实际键），
                                    // 别写成 fill.active —— 取不到会得到 undefined，
                                    // 赋给 color 报 "Unable to assign [undefined] to QColor"
                                    // 且底色整块不画。
                                    color: ent.on ? ThemeBridge.colors["fill.selected"]
                                                  : entArea.containsMouse
                                                    ? ThemeBridge.colors["fill.hover"]
                                                    : "transparent"
                                    Behavior on color { ColorAnimation { duration: root.durFast } }
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: root.spMd + root.spXs
                                    anchors.right: parent.right
                                    anchors.rightMargin: root.spMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: ent.modelData.name
                                    color: ent.on ? ThemeBridge.colors["accent.primary"]
                                                  : ThemeBridge.colors["text.primary"]
                                    font.family: ThemeBridge.fontFamily
                                    font.pixelSize: ThemeBridge.baseFontPx
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    id: entArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        Page.selectEntity(ent.modelData.id)
                                        root.ensureTop()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 空态：没有书 / 没有实体时给一句话，不是空白
    Empty {
        anchors.fill: parent
        visible: Page.entityGroups.length === 0
        title: Page.openError ? "读取失败" : "没有实体"
        text: Page.openError ? Page.openError : "打开书库后，这里按人物 / 地点 / 物品 / 势力分组。"
    }
}
