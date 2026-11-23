// shine::pages —— KPI 卡的本体（`views.css:409-456` 那 48 行 CSS 的 1:1 实现）
//
// 这一张卡上原先有三处偏差，**三处的形状都是「画出来了、看着也像个卡」**，
// 所以只有把设计稿的 CSS 逐条对、并且量出来才看得见：
//   1. 卡高写死 96（CSS 由内容撑开是 **115.17**）—— 三段文字被压扁 19.2px；
//   2. 柔光按 `theme::Tone` 取色（CSS 是 `var(--accent-dim)`，**五张卡同色**）、
//      圆心按 (max.x−26, min.y+26) 摆（CSS 是 (max.x−16, min.y+16)）、
//      形状是 14 环等 alpha 的径向渐变（CSS 是 blur(2px) 的近实心盘）、
//      而且没有 `overflow: hidden` 的裁剪；
//   3. `.card.hoverable` 的 hover **整条没接**（`KpiCard` 连命中测试都没有）。
#include "ui/imgui/pages/PageKpi.h"

#include <cstdio>
#include <cstdint>
#include <string>

namespace shine::pages {

using namespace shine::kit;

namespace {

// ---- `.kpi` 的纵向算式（`views.css:414-456`）----
// 行盒高 = 字号 × 1.6（`base.css:16` 的全局 line-height，`.kpi` 这一族没有覆盖它）。
// R3 起行盒由 `kit::LineBox(字号 token)` 推：三段的字号/行盒只在 token 一处定义。
// 逐项：边框 1 + padding 14 + 标签 18.4 + 4 + 数值 38.4 + 6 + 脚注 18.4 + padding 14 + 边框 1
constexpr float kBorder = 1.0f;
constexpr float kPadX = 16.0f;  // padding: 14px 16px
constexpr float kPadY = 14.0f;
constexpr float kLabelTop = kBorder + kPadY;                         // 15.0
constexpr float kLabelH = LineBox(theme::font::kLabel);              // 18.4
constexpr float kValueMarginTop = 4.0f;                              // `.k-value { margin-top: 4px }`
constexpr float kValueTop = kLabelTop + kLabelH + kValueMarginTop;    // 37.4
constexpr float kValueH = LineBox(theme::font::kValue);              // 38.4
constexpr float kFootMarginTop = 6.0f;                               // `.k-foot { margin-top: 6px }`
constexpr float kFootTop = kValueTop + kValueH + kFootMarginTop;     // 81.8
constexpr float kFootH = kLabelH;                                    // 18.4
constexpr float kUnitGap = 8.0f;                                     // `.k-value { gap: 8px }`

// 三段行盒 + padding + 边框 = 卡高。**这就是 `kKpiCardHeight` 的来源**，
// 改了任何一档字号/行高，这条断言会先红 —— 免得「卡高改了、行盒没改」。
static_assert(kFootTop + kFootH + kPadY + kBorder > kKpiCardHeight - 0.05f &&
                  kFootTop + kFootH + kPadY + kBorder < kKpiCardHeight + 0.05f,
              "KPI 卡高必须等于三段行盒 + padding + 边框（见 PageKpi.h 的算式）");

// ---- 每行的**基线**（相对卡顶）----
//
// ⚠️ 这三行**不能**用「行盒顶 + 半行距」算：Chrome 实测（无头 + 设计稿 dist 的真实 CSS，
//    用零尺寸 inline-block 探针取基线）是 **28 / 66.39 / 94.78**，而半行距那套会给
//    28.45 / 44.59 / 85.23 —— 数值行差 1.8px。原因不是取整：`.k-value` 是
//    `display:flex; align-items:baseline`，它的基线由**最高的那一项**（24px）决定，
//    不是它自己那个行盒的居中位置。所以这里存**实测基线**；上面那几个 `*Top`
//    只用来核卡高（`kFootTop + kFootH + kPadY + kBorder = 115.2`）。
constexpr float kLabelBase = 28.0f;
constexpr float kValueBase = 66.4f;  // Chrome 实测 66.390625
constexpr float kFootBase = 94.8f;   // Chrome 实测 94.78125

// ImGui 的 `Ascent` 口径与「基线 → pos.y」的换算**上移到 kit::Text**（R3 收编）：
// `kit::AscentOf` / `kit::BaselineToY` 是同一份常量（0.8017）与同一个 ceil ——
// 三行的 y 不再由本文件的本地算式决定。

// ---- `.kpi::after` 的柔光（`views.css:419-430`）----
// 90×90 圆，`right:-30px` / `top:-30px` 是相对 **padding box** 的：
//   圆心 x = (padding box 右边 − 1) + 30 − 45 = max.x − 16
//   圆心 y = (padding box 上边 + 1) − 30 + 45 = min.y + 16
// 半径 45 ⇒ 右、上各溢出 29px，靠 `.kpi { overflow:hidden }` 裁掉。
constexpr float kGlowR = 45.0f;   // 90px / 2
constexpr float kGlowCoreR = 43.0f;  // 半径 43 以内是「目标 alpha」，43→45 是那 2px 软边
// `filter: blur(2px)` 没有可用实现（本仓没有 blur API，投影那一族是同心圆角矩形逼近）。
// 近似成「近实心盘 + 2px 羽化」：**两层**同心实心圆、每层 alpha = 目标值的一半，
// 由外向内叠 ⇒ 半径 43 以内经过**两次**混合、恰好等于目标 alpha，
// 43→45 的环上只经过一次 ⇒ 半浓度，正好是一条 2px 的软边。
// ⚠️ 早先是 14 层等 alpha 铺满整个半径，叠出来的是「中心亮、边缘渐隐」的光晕 ——
//    那是 radial-gradient，不是 blur(2px)（blur 2px 的盘几乎就是硬边）。
// ⚠️ 两层**同一个颜色**、由大到小叠，这点很重要：报告给取证的「盘心色」就是把这个
//    过程按同样的 8 位混合算两遍（见下面 g_glowRgb），与屏上逐位一致。
constexpr int kGlowLayers = 2;
constexpr float kGlowOpacity = 0.7f;  // `opacity: .7`（views.css:429）

// 当帧画柔光用的**合成后**颜色（0xRRGGBB）。
// `KpiGlowRgb()` 的读数：取证要「最终落在屏上的那个色」，而柔光本色带 alpha。
std::uint32_t g_glowRgb = 0;

// `--accent-dim`（token 自带逐主题的 alpha）× `opacity: .7`，再按层数均分给每一层。
//
// ⚠️ 必须用 `WithAlphaSet`（**覆盖** alpha），不能用 `WithAlpha`（**调制** = 原 alpha × 参数）：
//    踩过一次 —— `WithAlpha` 把 0.141 又乘上 0.099 得到 0.014，再在层循环里乘一次 ⇒
//    实际 alpha ≈ 6e-5，柔光**根本没画上去**，而卡片、文字、静息截图全部正常。
//    更坏的是当时判据还报了绿（见 `ReviewScene.cpp` 里那段阈值上下界的说明）。
ImU32 GlowColor() {
    const ImU32 dim = ColorAccentDim();
    const float tokenAlpha = static_cast<float>((dim >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f;
    return WithAlphaSet(dim, tokenAlpha * kGlowOpacity / static_cast<float>(kGlowLayers));
}

// 带 alpha 的前景按 source-over 合成到不透明底上。**进出都是 ImGui 的 ImU32**。
// ⚠️ 全程走 ImGui 的 float4 往返，不手写字节位移：ImU32 的通道序是编译期宏
//    （`kit/Draw.cpp` 记过那次「手写位移把 alpha 写进红通道」）。
// ⚠️ 这个「ImU32 空间」必须与下面 `ToRgbHex` 的 `0xRRGGBB` 空间**分开**，两处混用
//    实测骗过一次：把 `ToRgbHex` 的结果当 `bg` 再喂进来，默认字节序下 R 在**最低**字节，
//    于是「盘心色」算成了 #373323（真值是 #1C333D），而屏上画的是对的 ——
//    判据读到 0 像素、报红，看着像「柔光没画」。
ImU32 OverOpaque(ImU32 fg, ImU32 bg) {
    const ImVec4 f = ImGui::ColorConvertU32ToFloat4(fg);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(bg);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(f.x * f.w + b.x * (1.0f - f.w), f.y * f.w + b.y * (1.0f - f.w),
               f.z * f.w + b.z * (1.0f - f.w), 1.0f));
}

// ImU32 → 取证要的 `0xRRGGBB`（`CountColorPixels` 比的是**帧缓冲的字节**，
// 而那一侧 R 在偏移 0 ⇒ 高位是 R）。**只用于出口，不要再把结果喂回 ImGui API。**
std::uint32_t ToRgbHex(ImU32 color) {
    const ImVec4 v = ImGui::ColorConvertU32ToFloat4(color);
    const auto byte = [](float f) { return static_cast<std::uint32_t>(f * 255.0f + 0.5f); };
    return (byte(v.x) << 16) | (byte(v.y) << 8) | byte(v.z);
}

}  // namespace

std::uint32_t KpiGlowRgb() { return g_glowRgb; }

void KpiCard(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view value,
             std::string_view unit, std::string_view footnote) {
    // ---- 命中（`.card.hoverable`，ui.css:196-200）----
    // ⚠️ 热区永远用**位移前**的矩形：`kit::CardHoverLift` 的约定写在 Widget_Card.h 里 ——
    //    命中区跟着位移会让鼠标停在卡片下缘 2px 内时 hovered 反复推出/落回。
    // id 用卡左上角坐标（与 `kit::Card` 同一套做法）：同页多张卡唯一，且不随标题变化。
    // id 在栈上拼（每帧 × 每张卡都走这里，第二十轮波 3）。
    char idBuf[48];
    const int idN =
        std::snprintf(idBuf, sizeof(idBuf), "kpi@%d,%d", static_cast<int>(bounds.min.x),
                      static_cast<int>(bounds.min.y));
    const Hit hit = detail::HitTestItemId(bounds, idN > 0 ? idBuf : "kpi@");
    const float lift = CardHoverLift(hit.hovered, /*hoverable=*/true);
    const Rect box{bounds.min.x, bounds.min.y + lift, bounds.max.x, bounds.max.y + lift};

    // hover 的三件事**必须一起**：边框 line-strong + `--shadow-1` + `translateY(-2px)`。
    // 非 hover 时档位是 `ShadowTier::None`（`.card` 本身没有投影）——早先这里两样都没给，
    // 于是「卡片标了 hoverable 却连命中都没有」。
    DrawShadowed(draw, box.min, box.max, 10.0f, ColorPanel(),
                 hit.hovered ? ColorLineStrong() : ColorLineSubtle(), kBorder,
                 hit.hovered ? theme::ShadowTier::Card : theme::ShadowTier::None);

    // ---- 柔光 ----
    const ImU32 glow = GlowColor();
    // 取证读数 = **盘心**的合成色，按绘制顺序（外圈 → 内圈）算同样的两遍混合。
    // 与屏上逐位一致（实测 #1C333D ⇒ 屏上那 150950 字节的图里正是这个值）⇒
    // 判据数这个色，才真是在数那团柔光，而不是数到某个凑巧的底色。
    g_glowRgb = ToRgbHex(OverOpaque(glow, OverOpaque(glow, ColorPanel())));

    // `overflow: hidden` 裁到 padding box（圆角裁不了 —— 本版 ImGui 没有 push clip path，
    // 与 `Art` / 真图是同一个降级）。不裁的话右、上各 29px 会画到卡片外面去。
    draw->PushClipRect(ImVec2(box.min.x + kBorder, box.min.y + kBorder),
                       ImVec2(box.max.x - kBorder, box.max.y - kBorder), true);
    const ImVec2 glowCenter(box.max.x - 16.0f, box.min.y + 16.0f);
    draw->AddCircleFilled(glowCenter, kGlowR, glow, 48);      // 外圈：只给 43→45 那圈用
    draw->AddCircleFilled(glowCenter, kGlowCoreR, glow, 48);  // 内圈：盘心再叠一层 = 目标值
    draw->PopClipRect();

    // ---- 三段文字 ----
    // R3 收编进 kit::Text：字号只传 token，基线换算走 kit::BaselineToY（同一份
    // 0.8017 常量），画与量走同一份 TextStyle —— 不会再出现「画用 A 档、量宽
    // 手写 B 档」的分叉。scale=1.0 时与原先的 AddText 逐位一致（Text.h 的契约）。
    const float textLeft = box.min.x + kPadX;

    // `.k-label`：11.5 / 700 / **text-muted** / letter-spacing .05em。
    // ⚠️ 色是 text-muted（views.css:434），不是 text-secondary —— 这两档在本仓相邻但不同，
    //    早先取错了一档（`#79859A` 画成了 `#A7B3C4`）。
    // ⚠️ `letter-spacing: .05em`（≈0.58px/字）**未接**：自绘路径没有字距，要逐字画。
    //    记在 `进度.md` 的未接清单里，别在这里假装接上了。
    const TextStyle labelStyle{.size = theme::font::kLabel, .bold = true,
                               .color = ColorTextMuted()};
    TextAtBaseline(draw, textLeft, box.min.y + kLabelBase, label, labelStyle);

    // `.k-value`：24 / 800（本仓字体只有 400/700 两档，800 落 Bold，见 Fonts.h 顶部）。
    const TextStyle valueStyle{.size = theme::font::kValue, .bold = true, .color = ColorText()};
    TextAtBaseline(draw, textLeft, box.min.y + kValueBase, value, valueStyle);
    const float valueW = TextSize(value, valueStyle).x;
    if (!unit.empty()) {
        // `.tiny.dim`（`base.css:118-120`）= 12px + text-muted，`align-items: baseline`
        // ⇒ **同一个基线**、不同的 Ascent，所以两处都从 `kValueBase` 推。
        // ⚠️ 字号是 12（不是 12.5），间距是 8（不是 4）。
        const TextStyle unitStyle{.size = theme::font::kTiny, .color = ColorTextMuted()};
        TextAtBaseline(draw, textLeft + valueW + kUnitGap, box.min.y + kValueBase, unit,
                       unitStyle);
    }

    // `.k-foot`：11.5 / text-muted。
    // ⚠️ **折行未接**：CSS 里长脚注会折行并把那一行整格撑高（`.kpis` 是 grid，行高取最高）。
    //    这里按单行画、超宽就溢到卡外。当前五条的脚注都在一行以内（最长的 146.8px < 210px
    //    可用宽），所以先按单行实现 —— 换了文案要重新看这一条。
    // ⚠️ `.k-delta`（涨跌）**不画**：设计稿有这一格、`mock.js` 也有 `delta/dir`，
    //    但业务层没有「环比」字段（`OverviewKpi.cpp` 里那条注释记的就是这件事）。
    //    宁可少一格，不编一个数。
    // ⚠️ `.k-foot` 是**常规字重**（`.k-label` 才是 700）—— 两段共用 11.5 档字号，
    //    字重不同，所以脚注单独一份 style，别顺手复用 labelStyle。
    const TextStyle footStyle{.size = theme::font::kLabel, .color = ColorTextMuted()};
    TextClipped(draw, ImVec2(textLeft, BaselineToY(box.min.y + kFootBase, footStyle.size)),
                box.max.x - textLeft - 14.0f, footnote, footStyle);
}

} // namespace shine::pages
