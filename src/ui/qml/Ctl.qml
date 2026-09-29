// src/ui/qml/Gallery/Ctl.qml —— QML 侧的控件基座
//
// 与 Widgets 侧 kit/controls 的关系：**不重写控件逻辑，只重写"外观表达"**。
// 所有色值/圆角/动效都从 ThemeBridge 桥取（= 既有 ColorToken），因此 QML 页面与
// QSS 控件在同一个主题下必然同色 —— 不会出现第二套颜色真值。
//
// ⚠️ 基类必须是 **Rectangle** 不是 Item：圆角/底色/描边这些视觉属性
// 都是 Rectangle 的，Item 上没有。子类（Card/Tag/Button）直接用 radius/color/border。
import QtQuick
import Shine 1.0

Rectangle {
    id: ctl

    // —— 与 widgets::WidgetCommon 同一批 token，QML 侧不新增刻度 ——
    readonly property int rSm: ThemeBridge.radii.sm
    readonly property int rMd: ThemeBridge.radii.md
    readonly property int rLg: ThemeBridge.radii.lg
    readonly property int rPill: ThemeBridge.radii.pill

    readonly property int durFast: ThemeBridge.durations.fast
    readonly property int durBase: ThemeBridge.durations.base
    readonly property int durSlow: ThemeBridge.durations.slow

    readonly property bool reduce: ThemeBridge.reduceMotion

    // layer.effect（MultiEffect 阴影）的总开关，由 C++ 侧探测场景图后端后告知。
    // software 后端下挂 layer 的 item 会被**整个吞掉**（不是「阴影没画出来」，
    // 是控件本身不显示）—— 实测 Button 的 primary 变体整排消失。
    // 所以这个开关必须来自 ThemeBridge 的自动探测，不能写死 true。
    readonly property bool shadows: ThemeBridge.layerEffectsAvailable

    // 「减少动效」下统一退化为瞬时：所有动画挂载前先读 reduce
    Behavior on opacity {
        NumberAnimation { duration: ctl.reduce ? 0 : ctl.durFast; easing.type: Easing.OutCubic }
    }
}
