#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace shine::theme {

struct ThemeColors {
    float windowBg[4];
    float panelBg[4];
    float titleBar[4];
    float border[4];
    float text[4];
    float textDim[4];
    float accent[4];
    float accentAlt[4];
    float danger[4];
    float success[4];
    float menuBar[4];
    float tabActive[4];
    float tabUnactive[4];
    float childBg[4];
    float frameBg[4];
    float frameHover[4];
    float frameActive[4];
    float header[4];
    float headerHover[4];
    float headerActive[4];
    float checkMark[4];
    float sliderGrab[4];
    float separator[4];
    float resizeGrip[4];
    float navHighlight[4];
};

struct ThemePreset {
    std::string id;
    std::string name;
    ThemeColors colors;
};

[[nodiscard]] const std::vector<ThemePreset>& Presets();
[[nodiscard]] const ThemePreset* FindPreset(std::string_view id);
void ApplyTheme(const ThemeColors& c);
void ApplyPresetById(std::string_view id);
[[nodiscard]] const ThemeColors& Current() noexcept;

} // namespace shine::theme
