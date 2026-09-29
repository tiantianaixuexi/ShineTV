#pragma once
// ui/kit/theme/ToneMix —— color-mix 的唯一实现（QSS 与 QML 两侧共用）
//
// 背景：设计稿大量使用 `color-mix(in srgb, var(--ok) 12%, transparent)`
// （webui ui.css:139-144 的 .tag tone、views.css 的 .gantt .gcell、.gates 等）。
// QSS 没有 color-mix，QML 也没有 —— 两侧都得自己算。
//
// ⚠️ **这个算法只许存在这一份**。此前 QssBuilder::FillToneMixes 有一份私有
// 实现；若 QML 桥再写一份「看起来一样」的，浮点取整、基准底色、比例任一处
// 漂移，QML 页面与 QSS 控件的同一个 tone 胶囊就会出现两种颜色 —— 而这种不一致
// 截图能看出来、极难定位。所以 QML 桥直接调用这里的函数。
//
// 比例与基准（与 QssBuilder 的 kToneBgRatio / kToneEdgeRatio 保持一致）：
//   底 = color-mix(tone 12%, <基准底色>)；边 = color-mix(tone 35%, <基准底色>)
// 基准底色取 bg.surface：CSS 第二色是 transparent，叠在标签所在容器上；
// 本仓既有约定（水墨主题预合成 line.*/fill.*）同样以 bg.surface 为基准。
#include "ui/kit/theme/Token.h"

#include <cmath>
#include <cstdint>

namespace shine::theme {

// tone 顺序 = QSS 模板里 $TONEBG$n$ / $TONEEDGE$n$ 的 n，**不可重排**
// （QssBuilder::kToneMix 与此保持同序；QML 侧按名字索引，不依赖下标）。
inline constexpr std::uint32_t kToneBgMixRatio = 12;   // 百分比
inline constexpr std::uint32_t kToneEdgeMixRatio = 35; // 百分比

// 按点分名取 tone 的 token 值；名不在 {accent,info,ok,warn,danger,busy,pending} 内返回 false
[[nodiscard]] constexpr bool ToneTokenForName(const char* name, std::size_t& out_index) noexcept {
    // 下标与 kToneMix 同序：accent/info/ok/warn/danger/busy/pending
    constexpr const char* kNames[7] = {"accent", "info", "ok", "warn", "danger", "busy", "pending"};
    for (std::size_t i = 0; i < 7; ++i) {
        std::size_t k = 0;
        const char* a = name;
        const char* b = kNames[i];
        while (a[k] != '\0' && a[k] == b[k]) {
            ++k;
        }
        if (a[k] == '\0' && b[k] == '\0') {
            out_index = i;
            return true;
        }
    }
    return false;
}

// top 按 percent 叠在 bottom 上，返回不透明的 RRGGBBAA
[[nodiscard]] constexpr std::uint32_t MixOver(std::uint32_t top, std::uint32_t bottom,
                                              std::uint32_t percent) noexcept {
    const double ratio = static_cast<double>(percent) / 100.0;
    const auto mix = [ratio](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::uint32_t>(
            static_cast<int>(std::lround(static_cast<double>(x) * ratio +
                                         static_cast<double>(y) * (1.0 - ratio))) &
            0xFF);
    };
    const std::uint32_t r = mix((top >> 24) & 0xFF, (bottom >> 24) & 0xFF);
    const std::uint32_t g = mix((top >> 16) & 0xFF, (bottom >> 16) & 0xFF);
    const std::uint32_t b = mix((top >> 8) & 0xFF, (bottom >> 8) & 0xFF);
    // alpha 一律取满：color-mix 叠在不透明底上，产物就是不透明色
    return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

} // namespace shine::theme
