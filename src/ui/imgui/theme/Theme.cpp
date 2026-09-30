#include "ui/imgui/theme/Theme.h"

#include "ui/imgui/theme/Theme_Internal.h"

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

// 本文件现在只保留「身份 / 访问器 / 阴影几何 / 应用到 ImGuiStyle / 持久化 +
// 自检」这几族。色值数学、派生色预混、JSON 解析分别搬去了 Theme_Color.cpp /
// Theme_Derived.cpp / Theme_Parse.cpp；跨文件共享的状态与内部函数声明在
// Theme_Internal.h。
namespace detail {

// 5 套主题的全局表。**全工程只有这一份定义**（Theme_Internal.h 里是 extern 声明）。
std::array<ThemeRecord, 5> g_themes{};
ThemeId g_current = ThemeId::DeepSpace;
bool g_loaded = false;

} // namespace detail

// 本文件其余部分照旧按短名使用，不必到处写 detail::。
using detail::g_current;
using detail::g_loaded;
using detail::g_themes;
using detail::IndexOf;
using detail::ThemeRecord;

std::string_view ThemeIdKey(ThemeId id) {
    switch (id) {
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


bool ThemesLoaded() { return g_loaded; }

const ColorToken& ThemeColorsOf(ThemeId id) { return g_themes[IndexOf(id)].colors; }
const Derived& ThemeDerivedOf(ThemeId id) { return g_themes[IndexOf(id)].derived; }

// ---- 阴影几何（逐主题）----
//
// 色相取主题 JSON 的 `shadow.1` / `shadow.2` / `shadow.accent`（逐条核对过，五套
// 主题的 RGBA 与 CSS 原值一致，误差 ≤ 1/255 的取整）；**几何与逐层 alpha** 记在
// 这里，因为 JSON 存不下 —— CSS 的 `--shadow-1` 是**两层**，而 JSON 里的
// `shadow.1` 只有一个 alpha，逐条核对下来它等于**第二层（柔和层）**的值，
// 第一层（1px/2px 的接触阴影）那个 alpha 被丢掉了。所以：
//
//   层 0 = 接触阴影（紧、浅）  alpha 取 CSS 原文
//   层 1 = 环境阴影（松、深）  alpha 取 CSS 原文（与 JSON 的值一致）
//
// CSS 原文（webui/src/styles/tokens.css，行号见下）：
//   深空   :67  --shadow-1: 0 1px 2px rgba(0,0,0,.35), 0 4px 16px rgba(0,0,0,.3)
//                 --shadow-2: 0 12px 40px rgba(0,0,0,.45)
//                 --shadow-accent: 0 4px 20px rgba(53,208,180,.28)
//   薄暮  :108  0 1px 2px .40, 0 4px 16px .32 / 0 12px 40px .50 / 0 4px 20px rgba(242,166,90,.28)
//   纸墨  :149  0 1px 2px rgba(60,50,30,.1), 0 4px 14px rgba(60,50,30,.1)
//                 0 12px 36px rgba(60,50,30,.16) / 0 4px 18px rgba(12,133,119,.25)
//   水墨  :190  0 1px 2px rgba(50,44,36,.12), 0 4px 14px rgba(50,44,36,.1)
//                 0 12px 36px rgba(50,44,36,.18) / 0 4px 18px rgba(47,44,40,.22)
//   极夜  :243  0 1px 2px .6, 0 4px 16px .5 / 0 12px 40px .7 / 0 4px 20px rgba(62,207,178,.26)
//
// ⚠️ 浅色主题的模糊半径**也不同**（14px / 36px，深色是 16px / 40px），照抄深空的
//    数字会让纸墨的阴影虚一圈 —— 这类「逐主题不同」的细节正是抄一份固定值最容
//    易漏掉的地方。
namespace {
struct ThemeShadow {
    ShadowSpec card;    // --shadow-1（两层）
    ShadowSpec overlay; // --shadow-2（一层）
    ShadowSpec accent;  // --shadow-accent（一层）
};
constexpr std::array<ThemeShadow, 5> kShadowSpecs{{
    // 深空 tokens.css:67-69
    {{ShadowLayer{0.0f, 1.0f, 2.0f, 0.35f}, ShadowLayer{0.0f, 4.0f, 16.0f, 0.30f}, 2},
     {ShadowLayer{0.0f, 12.0f, 40.0f, 0.45f}, {}, 1},
     {ShadowLayer{0.0f, 4.0f, 20.0f, 0.28f}, {}, 1}},
    // 薄暮 tokens.css:108-110
    {{ShadowLayer{0.0f, 1.0f, 2.0f, 0.40f}, ShadowLayer{0.0f, 4.0f, 16.0f, 0.32f}, 2},
     {ShadowLayer{0.0f, 12.0f, 40.0f, 0.50f}, {}, 1},
     {ShadowLayer{0.0f, 4.0f, 20.0f, 0.28f}, {}, 1}},
    // 纸墨 tokens.css:149-151（浅色：模糊更小、alpha 极低）
    {{ShadowLayer{0.0f, 1.0f, 2.0f, 0.10f}, ShadowLayer{0.0f, 4.0f, 14.0f, 0.10f}, 2},
     {ShadowLayer{0.0f, 12.0f, 36.0f, 0.16f}, {}, 1},
     {ShadowLayer{0.0f, 4.0f, 18.0f, 0.25f}, {}, 1}},
    // 水墨 tokens.css:190-192
    {{ShadowLayer{0.0f, 1.0f, 2.0f, 0.12f}, ShadowLayer{0.0f, 4.0f, 14.0f, 0.10f}, 2},
     {ShadowLayer{0.0f, 12.0f, 36.0f, 0.18f}, {}, 1},
     {ShadowLayer{0.0f, 4.0f, 18.0f, 0.22f}, {}, 1}},
    // 极夜 tokens.css:243-245（OLED：alpha 最高）
    {{ShadowLayer{0.0f, 1.0f, 2.0f, 0.60f}, ShadowLayer{0.0f, 4.0f, 16.0f, 0.50f}, 2},
     {ShadowLayer{0.0f, 12.0f, 40.0f, 0.70f}, {}, 1},
     {ShadowLayer{0.0f, 4.0f, 20.0f, 0.26f}, {}, 1}},
}};

const ShadowSpec& EmptySpec() {
    static const ShadowSpec kEmpty{};
    return kEmpty;
}
} // namespace

const ShadowSpec& ShadowSpecOf(ThemeId id, ShadowTier tier) {
    if (!ThemesLoaded()) {
        return EmptySpec();
    }
    const ThemeShadow& s = kShadowSpecs[IndexOf(id)];
    switch (tier) {
    case ShadowTier::Overlay:
        return s.overlay;
    case ShadowTier::Accent:
        return s.accent;
    case ShadowTier::Card:
        return s.card;
    case ShadowTier::None:
    case ShadowTier::kCount:
    default:
        return EmptySpec();
    }
}

// 某一档的**颜色 token**（原始 std::uint32_t RGBA，不是 ImU32）。
//
// ⚠️ 返回原始值而不是 ImU32：RGBA → ImU32 的转换是 `kit::ColorOf`，住在 kit 层。
//    theme 反过来去 include kit 是**倒置依赖**（Widgets.h 依赖 Theme.h）。
std::uint32_t ShadowTokenOf(ThemeId id, ShadowTier tier) {
    if (!ThemesLoaded()) {
        return 0u;
    }
    const ColorToken& c = ThemeColorsOf(id);
    switch (tier) {
    case ShadowTier::Card:
        return c.shadow1;
    case ShadowTier::Overlay:
        return c.shadow2;
    case ShadowTier::Accent:
        return c.shadowAccent;
    case ShadowTier::None:
    case ShadowTier::kCount:
    default:
        return 0u;
    }
}

std::uint32_t CurrentShadowToken(ShadowTier tier) { return ShadowTokenOf(g_current, tier); }

const ShadowSpec& CurrentShadowSpec(ShadowTier tier) { return ShadowSpecOf(g_current, tier); }

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
        out << "    \"" << kColorTokenNames[i] << "\": \"" << detail::ToHex(c.*(kFields[i]))
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
                out << " (toneAlpha[" << s << "])=" << detail::ToHex(v)
                    << " != " << detail::ToHex(wantRgb | wantAlpha);
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
