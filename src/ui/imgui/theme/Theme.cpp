#include "ui/imgui/theme/Theme.h"

#include "core/Log.h"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace shine::theme {
namespace {

struct ThemeRecord {
    ColorToken colors;
    Derived derived;
    bool loaded = false;
};

std::array<ThemeRecord, 5> g_themes{};
ThemeId g_current = ThemeId::DeepSpace;
bool g_loaded = false;

constexpr std::size_t IndexOf(ThemeId id) { return static_cast<std::size_t>(id); }

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

// #RRGGBB / #RRGGBBAA → 0xRRGGBBAA（6 位按不透明处理，与 Qt 侧契约一致）
bool ParseHexColor(std::string_view text, std::uint32_t& out) {
    if (text.size() < 7 || text.front() != '#') {
        return false;
    }
    std::uint32_t value = 0;
    std::size_t digits = 0;
    for (std::size_t i = 1; i < text.size() && i <= 8; ++i) {
        const char c = text[i];
        std::uint32_t nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            break;
        }
        value = (value << 4) | nibble;
        ++digits;
    }
    if (digits != 6 && digits != 8) {
        return false;
    }
    out = (digits == 6) ? (value << 8 | 0xFFu) : value;
    return true;
}

// 点分名 → ColorToken 字段。31 项逐个列出：这张表就是「31 项一次写全」的落点，
// 与 kColorTokenNames 同序，新增 token 必须同时加到这里。
bool AssignByName(std::string_view name, std::uint32_t value, ColorToken& out) {
    const std::array<std::uint32_t ColorToken::*, kColorTokenCount> kFields = {
        &ColorToken::bgVoid, &ColorToken::bgSurface, &ColorToken::bgPanel, &ColorToken::bgElevated,
        &ColorToken::bgOverlay, &ColorToken::lineSubtle, &ColorToken::lineNormal,
        &ColorToken::lineStrong, &ColorToken::textPrimary, &ColorToken::textSecondary,
        &ColorToken::textMuted, &ColorToken::textInverse, &ColorToken::accentPrimary,
        &ColorToken::accentPrimaryHover, &ColorToken::accentPrimaryFg, &ColorToken::accentSecondary,
        &ColorToken::accentInfo, &ColorToken::statusOk, &ColorToken::statusWarn,
        &ColorToken::statusDanger, &ColorToken::statusBusy, &ColorToken::statusIdle,
        &ColorToken::statusPending, &ColorToken::fillHover, &ColorToken::fillSelected,
        &ColorToken::fillMuted, &ColorToken::lineFocus, &ColorToken::shadowScrim,
        &ColorToken::shadow1, &ColorToken::shadow2, &ColorToken::shadowAccent,
    };
    for (std::size_t i = 0; i < kColorTokenCount; ++i) {
        if (kColorTokenNames[i] == name) {
            out.*(kFields[i]) = value;
            return true;
        }
    }
    return false;
}

std::string ToHex(std::uint32_t rgba) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%08X", rgba);
    return buffer;
}

bool ParseThemeFile(const std::filesystem::path& path, ThemeId id) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        shine::log::Error("theme: cannot open {}", path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        shine::log::Error("theme: {} is not valid JSON", path.string());
        return false;
    }

    ThemeRecord record;
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* colors = yyjson_obj_get(root, "colors");
    int hits = 0;
    if (yyjson_is_obj(colors)) {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(colors, &iter);
        yyjson_val* key = nullptr;
        yyjson_val* value = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            value = yyjson_obj_iter_get_val(key);
            const char* raw = yyjson_get_str(value);
            const char* keyText = yyjson_get_str(key);
            if (raw == nullptr || keyText == nullptr) {
                continue;
            }
            std::uint32_t rgba = 0;
            if (!ParseHexColor(raw, rgba)) {
                continue;
            }
            const std::string_view name(keyText, yyjson_get_len(key));
            if (AssignByName(name, rgba, record.colors)) {
                ++hits;
            }
        }
    }
    yyjson_doc_free(doc);

    if (hits != static_cast<int>(kColorTokenCount)) {
        shine::log::Error("theme: {} has {}/{} tokens", path.string(), hits, kColorTokenCount);
        return false;
    }
    record.loaded = true;
    g_themes[IndexOf(id)] = record;
    return true;
}

} // namespace

ImVec4 Rgba(std::uint32_t rgba) {
    return ImVec4(static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f,
                  static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f,
                  static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(rgba & 0xFFu) / 255.0f);
}

