#pragma once
// shine::theme::Token —— 设计系统 Token 结构（P01-S5 骨架）
//
// 点分名与语义值由本文件定义；主题/动效规则见 docs/10-modules/ui-kit.md：
//   §2.1 中性色阶 12 项 + §2.2 语义强调色 10 项 —— 颜色（RRGGBBAA）；
//   §2.3 形状 / 间距 / 字体 / 阴影 / 描边 / 动效 —— 与主题无关的几何与动效。
// 值与 4 套主题的 JSON 化在 P02-S1 落地；本文件只立结构与命名（S5 判据：与 §2 逐项对得上）。
#include <array>
#include <cstdint>
#include <string_view>

namespace shine::theme {

// 4 套主题（总纲 §2 列头顺序；深空为默认）
enum class ThemeId { DeepSpace /*深空*/, Dusk /*薄暮*/, PaperInk /*纸墨*/, PolarNight /*极夜*/ };

// ---- §2.1 + §2.2 颜色 token（字段顺序 = kColorTokenNames）----
struct ColorToken {
    // §2.1 中性色阶（语义层）
    std::uint32_t bgVoid = 0;        // bg.void
    std::uint32_t bgSurface = 0;     // bg.surface
    std::uint32_t bgPanel = 0;       // bg.panel
    std::uint32_t bgElevated = 0;    // bg.elevated
    std::uint32_t bgOverlay = 0;     // bg.overlay
    std::uint32_t lineSubtle = 0;    // line.subtle
    std::uint32_t lineNormal = 0;    // line.normal
    std::uint32_t lineStrong = 0;    // line.strong
    std::uint32_t textPrimary = 0;   // text.primary
    std::uint32_t textSecondary = 0; // text.secondary
    std::uint32_t textMuted = 0;     // text.muted
    std::uint32_t textInverse = 0;   // text.inverse
    // §2.2 语义强调色
    std::uint32_t accentPrimary = 0;      // accent.primary
    std::uint32_t accentPrimaryHover = 0; // accent.primary.hover
    std::uint32_t accentPrimaryFg = 0;    // accent.primary.fg
    std::uint32_t accentSecondary = 0;    // accent.secondary
    std::uint32_t accentInfo = 0;         // accent.info
    std::uint32_t statusOk = 0;           // status.ok
    std::uint32_t statusWarn = 0;         // status.warn
    std::uint32_t statusDanger = 0;       // status.danger
    std::uint32_t statusBusy = 0;         // status.busy
    std::uint32_t statusIdle = 0;         // status.idle

    bool operator==(const ColorToken&) const = default;
};

// 颜色 token 点分名（对表 / 序列化用；顺序 = ColorToken 字段顺序）
inline constexpr std::array<std::string_view, 22> kColorTokenNames = {
    "bg.void",  "bg.surface",  "bg.panel",  "bg.elevated",  "bg.overlay",
    "line.subtle",  "line.normal",  "line.strong",
    "text.primary",  "text.secondary",  "text.muted",  "text.inverse",
    "accent.primary",  "accent.primary.hover",  "accent.primary.fg",  "accent.secondary",  "accent.info",
    "status.ok",  "status.warn",  "status.danger",  "status.busy",  "status.idle",
};

// ---- §2.3 形状 / 间距 / 字体 / 阴影 / 描边 / 动效（与主题无关）----
namespace radius { // radius.xs / sm / md / lg / pill
inline constexpr int kXs = 3;
inline constexpr int kSm = 5;
inline constexpr int kMd = 8;
inline constexpr int kLg = 12;
inline constexpr int kPill = 999;
} // namespace radius

namespace space { // space.0 … space.8
inline constexpr std::array<int, 9> kSteps = {0, 2, 4, 8, 12, 16, 24, 32, 48};
} // namespace space

namespace font {
inline constexpr std::string_view kFamily = "Microsoft YaHei UI, Segoe UI, sans-serif"; // font.family
inline constexpr std::array<int, 6> kSizes = {11, 12, 13, 15, 18, 26}; // font.size.xs/sm/md/lg/xl/display
inline constexpr int kRegular = 400;  // font.weight.regular
inline constexpr int kSemibold = 600; // font.weight.semibold
} // namespace font

struct Shadow {
    int offsetX;
    int offsetY;
    int blur;
    std::uint32_t color; // RRGGBBAA
};

namespace shadow { // shadow.sm / md / lg
inline constexpr Shadow kSm{0, 1, 2, 0x00000099};  // 0 1 2 bg.overlay
inline constexpr Shadow kMd{0, 2, 8, 0x00000033};  // 0 2 8 #00000033
inline constexpr Shadow kLg{0, 8, 24, 0x0000004D}; // 0 8 24 #0000004D
} // namespace shadow

namespace border { // border.width.normal / focus
inline constexpr int kNormal = 1;
inline constexpr int kFocus = 2;
} // namespace border

namespace motion {
inline constexpr int kDurFastMs = 120;  // motion.dur.fast
inline constexpr int kDurBaseMs = 200;  // motion.dur.base
inline constexpr int kDurSlowMs = 320;  // motion.dur.slow
struct Ease {
    float x1;
    float y1;
    float x2;
    float y2;
};
inline constexpr Ease kStandard{0.2f, 0.0f, 0.0f, 1.0f};    // motion.ease.standard
inline constexpr Ease kEmphasized{0.3f, 0.05f, 0.15f, 1.0f}; // motion.ease.emphasized
inline constexpr Ease kExit{0.4f, 0.0f, 1.0f, 1.0f};         // motion.ease.exit
} // namespace motion

} // namespace shine::theme
