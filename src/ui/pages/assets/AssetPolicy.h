#pragma once
// ui/pages/assets/AssetPolicy —— 资产依赖策略的值类型（P05-S4）
//
// 为什么独立成文件：这张结构原先住在 `AssetPolicyPanel.h`（一个 QWidget 面板）。
// QML 迁移后 Widgets 面板退役，若把结构留在那个头里，**每一个只需要「两个开关」
// 的调用方都被迫 include 一个正在被删除的 QWidget 头**。值类型与它的 UI 分开，
// 两侧各自引用同一份定义，不会出现两套默认值。
//
// 口径：策略 C（缺依赖挂起）为主，超期后按策略 B（无参考图降级）兜底；
// allowDegrade=false 即严格模式，任何时候都只挂起。默认值 30 分钟。
//
// 见 ui/kit/images 不需要本文件；本文件无 Qt 依赖，可被 core 层安全引用。
#include <cstdint>

namespace shine::app {

struct AssetPolicy {
    std::int64_t suspendTimeoutMs = 30 * 60 * 1000;
    bool allowDegrade = true;
};

} // namespace shine::app
