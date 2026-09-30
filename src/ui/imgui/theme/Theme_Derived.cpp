// 派生色预混层：加载期把 Design 里的 color-mix() 全部算成实色。
//
// 为什么单独成文件：这一族的输入是「逐主题不同的常量表」（kGlassTriples /
// kGradEnds），输出是 Derived 结构。它是**唯一**一处「同一个视觉值在五套主题
// 下各写一遍」的地方，也是最容易被误当成可参数化而改坏的地方 —— 那两张表是
// 照抄 tokens.css 的，不是推导出来的。混在 Theme.cpp 里时它和阴影表、应用
// 逻辑挨着，看不出这三者根本不是一回事。

#include "ui/imgui/theme/Theme_Internal.h"

#include <array>
#include <cstdint>

namespace shine::theme::detail {
namespace {

// 每主题显式的 --accent-dim / --accent-glow / --glass。
// 这三个**不是**固定百分比配方，逐主题不同，只能照抄 tokens.css：
//   深空 tokens.css:65-66,64 · 薄暮 :106-107,105 · 纸墨 :147-148,146
//   水墨 :188-189,187 · 极夜 :241-242,240
struct GlassTriple {
    std::uint32_t dim;  // theme-ok
    std::uint32_t glow; // theme-ok
    std::uint32_t glass;// theme-ok
};
constexpr std::array<GlassTriple, 5> kGlassTriples{{
    {0x35D0B424u, 0x35D0B44Du, 0x101621D1u}, // 深空 .14/.30/.82
    {0xF2A65A24u, 0xF2A65A4Du, 0x1E1627D1u}, // 薄暮 .14/.30/.82
    {0x0C85771Au, 0x0C857738u, 0xF7F3EADBu}, // 纸墨 .10/.22/.86
    {0x2F2C2814u, 0x2F2C2833u, 0xF4F1E8E0u}, // 水墨 .08/.20/.88
    {0x3ECFB221u, 0x3ECFB247u, 0x080B10D9u}, // 极夜 .13/.28/.85
}};

std::array<std::uint32_t, kToneCount> ToneSource(const ColorToken& c) {
    return {c.accentPrimary, c.accentInfo, c.statusOk,  c.statusWarn,
            c.statusDanger, c.statusBusy,  c.statusIdle};
}

// --grad-accent / --grad-warm 的**终点**逐主题不同，且不是同一个 token：
//   grad-accent.to  深空:70 / 纸墨:152 / 水墨:193 / 极夜:246 = accent.info
//                   薄暮:111 = accent.secondary
//   grad-warm.to    薄暮:112 = accent.info，纸墨:153 / 水墨:194 = status.warn，
//                   深空:71 / 极夜:247 = #E86A8B
// 起点两套主题都恒等于 accent.primary / accent.secondary，所以不查表，直接从
// ColorToken 取 —— 保持单一真值。
// #E86A8B 在深空/极夜的 token 表里**没有**对应字段（它是薄暮的 --accent-2 被抄
// 过去的），只能显式给，这是设计稿自身的跨主题抄值，不是本侧的推导结果。
constexpr std::uint32_t kRoseE86A8B = 0xE86A8BFFu; // theme-ok
struct GradEnds {
    bool accentToIsSecondary; // grad-accent 终点取 accent.secondary 还是 accent.info
    int warmTo;              // 0=kRoseE86A8B, 1=accent.info, 2=status.warn
};
constexpr std::array<GradEnds, 5> kGradEnds{{
    {false, 0}, // 深空 tokens.css:70-71
    {true, 1},  // 薄暮 tokens.css:111-112
    {false, 2}, // 纸墨 tokens.css:152-153
    {false, 2}, // 水墨 tokens.css:193-194
    {false, 0}, // 极夜 tokens.css:246-247
}};

} // namespace

void ComputeDerived(ThemeRecord& record, ThemeId id) {
    const ColorToken& c = record.colors;
    Derived& d = record.derived;
    const std::array<std::uint32_t, kToneCount> tone = ToneSource(c);
    for (std::size_t i = 0; i < kToneCount; ++i) {
        d.tagBg[i] = MixAlpha(tone[i], 12.0f);
        d.tagBorder[i] = MixAlpha(tone[i], 35.0f);
        d.gateBg[i] = MixAlpha(tone[i], 10.0f);
        d.ganttCell[i] = MixAlpha(tone[i], 12.0f);
        d.checkRowBg[i] = MixAlpha(tone[i], 10.0f);
    }
    d.stageRunBg = MixAlpha(c.accentPrimary, 14.0f);
    d.jumpBtnBg = MixAlpha(c.accentPrimary, 22.0f);
    d.inputFocusRing = MixAlpha(c.accentPrimary, 14.0f);
    d.btnPrimaryShadow = MixAlpha(c.accentPrimary, 28.0f);
    d.dangerBg = MixAlpha(c.statusDanger, 12.0f);

    // 13 档 alpha × 7 色调全矩阵：设计稿里全部 color-mix(·, N%, transparent)。
    for (std::size_t s = 0; s < alpha::kStepCount; ++s) {
        for (std::size_t t = 0; t < kToneCount; ++t) {
            d.toneAlpha[s][t] = MixAlpha(tone[t], alpha::kPercents[s]);
        }
    }

    // 实色预混（color-mix 两端都是不透明基色 → 结果不透明）。
    d.crumbBg = MixSrgb(c.bgVoid, c.bgSurface, 60.0f);
    d.stageDoneBg = MixSrgb(c.statusOk, c.fillMuted, 10.0f);
    d.stageFailBg = MixSrgb(c.statusDanger, c.fillMuted, 10.0f);
    d.gateFailBg = MixSrgb(c.statusDanger, c.fillMuted, 7.0f);

    // 主题无关固定叠层：设计稿写死 rgba(255,255,255,·)/rgba(0,0,0,·)，不跟主题走。
    d.btnPrimaryInset = MixAlpha(0xFFFFFFFFu, 12.0f);
    d.progShimmer = MixAlpha(0xFFFFFFFFu, 35.0f);
    d.mediaScrim45 = MixAlpha(0x000000FFu, 45.0f);
    d.mediaScrim35 = MixAlpha(0x000000FFu, 35.0f);

    // 渐变两端实色。起点恒为 accent.primary / accent.secondary，终点查 kGradEnds。
    const GradEnds& ends = kGradEnds[IndexOf(id)];
    d.gradAccent.from = c.accentPrimary;
    d.gradAccent.to = ends.accentToIsSecondary ? c.accentSecondary : c.accentInfo;
    d.gradWarm.from = c.accentSecondary;
    d.gradWarm.to = ends.warmTo == 0 ? kRoseE86A8B
                                     : (ends.warmTo == 1 ? c.accentInfo : c.statusWarn);

    const GlassTriple& triple = kGlassTriples[IndexOf(id)];
    d.accentDim = triple.dim;
    d.accentGlow = triple.glow;
    d.glass = triple.glass;
}

} // namespace shine::theme::detail