std::uint32_t PackRgba(const ImVec4& rgba) {
    const auto channel = [](float v) -> std::uint32_t {
        const float clamped = std::clamp(v, 0.0f, 1.0f);
        return static_cast<std::uint32_t>(std::lround(clamped * 255.0f)) & 0xFFu;
    };
    return (channel(rgba.x) << 24) | (channel(rgba.y) << 16) | (channel(rgba.z) << 8) |
           channel(rgba.w);
}

std::uint32_t MixAlpha(std::uint32_t rgb, float percent) {
    const std::uint32_t alpha =
        static_cast<std::uint32_t>(std::lround(std::clamp(percent, 0.0f, 100.0f) * 2.55f)) & 0xFFu;
    return (rgb & 0xFFFFFF00u) | alpha;
}

std::uint32_t MixAlpha(std::uint32_t rgb, alpha::Step step) {
    return MixAlpha(rgb, alpha::kPercents[static_cast<std::size_t>(step)]);
}

// color-mix(in srgb, A N%, B) —— CSS Color 5 的定义是**预乘 alpha**后在 gamma 编码
// 的 sRGB 通道上插值，不是线性光。两端都不透明时它退化成通道直线 lerp；本项目所有
// 调用点的两端都是不透明基色。走 spec 公式而不是直接 lerp，是为了将来传半透明色进来
// 时不会静默算错。
std::uint32_t MixSrgb(std::uint32_t a, std::uint32_t b, float percentOfA) {
    const double t = static_cast<double>(std::clamp(percentOfA, 0.0f, 100.0f)) / 100.0;
    const double alphaA = static_cast<double>(a & 0xFFu);
    const double alphaB = static_cast<double>(b & 0xFFu);
    const double alphaOut = t * alphaA + (1.0 - t) * alphaB;
    if (alphaOut <= 0.0) {
        return 0u;
    }
    const auto channel = [t, alphaA, alphaB, alphaOut, a, b](int shift) -> std::uint32_t {
        const double valueA = static_cast<double>((a >> shift) & 0xFFu);
        const double valueB = static_cast<double>((b >> shift) & 0xFFu);
        const double premultiplied = t * alphaA * valueA + (1.0 - t) * alphaB * valueB;
        const double straight = std::clamp(premultiplied / alphaOut, 0.0, 255.0);
        return static_cast<std::uint32_t>(std::lround(straight)) & 0xFFu;
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

// 裸 rgba() 压平：source-over 直通混合（非预乘）。percent 用 CSS 标称值，不是 alpha 字节。
std::uint32_t FlattenOver(std::uint32_t translucent, std::uint32_t opaqueBackdrop, float percent) {
    const double t = static_cast<double>(std::clamp(percent, 0.0f, 100.0f)) / 100.0;
    const auto channel = [t](std::uint32_t top, std::uint32_t bottom, int shift) -> std::uint32_t {
        const double a = static_cast<double>((top >> shift) & 0xFFu);
        const double b = static_cast<double>((bottom >> shift) & 0xFFu);
        return static_cast<std::uint32_t>(std::lround(a * t + b * (1.0 - t))) & 0xFFu;
    };
    return (channel(translucent, opaqueBackdrop, 24) << 24) |
           (channel(translucent, opaqueBackdrop, 16) << 16) |
           (channel(translucent, opaqueBackdrop, 8) << 8) | 0xFFu;
}

ImU32 ToImU32(std::uint32_t rgba) {
    // 唯一一处「theme 存储序 → ImGui 打包序」的转换。走 ImGui 自己的 float4 往返，
    // 不手写位移：位移写错过一次，错的是整个界面而不是一个控件。
    return ImGui::ColorConvertFloat4ToU32(Rgba(rgba));
}

// 字段表抽成函数：AssignByName / PersistTheme / 样式编辑器三处共用一份，
// 少一份就少一处漂移风险。顺序 == kColorTokenNames。
const std::array<std::uint32_t ColorToken::*, kColorTokenCount>& TokenFields() {
    static const std::array<std::uint32_t ColorToken::*, kColorTokenCount> fields = {
        &ColorToken::bgVoid, &ColorToken::bgSurface, &ColorToken::bgPanel, &ColorToken::bgElevated,
        &ColorToken::bgOverlay, &ColorToken::lineSubtle, &ColorToken::lineNormal,
        &ColorToken::lineStrong, &ColorToken::textPrimary, &ColorToken::textSecondary,
        &ColorToken::textMuted, &ColorToken::textInverse, &ColorToken::accentPrimary,
        &ColorToken::accentPrimaryHover, &ColorToken::accentPrimaryFg, &ColorToken::accentSecondary,
        &ColorToken::accentInfo, &ColorToken::statusOk, &ColorToken::statusWarn,
        &ColorToken::statusDanger, &ColorToken::statusBusy, &ColorToken::statusIdle,
        &ColorToken::statusPending, &ColorToken::fillHover, &ColorToken::fillSelected,
        &ColorToken::fillMuted, &ColorToken::lineFocus, &ColorToken::shadowScrim,
        &ColorToken::shadow1, &ColorToken::shadow2, &ColorToken::shadowAccent,
    };
    return fields;
}

std::uint32_t TokenValue(const ColorToken& c, std::size_t index) {
    return index < kColorTokenCount ? c.*(TokenFields()[index]) : 0u;
}

std::uint32_t* TokenSlot(ColorToken& c, std::size_t index) {
    return index < kColorTokenCount ? &(c.*(TokenFields()[index])) : nullptr;
}
std::string_view ThemeIdKey(ThemeId id) {    switch (id) {
    case ThemeId::DeepSpace: return "deepspace";
    case ThemeId::Dusk: return "dusk";
    case ThemeId::PaperInk: return "paperink";
    case ThemeId::InkWash: return "inkwash";
    case ThemeId::PolarNight: return "polarnight";
    }
    return "deepspace";
}

std::string_view ThemeDisplayName(ThemeId id) {
    switch (id) {
    case ThemeId::DeepSpace: return "深空";
    case ThemeId::Dusk: return "薄暮";
    case ThemeId::PaperInk: return "纸墨";
    case ThemeId::InkWash: return "水墨";
    case ThemeId::PolarNight: return "极夜";
    }
    return "深空";
}

bool ThemeIdFromKey(std::string_view key, ThemeId& out) {
    for (ThemeId id : kAllThemes) {
        if (ThemeIdKey(id) == key) {
            out = id;
            return true;
        }
    }
    return false;
}

bool LoadThemesFrom(const std::filesystem::path& dir) {
    bool all = true;
    for (ThemeId id : kAllThemes) {
        const std::string_view display = ThemeDisplayName(id);
        const std::filesystem::path file =
            dir / (std::string(display) + ".json"); // theme-ok
        // 逐主题算一遍派生色：kGlassTriples / kGradEnds 都按 ThemeId 寻址。
        if (ParseThemeFile(file, id)) {
            ComputeDerived(g_themes[IndexOf(id)], id);
        } else {
            all = false;
        }
    }
    g_loaded = all;
    return all;
}

bool ThemesLoaded() { return g_loaded; }

const ColorToken& ThemeColorsOf(ThemeId id) { return g_themes[IndexOf(id)].colors; }
const Derived& ThemeDerivedOf(ThemeId id) { return g_themes[IndexOf(id)].derived; }

ThemeId CurrentThemeId() noexcept { return g_current; }
void SetCurrentTheme(ThemeId id) noexcept { g_current = id; }
const ColorToken& Current() { return ThemeColorsOf(g_current); }
const Derived& CurrentDerived() { return ThemeDerivedOf(g_current); }

std::string_view ThemeFontFamilyOf(ThemeId id) {
    // 只有水墨覆盖 --font-ui（tokens.css:195）。
    return id == ThemeId::InkWash ? font::kFamilySerif : font::kFamily;
}
bool ThemeUsesSerif(ThemeId id) { return id == ThemeId::InkWash; }

void ApplyGeometry(ImGuiStyle& style) {
    // design-spec §1：控件/输入框/按钮 = r-sm(6)，卡片/面板 = r-md(10)，
    // 弹窗/浮层 = r-lg(14)，胶囊 = r-pill，小构件 = r-xs(4)。
    style.WindowRounding = static_cast<float>(radius::kMd);
    style.ChildRounding = static_cast<float>(radius::kMd);
    style.PopupRounding = static_cast<float>(radius::kLg);
    style.FrameRounding = static_cast<float>(radius::kSm);
    style.GrabRounding = static_cast<float>(radius::kSm);
    style.TabRounding = static_cast<float>(radius::kXs);
    style.ScrollbarRounding = static_cast<float>(radius::kPill);

    // ui.css 的 .input / .btn 高度 30 → FramePadding 竖向 (30 - 字号 - 2*border)/2 ≈ 7。
    style.WindowPadding = ImVec2(space::kSteps[5], space::kSteps[5]); // 16,16
    style.FramePadding = ImVec2(space::kSteps[4], space::kSteps[3]);  // 12,8
    style.ItemSpacing = ImVec2(space::kSteps[3], space::kSteps[3]);    // 8,8
    style.ItemInnerSpacing = ImVec2(space::kSteps[3], space::kSteps[3]);
    style.CellPadding = ImVec2(space::kSteps[3], space::kSteps[2]);    // 8,4
    style.IndentSpacing = space::kSteps[5];
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;

    // ImGui 默认的窗口/控件底色全部由我们接管：外壳是自绘的，ImGui 窗口只做容器。
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = border::kNormal;
    style.PopupBorderSize = border::kNormal;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.Alpha = 1.0f;
}

void ApplyTheme(ThemeId id) {
    g_current = id;
    if (!g_themes[IndexOf(id)].loaded) {
        return;
    }
    const ColorToken& c = Current();
    const Derived& d = CurrentDerived();
    ImGuiStyle& style = ImGui::GetStyle();
    ApplyGeometry(style);

    // ---- 31 项一次写全：ImGuiCol_* 与 ColorToken 字段的对应表 ----
    // 不许用循环凑：漏一项 = 某个控件在某主题下颜色错，且不报错。
    ImGui::StyleColorsDark();
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = Rgba(c.bgSurface);      // 外壳自绘，这里只兜底
    colors[ImGuiCol_ChildBg] = Rgba(c.bgPanel);
    colors[ImGuiCol_PopupBg] = Rgba(c.bgElevated);
    colors[ImGuiCol_Border] = Rgba(c.lineNormal);
    colors[ImGuiCol_BorderShadow] = Rgba(c.shadowScrim);
    colors[ImGuiCol_FrameBg] = Rgba(c.fillMuted);
    colors[ImGuiCol_FrameBgHovered] = Rgba(c.fillHover);
    colors[ImGuiCol_FrameBgActive] = Rgba(c.fillSelected);
    colors[ImGuiCol_TitleBg] = Rgba(c.bgPanel);
    colors[ImGuiCol_TitleBgActive] = Rgba(c.bgPanel);
    colors[ImGuiCol_TitleBgCollapsed] = Rgba(c.bgSurface);
    colors[ImGuiCol_MenuBarBg] = Rgba(c.bgPanel);
    // theme-ok：全透明。滚动条由壳层自绘（ScrollRegion 走 BeginChild），
    // 这里给的是「什么都不画」而不是一个颜色，token 表里没有对应项。
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); // theme-ok
    colors[ImGuiCol_ScrollbarGrab] = Rgba(c.lineStrong);
    colors[ImGuiCol_ScrollbarGrabHovered] = Rgba(c.textMuted);
    colors[ImGuiCol_ScrollbarGrabActive] = Rgba(c.accentPrimary);
    colors[ImGuiCol_CheckMark] = Rgba(c.accentPrimary);
    colors[ImGuiCol_SliderGrab] = Rgba(c.accentPrimary);
    colors[ImGuiCol_SliderGrabActive] = Rgba(c.accentPrimaryHover);
    colors[ImGuiCol_Button] = Rgba(c.fillMuted);
    colors[ImGuiCol_ButtonHovered] = Rgba(c.fillHover);
    colors[ImGuiCol_ButtonActive] = Rgba(c.fillSelected);
    colors[ImGuiCol_Header] = Rgba(c.fillHover);
    colors[ImGuiCol_HeaderHovered] = Rgba(c.fillSelected);
    colors[ImGuiCol_HeaderActive] = Rgba(d.accentDim);
    colors[ImGuiCol_Separator] = Rgba(c.lineSubtle);
    colors[ImGuiCol_SeparatorHovered] = Rgba(c.lineNormal);
    colors[ImGuiCol_SeparatorActive] = Rgba(c.lineStrong);
    colors[ImGuiCol_ResizeGrip] = Rgba(d.accentDim);
    colors[ImGuiCol_Tab] = Rgba(c.bgPanel);
    colors[ImGuiCol_TabHovered] = Rgba(c.fillHover);
    colors[ImGuiCol_TabActive] = Rgba(c.bgSurface);
    colors[ImGuiCol_TabUnfocused] = Rgba(c.bgPanel);
    colors[ImGuiCol_TabUnfocusedActive] = Rgba(c.bgSurface);
    colors[ImGuiCol_DockingPreview] = Rgba(d.accentDim);
    colors[ImGuiCol_Text] = Rgba(c.textPrimary);
    colors[ImGuiCol_TextDisabled] = Rgba(c.textMuted);
    colors[ImGuiCol_PlotLines] = Rgba(c.lineNormal);
    colors[ImGuiCol_PlotLinesHovered] = Rgba(c.accentPrimary);
    colors[ImGuiCol_PlotHistogram] = Rgba(c.accentPrimary);
    colors[ImGuiCol_PlotHistogramHovered] = Rgba(c.accentPrimaryHover);
    colors[ImGuiCol_TableHeaderBg] = Rgba(c.fillMuted);
    colors[ImGuiCol_TableBorderStrong] = Rgba(c.lineNormal);
    colors[ImGuiCol_TableBorderLight] = Rgba(c.lineSubtle);
    colors[ImGuiCol_TableRowBg] = Rgba(c.bgPanel);
    colors[ImGuiCol_TableRowBgAlt] = Rgba(c.bgSurface);
    colors[ImGuiCol_TextSelectedBg] = Rgba(d.accentDim);
    colors[ImGuiCol_DragDropTarget] = Rgba(d.accentGlow);
    colors[ImGuiCol_NavHighlight] = Rgba(d.accentDim);
    colors[ImGuiCol_NavWindowingHighlight] = Rgba(c.lineFocus);
    colors[ImGuiCol_ModalWindowDimBg] = Rgba(c.shadowScrim);
}

void ApplyCurrentTheme() { ApplyTheme(g_current); }

std::filesystem::path DefaultThemeFile() {
    // APPDATA 是宽字符环境变量：必须走宽字符 API，按本地代码页读会误解码路径。
    if (const wchar_t* appdata = _wgetenv(L"APPDATA"); appdata != nullptr) {
        return std::filesystem::path(appdata) / L"ShineTVStudio" / L"theme.json"; // theme-ok
    }
    return {};
}

bool PersistTheme(const std::filesystem::path& file) {
    if (file.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    const ColorToken& c = Current();
    std::ostringstream out;
    out << "{\n  \"id\": \"" << ThemeIdKey(g_current) << "\",\n  \"colors\": {\n";
    const std::array<std::uint32_t ColorToken::*, kColorTokenCount> kFields = {
        &ColorToken::bgVoid, &ColorToken::bgSurface, &ColorToken::bgPanel, &ColorToken::bgElevated,
        &ColorToken::bgOverlay, &ColorToken::lineSubtle, &ColorToken::lineNormal,
        &ColorToken::lineStrong, &ColorToken::textPrimary, &ColorToken::textSecondary,
        &ColorToken::textMuted, &ColorToken::textInverse, &ColorToken::accentPrimary,
        &ColorToken::accentPrimaryHover, &ColorToken::accentPrimaryFg, &ColorToken::accentSecondary,
        &ColorToken::accentInfo, &ColorToken::statusOk, &ColorToken::statusWarn,
        &ColorToken::statusDanger, &ColorToken::statusBusy, &ColorToken::statusIdle,
        &ColorToken::statusPending, &ColorToken::fillHover, &ColorToken::fillSelected,
        &ColorToken::fillMuted, &ColorToken::lineFocus, &ColorToken::shadowScrim,
        &ColorToken::shadow1, &ColorToken::shadow2, &ColorToken::shadowAccent,
    };
    for (std::size_t i = 0; i < kColorTokenCount; ++i) {
        out << "    \"" << kColorTokenNames[i] << "\": \"" << ToHex(c.*(kFields[i]))
            << (i + 1 == kColorTokenCount ? "\"\n" : "\",\n");
    }
    out << "  }\n}\n";

    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream << out.str();
    return stream.good();
}

bool LoadPersistedTheme(const std::filesystem::path& file) {
    if (file.empty() || !std::filesystem::exists(file)) {
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        return false; // 损坏 → 保持默认主题，不崩（照 Qt 侧 MainWindow 的行为）
    }
    ThemeId id = ThemeId::DeepSpace;
    if (const char* key = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "id"));
        key != nullptr && ThemeIdFromKey(key, id)) {
        yyjson_doc_free(doc);
        ApplyTheme(id);
        return true;
    }
    yyjson_doc_free(doc);
    return false;
}

