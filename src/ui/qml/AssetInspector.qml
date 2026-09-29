pragma ComponentBehavior: Bound
// src/ui/qml/AssetInspector.qml —— 资产详情（QML 迁移，右栏检查器）
//
// 原 Widgets 侧 AssetDetailView + AssetPolicyPanel 的可读部分，随迁移退役删除。
// 数据全部来自 C++ 注入的 `Page`（AssetPageModel），本页无 mock。
//
// 内容对应设计稿 Assets.jsx:208 起的设定集 KV（类别 / 别名 / 出处 / 降级策略），
// 外加形象层派生链（mock.js DERIVE_CHAIN 的真数据形态）与运行态。
//
// ⚠️ 一致性与参考库的**渲染位置**在本页之外：
//   一致性 → AssetsCompare.qml（内容区，Assets.qml 内）；
//   参考库 → AssetsRefLibrary.qml（同上）。
// 本页只承载设定集 KV / 形象层派生链 / 运行态这三块可读信息。
// 两者现均已接真数据（ConsistencyProbe / RefProbe 返回真值，不再是 unavailable）。
import QtQuick
import Shine 1.0

Ctl {
    id: root

    color: "transparent"

    // 间距别名：Ctl 只转发圆角/动效，不带 spaces；键是 CSS 刻度名（见 ThemeBridge.cpp）
    readonly property int spXs: ThemeBridge.spaces["1"]   // 4
    readonly property int spSm: ThemeBridge.spaces["2"]   // 8
    readonly property int spMd: ThemeBridge.spaces["3"]   // 12

    // 取证探针：与 Widgets 侧 DetailProbe() 同类，供 P05 比对
    readonly property string detailProbe: Page.assets.length === 0
        ? "detail=empty"
        : "detail=ok; asset=" + Page.selectedAssetId + "; runtime=" + (Page.runtime.none ? "none" : Page.runtime.phase)

    readonly property var current: {
        var id = Page.selectedAssetId
        for (var i = 0; i < Page.assets.length; ++i) {
            if (Page.assets[i].id === id) { return Page.assets[i] }
        }
        return Page.assets.length > 0 ? Page.assets[0] : null
    }

    Flickable {
        anchors.fill: parent
        contentHeight: body.implicitHeight + root.spMd
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body
            width: parent.width
            spacing: root.spMd
            visible: root.current !== null

            // —— 标题：资产名 + 实体 ——
            Item {
                width: parent.width
                implicitHeight: titleCol.implicitHeight
                Column {
                    id: titleCol
                    width: parent.width
                    spacing: 2
                    Text {
                        width: parent.width
                        text: root.current ? root.current.name : ""
                        color: ThemeBridge.colors["text.primary"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: ThemeBridge.baseFontPx
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: root.current ? root.current.entityName : ""
                        color: ThemeBridge.colors["text.muted"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            // —— 状态 + 降级标记 ——
            Flow {
                width: parent.width
                spacing: root.spXs
                Tag {
                    text: root.current ? root.current.statusLabel : ""
                    tone: root.current ? root.current.statusTone : "idle"
                }
                Tag {
                    visible: root.current ? root.current.degraded : false
                    text: "降级"
                    tone: "warn"
                }
            }

            // —— 设定集 KV（Assets.jsx:208 的四行）——
            // Kv 的调用契约是 `rows: [{key, value}]`（见 Kv.qml 文件头），
            // 不是 label/value 两个独立属性。
            Card {
                width: parent.width
                Column {
                    x: root.spMd
                    y: root.spMd
                    width: parent.width - root.spMd * 2
                    Kv {
                        width: parent.width
                        rows: root.current ? [
                            { key: "类别",   value: root.current.kindLabel },
                            { key: "出处",   value: root.current.role },
                            { key: "描述",   value: root.current.baseDesc },
                            { key: "定稿",   value: root.current.canonStatus }
                        ] : []
                    }
                }
            }

            // —— 形象层派生链 ——
            Text {
                width: parent.width
                text: "形象层"
                color: ThemeBridge.colors["text.secondary"]
                font.family: ThemeBridge.fontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            // StageFlow 的契约是 stages[{code,name}] + stageStates[]（**同下标**），
            // 所以要把 Page.deriveChain 的 {name,state} 拆成两条数组。
            StageFlow {
                width: parent.width
                stages: {
                    var out = []
                    for (var i = 0; i < Page.deriveChain.length; ++i) {
                        out.push({ code: "L" + (i + 1), name: Page.deriveChain[i].name })
                    }
                    return out
                }
                stageStates: {
                    var out = []
                    for (var i = 0; i < Page.deriveChain.length; ++i) {
                        out.push(Page.deriveChain[i].state)
                    }
                    return out
                }
            }

            // —— 运行态：有任务才出现 ——
            Card {
                width: parent.width
                visible: root.current && !Page.runtime.none && Page.runtime.active
                Column {
                    x: root.spMd
                    y: root.spMd
                    width: parent.width - root.spMd * 2
                    spacing: root.spSm
                    Kv {
                        width: parent.width
                        rows: [
                            { key: "阶段", value: Page.runtime.phaseLabel },
                            { key: "层",   value: Page.runtime.layer }
                        ]
                    }
                    Text {
                        width: parent.width
                        text: Page.runtime.detail
                        color: ThemeBridge.colors["text.muted"]
                        font.family: ThemeBridge.fontFamily
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // 空态：没有资产时说明原因，不留白
    Empty {
        anchors.fill: parent
        visible: root.current === null
        title: Page.hasBook ? "未选择资产" : "未打开书库"
        text: Page.openError ? Page.openError : "在左侧选一个实体，这里显示它的设定集与形象层。"
    }
}
