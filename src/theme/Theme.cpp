#include "theme/Theme.h"

#include <imgui.h>

#include <ranges>

namespace shine::theme {
namespace {

ThemeColors g_current{};

std::vector<ThemePreset> MakePresets() {
    std::vector<ThemePreset> list;

    // Dark: deep blue-black + teal accent (default)
    {
        ThemePreset t;
        t.id = "dark";
        t.name = "Dark";
        t.colors = {
            /*windowBg*/ {0.055f, 0.067f, 0.086f, 1.f},
            /*panelBg*/ {0.082f, 0.102f, 0.129f, 1.f},
            /*titleBar*/ {0.039f, 0.051f, 0.071f, 1.f},
            /*border*/ {0.165f, 0.200f, 0.251f, 1.f},
            /*text*/ {0.902f, 0.929f, 0.953f, 1.f},
            /*textDim*/ {0.545f, 0.604f, 0.667f, 1.f},
            /*accent*/ {0.180f, 0.769f, 0.714f, 1.f},
            /*accentAlt*/ {1.000f, 0.624f, 0.110f, 1.f},
            /*danger*/ {0.902f, 0.224f, 0.275f, 1.f},
            /*success*/ {0.239f, 0.863f, 0.592f, 1.f},
            /*menuBar*/ {0.047f, 0.059f, 0.078f, 1.f},
            /*tabActive*/ {0.120f, 0.560f, 0.520f, 1.f},
            /*tabUnactive*/ {0.070f, 0.090f, 0.110f, 1.f},
            /*childBg*/ {0.070f, 0.086f, 0.110f, 1.f},
            /*frameBg*/ {0.110f, 0.130f, 0.160f, 1.f},
            /*frameHover*/ {0.150f, 0.190f, 0.230f, 1.f},
            /*frameActive*/ {0.180f, 0.769f, 0.714f, 0.35f},
            /*header*/ {0.120f, 0.200f, 0.240f, 1.f},
            /*headerHover*/ {0.160f, 0.280f, 0.300f, 1.f},
            /*headerActive*/ {0.180f, 0.769f, 0.714f, 0.45f},
            /*checkMark*/ {0.180f, 0.769f, 0.714f, 1.f},
            /*sliderGrab*/ {0.180f, 0.769f, 0.714f, 0.85f},
            /*separator*/ {0.165f, 0.200f, 0.251f, 1.f},
            /*resizeGrip*/ {0.180f, 0.769f, 0.714f, 0.25f},
            /*navHighlight*/ {0.180f, 0.769f, 0.714f, 0.70f},
        };
        list.push_back(std::move(t));
    }

    // Light
    {
        ThemePreset t;
        t.id = "light";
        t.name = "Light";
        t.colors = {
            {0.960f, 0.965f, 0.970f, 1.f},
            {0.980f, 0.985f, 0.990f, 1.f},
            {0.920f, 0.930f, 0.940f, 1.f},
            {0.780f, 0.800f, 0.820f, 1.f},
            {0.120f, 0.140f, 0.160f, 1.f},
            {0.400f, 0.430f, 0.460f, 1.f},
            {0.050f, 0.550f, 0.500f, 1.f},
            {0.850f, 0.450f, 0.050f, 1.f},
            {0.800f, 0.150f, 0.180f, 1.f},
            {0.100f, 0.600f, 0.350f, 1.f},
            {0.900f, 0.910f, 0.920f, 1.f},
            {0.050f, 0.550f, 0.500f, 1.f},
            {0.880f, 0.890f, 0.900f, 1.f},
            {0.970f, 0.975f, 0.980f, 1.f},
            {0.920f, 0.930f, 0.940f, 1.f},
            {0.860f, 0.880f, 0.900f, 1.f},
            {0.050f, 0.550f, 0.500f, 0.20f},
            {0.850f, 0.900f, 0.920f, 1.f},
            {0.750f, 0.880f, 0.860f, 1.f},
            {0.050f, 0.550f, 0.500f, 0.30f},
            {0.050f, 0.550f, 0.500f, 1.f},
            {0.050f, 0.550f, 0.500f, 0.85f},
            {0.780f, 0.800f, 0.820f, 1.f},
            {0.050f, 0.550f, 0.500f, 0.35f},
            {0.050f, 0.550f, 0.500f, 0.60f},
        };
        list.push_back(std::move(t));
    }

    // Ocean
    {
        ThemePreset t;
        t.id = "ocean";
        t.name = "Ocean";
        t.colors = {
            {0.040f, 0.080f, 0.120f, 1.f},
            {0.060f, 0.110f, 0.160f, 1.f},
            {0.030f, 0.060f, 0.100f, 1.f},
            {0.120f, 0.220f, 0.320f, 1.f},
            {0.850f, 0.920f, 0.980f, 1.f},
            {0.450f, 0.580f, 0.680f, 1.f},
            {0.250f, 0.650f, 0.950f, 1.f},
            {0.950f, 0.700f, 0.200f, 1.f},
            {0.950f, 0.300f, 0.350f, 1.f},
            {0.300f, 0.850f, 0.700f, 1.f},
            {0.035f, 0.070f, 0.110f, 1.f},
            {0.250f, 0.650f, 0.950f, 1.f},
            {0.050f, 0.090f, 0.130f, 1.f},
            {0.050f, 0.095f, 0.140f, 1.f},
            {0.080f, 0.140f, 0.200f, 1.f},
            {0.100f, 0.180f, 0.260f, 1.f},
            {0.250f, 0.650f, 0.950f, 0.30f},
            {0.080f, 0.160f, 0.240f, 1.f},
            {0.120f, 0.220f, 0.320f, 1.f},
            {0.250f, 0.650f, 0.950f, 0.40f},
            {0.250f, 0.650f, 0.950f, 1.f},
            {0.250f, 0.650f, 0.950f, 0.85f},
            {0.120f, 0.220f, 0.320f, 1.f},
            {0.250f, 0.650f, 0.950f, 0.25f},
            {0.250f, 0.650f, 0.950f, 0.70f},
        };
        list.push_back(std::move(t));
    }

    // Mono
    {
        ThemePreset t;
        t.id = "mono";
        t.name = "Mono";
        t.colors = {
            {0.100f, 0.100f, 0.100f, 1.f},
            {0.130f, 0.130f, 0.130f, 1.f},
            {0.080f, 0.080f, 0.080f, 1.f},
            {0.250f, 0.250f, 0.250f, 1.f},
            {0.880f, 0.880f, 0.880f, 1.f},
            {0.550f, 0.550f, 0.550f, 1.f},
            {0.700f, 0.700f, 0.700f, 1.f},
            {0.850f, 0.750f, 0.400f, 1.f},
            {0.850f, 0.350f, 0.350f, 1.f},
            {0.500f, 0.800f, 0.500f, 1.f},
            {0.090f, 0.090f, 0.090f, 1.f},
            {0.700f, 0.700f, 0.700f, 1.f},
            {0.110f, 0.110f, 0.110f, 1.f},
            {0.120f, 0.120f, 0.120f, 1.f},
            {0.160f, 0.160f, 0.160f, 1.f},
            {0.200f, 0.200f, 0.200f, 1.f},
            {0.700f, 0.700f, 0.700f, 0.25f},
            {0.180f, 0.180f, 0.180f, 1.f},
            {0.220f, 0.220f, 0.220f, 1.f},
            {0.700f, 0.700f, 0.700f, 0.35f},
            {0.700f, 0.700f, 0.700f, 1.f},
            {0.700f, 0.700f, 0.700f, 0.85f},
            {0.250f, 0.250f, 0.250f, 1.f},
            {0.700f, 0.700f, 0.700f, 0.25f},
            {0.700f, 0.700f, 0.700f, 0.70f},
        };
        list.push_back(std::move(t));
    }

    return list;
}

const std::vector<ThemePreset>& All() {
    static const std::vector<ThemePreset> presets = MakePresets();
    return presets;
}

} // namespace

const std::vector<ThemePreset>& Presets() { return All(); }

const ThemePreset* FindPreset(std::string_view id) {
    const auto& all = All();
    const auto it = std::ranges::find_if(all, [id](const ThemePreset& p) { return p.id == id; });
    return it == all.end() ? nullptr : &*it;
}

const ThemeColors& Current() noexcept { return g_current; }

void ApplyTheme(const ThemeColors& c) {
    g_current = c;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = ImVec4(c.text[0], c.text[1], c.text[2], c.text[3]);
    colors[ImGuiCol_TextDisabled] = ImVec4(c.textDim[0], c.textDim[1], c.textDim[2], 1.f);
    colors[ImGuiCol_WindowBg] = ImVec4(c.windowBg[0], c.windowBg[1], c.windowBg[2], 1.f);
    colors[ImGuiCol_ChildBg] = ImVec4(c.childBg[0], c.childBg[1], c.childBg[2], 1.f);
    colors[ImGuiCol_PopupBg] = ImVec4(c.panelBg[0], c.panelBg[1], c.panelBg[2], 0.98f);
    colors[ImGuiCol_Border] = ImVec4(c.border[0], c.border[1], c.border[2], 0.60f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = ImVec4(c.frameBg[0], c.frameBg[1], c.frameBg[2], 1.f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(c.frameHover[0], c.frameHover[1], c.frameHover[2], 1.f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(c.frameActive[0], c.frameActive[1], c.frameActive[2], 1.f);
    colors[ImGuiCol_TitleBg] = ImVec4(c.titleBar[0], c.titleBar[1], c.titleBar[2], 1.f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(c.titleBar[0], c.titleBar[1], c.titleBar[2], 1.f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(c.titleBar[0], c.titleBar[1], c.titleBar[2], 0.8f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(c.menuBar[0], c.menuBar[1], c.menuBar[2], 1.f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(c.windowBg[0], c.windowBg[1], c.windowBg[2], 0.6f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(c.border[0], c.border[1], c.border[2], 0.8f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.5f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.8f);
    colors[ImGuiCol_CheckMark] = ImVec4(c.checkMark[0], c.checkMark[1], c.checkMark[2], 1.f);
    colors[ImGuiCol_SliderGrab] = ImVec4(c.sliderGrab[0], c.sliderGrab[1], c.sliderGrab[2], 1.f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    colors[ImGuiCol_Button] = ImVec4(c.header[0], c.header[1], c.header[2], 1.f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(c.headerHover[0], c.headerHover[1], c.headerHover[2], 1.f);
    colors[ImGuiCol_ButtonActive] = ImVec4(c.headerActive[0], c.headerActive[1], c.headerActive[2], 1.f);
    colors[ImGuiCol_Header] = ImVec4(c.header[0], c.header[1], c.header[2], 1.f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(c.headerHover[0], c.headerHover[1], c.headerHover[2], 1.f);
    colors[ImGuiCol_HeaderActive] = ImVec4(c.headerActive[0], c.headerActive[1], c.headerActive[2], 1.f);
    colors[ImGuiCol_Separator] = ImVec4(c.separator[0], c.separator[1], c.separator[2], 1.f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.6f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(c.resizeGrip[0], c.resizeGrip[1], c.resizeGrip[2], 0.3f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.6f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    colors[ImGuiCol_Tab] = ImVec4(c.tabUnactive[0], c.tabUnactive[1], c.tabUnactive[2], 1.f);
    colors[ImGuiCol_TabHovered] = ImVec4(c.headerHover[0], c.headerHover[1], c.headerHover[2], 1.f);
    colors[ImGuiCol_TabSelected] = ImVec4(c.tabActive[0], c.tabActive[1], c.tabActive[2], 1.f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    colors[ImGuiCol_TabDimmed] = ImVec4(c.tabUnactive[0], c.tabUnactive[1], c.tabUnactive[2], 0.8f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(c.tabActive[0], c.tabActive[1], c.tabActive[2], 0.7f);
    colors[ImGuiCol_DockingPreview] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(c.windowBg[0], c.windowBg[1], c.windowBg[2], 1.f);
    colors[ImGuiCol_PlotLines] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(c.header[0], c.header[1], c.header[2], 0.8f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(c.border[0], c.border[1], c.border[2], 1.f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(c.border[0], c.border[1], c.border[2], 0.5f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.30f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(c.accentAlt[0], c.accentAlt[1], c.accentAlt[2], 0.95f);
    colors[ImGuiCol_NavHighlight] = ImVec4(c.navHighlight[0], c.navHighlight[1], c.navHighlight[2], 1.f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(c.accent[0], c.accent[1], c.accent[2], 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);

    style.WindowRounding = 6.f;
    style.ChildRounding = 4.f;
    style.FrameRounding = 4.f;
    style.PopupRounding = 6.f;
    style.ScrollbarRounding = 6.f;
    style.GrabRounding = 4.f;
    style.TabRounding = 4.f;
    style.WindowPadding = ImVec2(10, 8);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.CellPadding = ImVec2(6, 4);
    style.ScrollbarSize = 14.f;
    style.WindowBorderSize = 1.f;
    style.ChildBorderSize = 1.f;
    style.PopupBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
}

void ApplyPresetById(std::string_view id) {
    if (const ThemePreset* p = FindPreset(id)) {
        ApplyTheme(p->colors);
    } else if (const ThemePreset* d = FindPreset("dark")) {
        ApplyTheme(d->colors);
    }
}

} // namespace shine::theme