bool SelfTestRoundTrip(std::string* report) {
    std::ostringstream out;
    bool ok = true;
    for (ThemeId id : kAllThemes) {
        const ThemeRecord& record = g_themes[IndexOf(id)];
        out << ThemeDisplayName(id) << ": " << (record.loaded ? "loaded" : "MISSING");
        if (!record.loaded) {
            ok = false;
        }
        // 每套主题的派生色必须与主色自洽：tagBg 的 alpha 恒为 12%。
        const std::uint32_t accentTagBg = record.derived.tagBg[static_cast<std::size_t>(Tone::Accent)];
        if ((accentTagBg & 0xFFu) != 31u) {
            out << " (tagBg alpha=" << (accentTagBg & 0xFFu) << " != 31)";
            ok = false;
        }
        // 13 档矩阵的 alpha 字节必须等于 alpha::kBytes，且 RGB 通道取自主色本身
        // （color-mix(·, N%, transparent) 不改 RGB）。错一格就是整档偏 1/255。
        for (std::size_t s = 0; s < alpha::kStepCount; ++s) {
            const std::size_t ti = static_cast<std::size_t>(Tone::Accent);
            const std::uint32_t v = record.derived.toneAlpha[s][ti];
            const std::uint32_t wantAlpha = alpha::kBytes[s];
            const std::uint32_t wantRgb =
                record.derived.tagBg[ti] & 0xFFFFFF00u; // 同为 tone 色，只比 alpha
            if ((v & 0xFFu) != wantAlpha || (v & 0xFFFFFF00u) != wantRgb) {
                out << " (toneAlpha[" << s << "])=" << ToHex(v) << " != " << ToHex(wantRgb | wantAlpha);
                ok = false;
                break;
            }
        }
        // 实色预混必须不透明：color-mix 两端都是不透明基色。
        if ((record.derived.crumbBg & 0xFFu) != 0xFFu || (record.derived.stageFailBg & 0xFFu) != 0xFFu) {
            out << " (premix not opaque)";
            ok = false;
        }
        out << '\n';
    }

    // ---- 通道顺序硬判据 ----
    // theme 内部是 0xRRGGBBAA，ImGui 打包是 A<<24|B<<16|G<<8|R，两套不一样。
    // 写错过一次：ColorOf 当时把 theme 值原样当 ImU32 递出去，等于把整个界面的
    // 颜色通道旋转了一次。深色主题旋转后仍然「看着像深色」，只有主按钮那种
    // 高饱和不透明填充才炸得出来 —— 所以这里放一组探针值当场验字节。
    {
        constexpr std::uint32_t kProbe = 0x11223344u; // R=11 G=22 B=33 A=44
        const ImU32 packed = ToImU32(kProbe);
        const auto byte = [packed](int shift) { return (packed >> shift) & 0xFFu; };
        const bool orderOk = byte(IM_COL32_R_SHIFT) == 0x11u &&
                             byte(IM_COL32_G_SHIFT) == 0x22u &&
                             byte(IM_COL32_B_SHIFT) == 0x33u &&
                             byte(IM_COL32_A_SHIFT) == 0x44u;
        out << "channel order: " << (orderOk ? "ok" : "MISMATCH");
        if (!orderOk) {
            out << " (got R=" << byte(IM_COL32_R_SHIFT) << " G=" << byte(IM_COL32_G_SHIFT)
                << " B=" << byte(IM_COL32_B_SHIFT) << " A=" << byte(IM_COL32_A_SHIFT)
                << ", want 17 34 51 68)";
            ok = false;
        }
        out << '\n';
    }

    // WithAlpha / LerpColorTo 也靠 ImGui 自己的转换器，一并探。
    {
        const ImU32 base = ToImU32(0x112233FFu);
        ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(base);
        rgba.w = 0.5f;
        const ImU32 faded = ImGui::ColorConvertFloat4ToU32(rgba);
        const bool alphaOk = ((faded >> IM_COL32_A_SHIFT) & 0xFFu) == 0x80u &&
                             ((faded >> IM_COL32_R_SHIFT) & 0xFFu) == 0x11u;
        out << "alpha override: " << (alphaOk ? "ok" : "MISMATCH");
        if (!alphaOk) {
            out << " (R=" << ((faded >> IM_COL32_R_SHIFT) & 0xFFu)
                << " A=" << ((faded >> IM_COL32_A_SHIFT) & 0xFFu) << ", want R=17 A=128)";
        }
        ok = ok && alphaOk;
        out << '\n';
    }
    if (report != nullptr) {
        *report = out.str();
    }
    return ok;
}

} // namespace shine::theme
