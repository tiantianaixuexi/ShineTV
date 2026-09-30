#pragma once
// shine::theme —— 主题运行时（5 套 JSON + 派生色 + ImGuiStyle 生成 + 持久化）
//
// 对应 refactor/phases.md P2.1–P2.4、P2.6。关键约定：
//   * 5 个 Themes/*.json 原地复用，格式不改（{"name":…, "colors":{点分名:"#RRGGBBAA"}}）。
//   * 31 个 ColorToken → ImGuiStyle 的映射**一次写全**（ApplyTheme 内），
//     不许用循环凑 —— 漏一项就是某个控件在某主题下颜色错，而且不报错。
//   * color-mix() 的 12 组派生色在主题加载时预混（见 Derived）。
#include "ui/imgui/theme/Tokens.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::theme {

// ---- 色值工具（0xRRGGBBAA ↔ ImVec4 / ImU32）----
// 内部存储是 JSON 的自然序：0xRRGGBBAA，**alpha 在最低字节**。
[[nodiscard]] ImVec4 Rgba(std::uint32_t rgba);
[[nodiscard]] std::uint32_t PackRgba(const ImVec4& rgba);

// theme 存储序 → ImGui 的 ImU32。**画任何东西都必须走这个**。
//
// 两套字节序不一样：theme 是 0xRRGGBBAA（R 在最高字节），ImGui 默认打包是
// A<<24 | B<<16 | G<<8 | R（R 在最低字节）。直接把手上的 theme 值当 ImU32 递给
// ImGui 等于把整个界面的颜色通道旋转一次。
// 实测症状：深色低饱和的主题色旋转后仍像「偏暖的深色」，一眼看不出来；
// 只有高饱和不透明填充（主按钮渐变）才当场炸成灰紫/亮蓝，28% 的 accent 光环
// 变成橄榄褐。别靠「看起来还行」判断。
[[nodiscard]] ImU32 ToImU32(std::uint32_t rgba);

// color-mix(in srgb, X N%, transparent) 的等价物：保留 X 的 RGB，alpha = N%。
[[nodiscard]] std::uint32_t MixAlpha(std::uint32_t rgb, float percent);

// 同上，档位取自 Tokens.h 的 alpha::Step（页面层不要自己写 0.18f）。
[[nodiscard]] std::uint32_t MixAlpha(std::uint32_t rgb, alpha::Step step);

// color-mix(in srgb, A N%, B)：**两色真混合**，结果是实色（不透明）。
//
// gamma 提醒：CSS Color 5 对 in srgb 的定义是在**预乘 alpha 后的 gamma 编码 sRGB
// 通道**上插值，不是线性光插值。A、B 都不透明时退化成通道直线 lerp；本项目所有
// 调用点（crumbs / 阶段节点底 / 闸门底）的两端都是不透明基色，所以用直线即可，
// 但函数按 spec 走预乘公式，以免将来有人传半透明色进来时静默算错。
// 用法：MixSrgb(c.statusOk, c.fillMuted, 10.0f)  ≡ ui.css:852 的
//      color-mix(in srgb, var(--ok) 10%, var(--fill-muted))
[[nodiscard]] std::uint32_t MixSrgb(std::uint32_t a, std::uint32_t b, float percentOfA);

// 裸 rgba(R,G,B,N) 压到不透明底色上（CSS 的 source-over 直通混合，非预乘）。
//
// 存在的理由：themes/*.json 里的 line.* / fill.* **已经是这么压平的实色**，
// 探针反证 21/21 吻合 —— 例如深空 line.strong = #414853 恰是
// rgba(234,240,247,0.22) 压 bg.surface #111825。压平时 percent 必须用 CSS 的
// **标称** 0.22，不是量化后的 56/255=0.2196（差 1/255 会让整档偏一格）。
[[nodiscard]] std::uint32_t FlattenOver(std::uint32_t translucent, std::uint32_t opaqueBackdrop,
                                       float percent);

// 7 色调（ui.css:139-145 的 .tag.ok/.warn/.danger/.busy/.info/.accent/.idle）
enum class Tone { Accent = 0, Info, Ok, Warn, Danger, Busy, Idle };
inline constexpr std::size_t kToneCount = 7;

// 派生色：ImGui 没有 color-mix 助手，淡底/淡边必须在主题加载时算好。
struct Derived {
    std::array<std::uint32_t, kToneCount> tagBg;      // tone 12%
    std::array<std::uint32_t, kToneCount> tagBorder;  // tone 35%
    std::array<std::uint32_t, kToneCount> gateBg;     // tone 10%（见下方过时说明）
    std::array<std::uint32_t, kToneCount> ganttCell;  // tone 12%（设计稿无此用法，见下）
    std::array<std::uint32_t, kToneCount> checkRowBg; // tone 10%（见下方过时说明）
    std::uint32_t stageRunBg;      // accent 14%
    std::uint32_t jumpBtnBg;       // accent 22%
    std::uint32_t inputFocusRing;  // accent 14%
    std::uint32_t btnPrimaryShadow;// accent 28%
    std::uint32_t dangerBg;        // danger 12%
    std::uint32_t accentDim;       // 每主题显式值（tokens.css 的 --accent-dim）
    std::uint32_t accentGlow;      // 每主题显式值（tokens.css 的 --accent-glow）
    std::uint32_t glass;           // 每主题显式值（--glass；毛玻璃降级用的实色）

    // ---- 以下为 color-mix 派生色补齐（只增；上面 14 个字段语义不动）----

