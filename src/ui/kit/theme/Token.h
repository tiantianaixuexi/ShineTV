#pragma once
// shine::theme::Token —— 设计系统 Token 结构（P01-S5 骨架）
//
// 点分名与语义值由本文件定义；主题/动效规则见 docs/10-modules/ui-kit.md：
//   §2.1 中性色阶 12 项 + §2.2 语义强调色 10 项 —— 颜色（RRGGBBAA）；
//   §2.3 形状 / 间距 / 字体 / 阴影 / 描边 / 动效 —— 与主题无关的几何与动效。
// 值与 4 套主题的 JSON 化在 P02-S1 落地；本文件只立结构与命名（S5 判据：与 §2 逐项对得上）。
#include <array>
#include <cstddef>
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
    // §2.4 交互态专用色 + 焦点环 + 遮罩（P01 方案 01 追加；⚠️ 只许尾部追加，见 kColorTokenCount）
    // 状态轴补到 6 态：idle 灰 / pending 蓝 / busy 紫 / ok 绿 / warn 黄 / danger 红。
    std::uint32_t statusPending = 0;      // status.pending（排队中）
    std::uint32_t fillHover = 0;          // fill.hover（行 / 卡片 / 列表项悬停底）
    std::uint32_t fillSelected = 0;       // fill.selected（选中行 / 当前 tab / 选中项底）
    std::uint32_t fillMuted = 0;          // fill.muted（斑马行、只读区、次要分区底）
    std::uint32_t lineFocus = 0;          // line.focus（焦点环；浅色主题下比主色更清楚）
    std::uint32_t shadowScrim = 0;        // shadow.scrim（浮层遮罩，带 alpha）
    // 设计稿的 box-shadow 阴影（Qt QSS 无 box-shadow，改由 widgets::ApplyShadow
    // 用 QGraphicsDropShadowEffect 落地；色值随主题变化，故进 ColorToken 而非常量）。
    // 数值逐条对齐 webui/src/styles/tokens.css 的 --shadow-1 / --shadow-2 / --shadow-accent。
    std::uint32_t shadow1 = 0;            // shadow.1（卡片 / 悬浮层 / 节点 hover）
    std::uint32_t shadow2 = 0;            // shadow.2（弹窗 / 抽屉 / 命令面板 / 浮动面板）
    std::uint32_t shadowAccent = 0;       // shadow.accent（主按钮 / 选中项的强调辉光）

    bool operator==(const ColorToken&) const = default;
};

// 颜色 token 点分名（对表 / 序列化用；顺序 = ColorToken 字段顺序）
// ⚠️ 本表顺序 == ColorToken 字段顺序 == QSS %N 顺序，三者必须逐位一致：
//    QssBuilder::FillTokens 按 "%N" 做位置替换，**中途插入会让整张 QSS 模板错位**。
//    新增 token 一律追加到末尾（design/01-tokens-color.md §1.1）。
inline constexpr std::array<std::string_view, 31> kColorTokenNames = {
    "bg.void",  "bg.surface",  "bg.panel",  "bg.elevated",  "bg.overlay",
    "line.subtle",  "line.normal",  "line.strong",
    "text.primary",  "text.secondary",  "text.muted",  "text.inverse",
    "accent.primary",  "accent.primary.hover",  "accent.primary.fg",  "accent.secondary",  "accent.info",
    "status.ok",  "status.warn",  "status.danger",  "status.busy",  "status.idle",
    "status.pending",  "fill.hover",  "fill.selected",  "fill.muted",  "line.focus",  "shadow.scrim",
    "shadow.1",  "shadow.2",  "shadow.accent",
};

// token 总数（22 → 28 → 31）。用它替代散落的字面量：值数组长度、样式编辑器行数、
// 主题完整性判定全部由它推导，下次追加 token 不必再全仓搜魔数。
inline constexpr std::size_t kColorTokenCount = kColorTokenNames.size();

// ---- §2.3 形状 / 间距 / 字体 / 阴影 / 描边 / 动效（与主题无关）----
namespace radius { // radius.xs / sm / md / lg / pill
inline constexpr int kXs = 2;   // 输入框 / 小徽标
inline constexpr int kSm = 4;   // 按钮 / 输入 / 菜单项
inline constexpr int kMd = 6;   // 按钮（lg）、分段控件、进度条
inline constexpr int kLg = 10;  // 卡片 / 弹层 / 抽屉
inline constexpr int kPill = 999;
} // namespace radius

namespace space {
inline constexpr std::array<int, 9> kSteps = {0, 2, 4, 8, 12, 16, 24, 32, 48}; // space.0…space.8
// 方案 01 补的 3 档（6 / 20 / 40）。**不进 kSteps**：kSteps 的下标被 ~60 处
// 按索引取值（kSteps[3] 等），中途插入会让全部既有取值漂移；补在数组末尾又会让
// 序列变成 0,2,4,…,48,6,20,40（非单调，后续二分/追加都会踩坑）。
// 因此新增档位一律用命名常量，既有下标语义保持不变。
inline constexpr int kXs = 6;   // 细间距：图标与文字、紧凑分组内
inline constexpr int kXl = 20;  // 大间距：区块之间（16 与 24 之间）
inline constexpr int kXxl = 40; // 超大间距：页面级留白（32 与 48 之间）
} // namespace space

namespace font {
inline constexpr std::string_view kFamily = "Microsoft YaHei UI, Segoe UI, sans-serif"; // font.family
inline constexpr std::array<int, 6> kSizes = {12, 13, 14, 16, 20, 28}; // font.size.xs/sm/md/lg/xl/display
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
