#include "ui/imgui/kit/Debug.h"

#include "core/Log.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/theme/Theme.h"

#include <misc/cpp/imgui_stdlib.h>

#include <array>
#include <string>
#include <vector>

namespace shine::kit {
namespace {

bool g_saveRequested = false;

// 样式编辑器的草稿：改动先落在这里，Save 才回写 theme::Current()。
theme::ColorToken g_draft{};
bool g_draftLoaded = false;

void EnsureDraft() {
    if (g_draftLoaded) {
        return;
    }
    g_draft = theme::Current();
    g_draftLoaded = true;
}

} // namespace

void DrawDebugWindows(DebugWindows& state) {
    // F1 = Metrics（自绘 UI 的救命工具：没画出来时它能指出被裁 / alpha 0 / 尺寸 0）
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        state.metrics = !state.metrics;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        state.log = !state.log;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, true)) {
        state.demo = !state.demo;
    }

    if (state.metrics) {
        ImGui::ShowMetricsWindow(&state.metrics);
    }
    if (state.log) {
        ImGui::ShowDebugLogWindow(&state.log);
    }
    if (state.demo) {
        ImGui::ShowDemoWindow(&state.demo);
    }
}

void DrawStyleEditorPanel() {
    EnsureDraft();

    if (ImGui::Button("重置")) {
        g_draft = theme::Current();
    }
    ImGui::SameLine();
    if (ImGui::Button("保存到 theme.json")) {
        g_saveRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("还原")) {
        g_draftLoaded = false;
    }
    ImGui::Spacing();

    // 31 个 token 用 ImGui 自带的 ColorEdit4 排成网格 —— 不手写取色器。
    // ColorEdit4 要 float[4]，token 存的是 0xRRGGBBAA，逐项换算。
    ImGui::PushFont(FontAt(12.0f));
    constexpr int columns = 3;
    for (std::size_t i = 0; i < theme::kColorTokenCount; ++i) {
        ImGui::PushID(static_cast<int>(i));
        std::uint32_t* slot = theme::TokenSlot(g_draft, i);
        if (slot != nullptr) {
            // 解包/回写都走 theme 模块自己的那对函数 —— 字节序只在 Theme.cpp 里写一次。
            const ImVec4 rgba = theme::Rgba(*slot);
            float edited[4] = {rgba.x, rgba.y, rgba.z, rgba.w};
            if (ImGui::ColorEdit4(theme::kColorTokenNames[i].data(), edited,
                                  ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                      ImGuiColorEditFlags_NoInputs)) {
                *slot = theme::PackRgba(ImVec4(edited[0], edited[1], edited[2], edited[3]));
            }
        }
        ImGui::PopID();
        if (static_cast<int>(i) % columns != columns - 1) {
            ImGui::SameLine();
        }
    }
    ImGui::PopFont();

    ImGui::SeparatorText("ImGuiStyle 原生（官方 ShowStyleEditor）");
    // 官方自带：几何档 + 全部 ImGuiCol_* 的实时编辑。这块**不重写**。
    ImGui::ShowStyleEditor();
}

bool StyleEditorSaveRequested() { return g_saveRequested; }
void StyleEditorRequestSave() { g_saveRequested = true; }
void StyleEditorClearSaveRequest() { g_saveRequested = false; }

} // namespace shine::kit
