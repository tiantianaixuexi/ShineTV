#pragma once
// shine::theme —— 设计系统 Token（ImGui 前端）
//
// 由 src/ui/kit/theme/Token.h 平移而来（refactor/phases.md P2.1）：那份是纯 std、
// 零 Qt，可近乎原样搬。唯一的改动是删掉 QSS 用的字体族串（ImGui 侧走字体图集，
// 不用 CSS font-family），以及本文件现在是 5 套主题 JSON 的唯一真值来源。
//
// 权威：webui/src/styles/tokens.css 的 :root 块（--r-* / --sp-* / --dur-* / --font-*）
// 与 5 套主题 JSON。点分名与 ColorToken 字段顺序必须逐位一致 —— ApplyTheme()
// 靠 kColorTokenNames 做一次性映射，插入会让全表错位。
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace shine::theme {

// 5 套主题（深空为默认；下标 = ThemeId 枚举值，不得重排）
enum class ThemeId {
    DeepSpace /*深空*/, Dusk /*薄暮*/, PaperInk /*纸墨*/, InkWash /*水墨*/, PolarNight /*极夜*/,
};

inline constexpr std::array<ThemeId, 5> kAllThemes = {
    ThemeId::DeepSpace, ThemeId::Dusk, ThemeId::PaperInk, ThemeId::InkWash, ThemeId::PolarNight};

[[nodiscard]] std::string_view ThemeIdKey(ThemeId id);   // 持久化用的 ASCII id
[[nodiscard]] std::string_view ThemeDisplayName(ThemeId id); // UI 显示名（含中文）
[[nodiscard]] bool ThemeIdFromKey(std::string_view key, ThemeId& out);

// ---- §2.1 + §2.2 颜色 token（字段顺序 = kColorTokenNames）----
struct ColorToken {
    // §2.1 中性色阶
    std::uint32_t bgVoid = 0;
    std::uint32_t bgSurface = 0;
    std::uint32_t bgPanel = 0;
    std::uint32_t bgElevated = 0;
    std::uint32_t bgOverlay = 0;
    std::uint32_t lineSubtle = 0;
    std::uint32_t lineNormal = 0;
    std::uint32_t lineStrong = 0;
    std::uint32_t textPrimary = 0;
    std::uint32_t textSecondary = 0;
    std::uint32_t textMuted = 0;
    std::uint32_t textInverse = 0;
    // §2.2 语义强调色
    std::uint32_t accentPrimary = 0;
    std::uint32_t accentPrimaryHover = 0;
    std::uint32_t accentPrimaryFg = 0;
    std::uint32_t accentSecondary = 0;
    std::uint32_t accentInfo = 0;
    std::uint32_t statusOk = 0;
    std::uint32_t statusWarn = 0;
    std::uint32_t statusDanger = 0;
    std::uint32_t statusBusy = 0;
    std::uint32_t statusIdle = 0;
    // §2.4 交互态专用色 + 焦点环 + 遮罩 + 阴影
    std::uint32_t statusPending = 0;
    std::uint32_t fillHover = 0;
    std::uint32_t fillSelected = 0;
    std::uint32_t fillMuted = 0;
    std::uint32_t lineFocus = 0;
    std::uint32_t shadowScrim = 0;
    std::uint32_t shadow1 = 0;
    std::uint32_t shadow2 = 0;
    std::uint32_t shadowAccent = 0;

    bool operator==(const ColorToken&) const = default;
};

// 颜色 token 点分名。顺序 == ColorToken 字段顺序 == ApplyTheme 映射顺序。
inline constexpr std::array<std::string_view, 31> kColorTokenNames = {
    "bg.void",  "bg.surface",  "bg.panel",  "bg.elevated",  "bg.overlay",
    "line.subtle",  "line.normal",  "line.strong",
    "text.primary",  "text.secondary",  "text.muted",  "text.inverse",
    "accent.primary",  "accent.primary.hover",  "accent.primary.fg",  "accent.secondary",  "accent.info",
    "status.ok",  "status.warn",  "status.danger",  "status.busy",  "status.idle",
    "status.pending",  "fill.hover",  "fill.selected",  "fill.muted",  "line.focus",  "shadow.scrim",
    "shadow.1",  "shadow.2",  "shadow.accent",
};

inline constexpr std::size_t kColorTokenCount = kColorTokenNames.size();

// ---- §2.3 形状 / 间距 / 字体 / 描边 / 动效（与主题无关）----
namespace radius {
inline constexpr int kXs = 4;   // 分段项 / 勾选框 / kbd / 卡片强调条
inline constexpr int kSm = 6;   // 按钮 / 输入框 / 菜单项 / 树节点 / tooltip
inline constexpr int kMd = 10;  // 卡片 / 分段控件容器 / 提示条 / 浮层面板
inline constexpr int kLg = 14;  // 弹窗 / 空态字形 / 浮动面板
inline constexpr int kXl = 18;  // 预留大容器档
inline constexpr int kPill = 999; // 胶囊 / 进度条 / 滚动条滑块（>半边长时 ImGui 自钳）
} // namespace radius

namespace space {
// ⚠️ 下标语义不可动：既有代码按 kSteps[i] 取值，插入会让全部既有取值漂移。
// tokens.css 的 --sp-1…--sp-7（4/8/12/16/24/32/48）对应下标 2…8。
inline constexpr std::array<int, 9> kSteps = {0, 2, 4, 8, 12, 16, 24, 32, 48};
inline constexpr int kXs = 6;   // 细间距：图标与文字、紧凑分组内
inline constexpr int kXl = 20;  // 大间距：区块之间
inline constexpr int kXxl = 40; // 超大间距：页面级留白
} // namespace space

namespace font {
// --font-ui 的族序：Segoe UI 在前（拉丁字形更中性），雅黑接管中文。
inline constexpr std::string_view kFamily = "Segoe UI, Microsoft YaHei UI, PingFang SC, system-ui";
// tokens.css:195 [data-theme="inkwash"] 覆盖的 --font-ui（衬线族）。只有水墨换。
inline constexpr std::string_view kFamilySerif = "Segoe UI, Noto Serif SC, Source Han Serif SC, SimSun, serif";
inline constexpr std::string_view kMonoFamily = "Cascadia Code, JetBrains Mono, Consolas, monospace";
// base.css 的 body 是 13.5px；QFont::setPixelSize 会把小数量化到整设备像素，
// 取整到 13 与 ui.css 里显式声明最多的控件字号（.btn/.tabs/.input/.table）同族。
inline constexpr std::array<int, 6> kSizes = {12, 13, 14, 16, 20, 28}; // xs/sm/md/lg/xl/display
inline constexpr int kBase = 13;
inline constexpr int kRegular = 400;
inline constexpr int kSemibold = 600;
} // namespace font

struct Shadow {
    int offsetX;
    int offsetY;
    int blur;
    std::uint32_t color; // RRGGBBAA
};

namespace shadow {
inline constexpr Shadow kSm{0, 1, 2, 0x00000099};  // theme-ok
inline constexpr Shadow kMd{0, 2, 8, 0x00000033};  // theme-ok
inline constexpr Shadow kLg{0, 8, 24, 0x0000004D}; // theme-ok
} // namespace shadow

namespace border {
inline constexpr int kNormal = 1;
inline constexpr int kFocus = 2;
} // namespace border

namespace motion {
inline constexpr int kDurFastMs = 120;
inline constexpr int kDurBaseMs = 200;
inline constexpr int kDurSlowMs = 320;
struct Ease {
    float x1;
    float y1;
    float x2;
    float y2;
};
inline constexpr Ease kStandard{0.2f, 0.0f, 0.0f, 1.0f};
inline constexpr Ease kEmphasized{0.3f, 0.05f, 0.15f, 1.0f};
inline constexpr Ease kExit{0.4f, 0.0f, 1.0f, 1.0f};
} // namespace motion

} // namespace shine::theme
