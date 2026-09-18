#include "theme/ThemeTokens.h"

#include "core/Log.h"
#include "core/Settings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace shine::theme {
namespace {

// —— 唯一事实来源：25 个 token。加新颜色只改这里（UI 与存盘自动跟上）——
constexpr ThemeToken kTokens[] = {
    {"windowBg", "基础", &ThemeColors::windowBg},
    {"panelBg", "基础", &ThemeColors::panelBg},
    {"childBg", "基础", &ThemeColors::childBg},
    {"titleBar", "基础", &ThemeColors::titleBar},
    {"menuBar", "基础", &ThemeColors::menuBar},
    {"border", "基础", &ThemeColors::border},
    {"separator", "基础", &ThemeColors::separator},
    {"text", "基础", &ThemeColors::text},
    {"textDim", "基础", &ThemeColors::textDim},

    {"accent", "强调与状态", &ThemeColors::accent},
    {"accentAlt", "强调与状态", &ThemeColors::accentAlt},
    {"navHighlight", "强调与状态", &ThemeColors::navHighlight},
    {"danger", "强调与状态", &ThemeColors::danger},
    {"success", "强调与状态", &ThemeColors::success},

    {"frameBg", "控件", &ThemeColors::frameBg},
    {"frameHover", "控件", &ThemeColors::frameHover},
    {"frameActive", "控件", &ThemeColors::frameActive},
    {"header", "控件", &ThemeColors::header},
    {"headerHover", "控件", &ThemeColors::headerHover},
    {"headerActive", "控件", &ThemeColors::headerActive},
    {"checkMark", "控件", &ThemeColors::checkMark},
    {"sliderGrab", "控件", &ThemeColors::sliderGrab},
    {"resizeGrip", "控件", &ThemeColors::resizeGrip},
    {"tabActive", "标签页", &ThemeColors::tabActive},
    {"tabUnactive", "标签页", &ThemeColors::tabUnactive},
};

std::map<std::string, std::array<float, 4>> g_overrides;

[[nodiscard]] int HexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::span<const ThemeToken> Tokens() { return {kTokens, std::size(kTokens)}; }

const ThemeToken* FindToken(std::string_view name) noexcept {
    for (const ThemeToken& token : kTokens) {
        if (name == token.name) {
            return &token;
        }
    }
    return nullptr;
}

const std::map<std::string, std::array<float, 4>>& Overrides() { return g_overrides; }

std::size_t OverrideCount() noexcept { return g_overrides.size(); }

void SetOverride(std::string_view name, const std::array<float, 4>& rgba) {
    if (FindToken(name) == nullptr) {
        log::Warn("主题覆盖：未知 token {}", name);
        return;
    }
    g_overrides[std::string{name}] = rgba;
}

void ClearOverride(std::string_view name) { g_overrides.erase(std::string{name}); }

void ClearOverrides() { g_overrides.clear(); }

void ReapplyTheme() {
    // 顺序很重要：**先预设、再叠加覆盖**（否则覆盖会被预设冲掉）
    ApplyPresetById(Settings().themeId);
    if (g_overrides.empty()) {
        return;
    }
    ThemeColors colors = Current();
    for (const auto& [name, rgba] : g_overrides) {
        const ThemeToken* token = FindToken(name);
        if (token != nullptr) {
            std::memcpy(&(colors.*(token->member)), rgba.data(), sizeof(float) * 4);
        }
    }
    ApplyTheme(colors);
}

std::string ToHex(const float rgba[4]) {
    const auto byte = [](float v) {
        const int i = static_cast<int>(v * 255.f + 0.5f);
        return static_cast<unsigned>(std::clamp(i, 0, 255));
    };
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", byte(rgba[0]), byte(rgba[1]), byte(rgba[2]),
                  byte(rgba[3]));
    return buffer;
}

bool FromHex(std::string_view hex, float out[4]) noexcept {
    hex = Trim(hex);
    if (hex.size() != 9 || hex.front() != '#') {
        return false;
    }
    unsigned values[4] = {0, 0, 0, 255};
    for (int i = 0; i < 4; ++i) {
        const int hi = HexNibble(hex[1 + i * 2]);
        const int lo = HexNibble(hex[2 + i * 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        values[i] = static_cast<unsigned>(hi * 16 + lo);
    }
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<float>(values[i]) / 255.f;
    }
    return true;
}

std::string ExportOverrides() {
    std::string text;
    for (const auto& [name, rgba] : g_overrides) {
        if (!text.empty()) {
            text += ';';
        }
        text += name;
        text += '=';
        text += ToHex(rgba.data());
    }
    return text;
}

void ApplyOverrides(std::string_view text, bool reapply) {
    g_overrides.clear();
    std::size_t start = 0;
    int bad = 0;
    while (start <= text.size()) {
        const std::size_t sep = text.find(';', start);
        const std::string_view item = text.substr(start, sep == std::string_view::npos ? std::string_view::npos
                                                                                       : sep - start);
        if (!item.empty()) {
            const std::size_t eq = item.find('=');
            if (eq == std::string_view::npos) {
                ++bad;
            } else {
                float rgba[4] = {0.f, 0.f, 0.f, 1.f};
                if (FromHex(item.substr(eq + 1), rgba)) {
                    SetOverride(Trim(item.substr(0, eq)), {rgba[0], rgba[1], rgba[2], rgba[3]});
                } else {
                    ++bad;
                }
            }
        }
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }
    if (bad > 0) {
        log::Warn("主题覆盖：{} 项无法解析，已忽略", bad);
    }
    if (reapply) {
        ReapplyTheme();
    }
}

} // namespace shine::theme