    // [step][tone] = color-mix(in srgb, tone N%, transparent)，N 取自 alpha::Step。
    // 覆盖设计稿全部 13 档 × 7 色调；页面层要「某色调某档」一律走 At()，
    // 别再写 WithAlpha(ColorX(), 0.18f)。
    std::array<std::array<std::uint32_t, kToneCount>, alpha::kStepCount> toneAlpha{};

    // 实色预混：两端都是不透明基色，结果不透明。跟 toneAlpha 是两种算法，别混用。
    std::uint32_t crumbBg;      // shell.css:233  MixSrgb(bg-void 60%, bg-surface)
    std::uint32_t stageDoneBg;  // ui.css:852     MixSrgb(ok 10%, fill-muted)
    std::uint32_t stageFailBg;  // ui.css:869     MixSrgb(danger 10%, fill-muted)
    std::uint32_t gateFailBg;   // views.css:660  MixSrgb(danger 7%, fill-muted)

    // 主题无关的固定叠层：设计稿写死 rgba(255,255,255,·) / rgba(0,0,0,·)，不跟主题走。
    std::uint32_t btnPrimaryInset; // ui.css:54  .btn-primary 内侧高光  白 12%
    std::uint32_t progShimmer;     // ui.css:448 .prog.run 走马灯       白 35%
    std::uint32_t mediaScrim45;    // ImageFlow.jsx:159 媒体失败遮罩   黑 45%
    std::uint32_t mediaScrim35;    // VideoFlow.jsx:151 媒体失败遮罩   黑 35%

    // --grad-accent / --grad-warm 两端实色（tokens.css 里是 linear-gradient(120deg,·,·)，
    // ImGui 侧拆成两个端点自己画渐变）。from 恒等于 accent.primary / accent.secondary；
    // to 逐主题不同，见 Theme.cpp 的 kGradEnds。
    struct Gradient {
        std::uint32_t from;
        std::uint32_t to;
    };
    Gradient gradAccent;
    Gradient gradWarm;

    [[nodiscard]] std::uint32_t At(Tone tone, alpha::Step step) const {
        return toneAlpha[static_cast<std::size_t>(step)][static_cast<std::size_t>(tone)];
    }
};

// ⚠️ gateBg / checkRowBg 的已知偏差（**保持原值不动**，只是记下来）：
//   设计稿 ui.css:852/869/660 的背景是 color-mix(in srgb, 色调 N%, var(--fill-muted))，
//   两端都是不透明色 → 结果是实色。这两个字段当年按「纯 alpha」算（MixAlpha），
//   与设计稿的实色差一个底。要 1:1 就用上面新加的 stageDoneBg / stageFailBg /
//   gateFailBg。这三个旧字段已冻结，不改值也不删除。

// ---- 载入 / 查询 ----
[[nodiscard]] bool LoadThemesFrom(const std::filesystem::path& dir);
[[nodiscard]] const ColorToken& ThemeColorsOf(ThemeId id);
[[nodiscard]] const Derived& ThemeDerivedOf(ThemeId id);
[[nodiscard]] bool ThemesLoaded();

// 某一档阴影的几何规格（逐主题）。**色相**不在这里 —— 取 ColorToken 的
// shadow1 / shadow2 / shadowAccent；这里只给「几何 + 逐层 alpha」，因为
// CSS 的 box-shadow 两者都随主题变，而主题 JSON 只存了色值。
[[nodiscard]] const ShadowSpec& ShadowSpecOf(ThemeId id, ShadowTier tier);
[[nodiscard]] const ShadowSpec& CurrentShadowSpec(ShadowTier tier);
// 色相取主题 JSON 的 shadow.1 / 2 / accent（已逐条核对过五套主题与 CSS 一致）。
[[nodiscard]] std::uint32_t ShadowTokenOf(ThemeId id, ShadowTier tier);
[[nodiscard]] std::uint32_t CurrentShadowToken(ShadowTier tier);

[[nodiscard]] ThemeId CurrentThemeId() noexcept;
void SetCurrentTheme(ThemeId id) noexcept;
[[nodiscard]] const ColorToken& Current();
[[nodiscard]] const Derived& CurrentDerived();

// 每主题字体族：只有水墨换衬线族（tokens.css:195）。
[[nodiscard]] std::string_view ThemeFontFamilyOf(ThemeId id);
[[nodiscard]] bool ThemeUsesSerif(ThemeId id);

// ---- 应用到 ImGui ----
void ApplyTheme(ThemeId id);      // 换 ImGuiStyle + 记下当前主题
void ApplyCurrentTheme();          // 当前主题重铺（P0.4 初始化 / 字体图集重建后）

// 主题无关几何（design-spec §1）：写进 ImGuiStyle 的圆角/间距/边框档。
void ApplyGeometry(ImGuiStyle& style);

// 按下标读写单个 token（样式编辑器用）。与 kColorTokenNames 同序，共用 Theme.cpp 里
// 那张 kColorTokenCount 大小的字段表 —— 不另写一份映射，避免两处漂移。
[[nodiscard]] std::uint32_t TokenValue(const ColorToken& c, std::size_t index);
[[nodiscard]] std::uint32_t* TokenSlot(ColorToken& c, std::size_t index);

// ---- 持久化（webui 没有，Qt 侧有 theme.json → 保留 Qt 的行为）----
[[nodiscard]] bool LoadPersistedTheme(const std::filesystem::path& file);
[[nodiscard]] bool PersistTheme(const std::filesystem::path& file);
[[nodiscard]] std::filesystem::path DefaultThemeFile();

// ---- 自检（SHINE_THEME_SET / SHINE_THEME_SELFTEST）----
[[nodiscard]] bool SelfTestRoundTrip(std::string* report);

} // namespace shine::theme
