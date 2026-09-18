#include "app/ui/Widgets.h"

#include "app/FileDialog.h" // PathPickerRow 用（目录/文件选择对话框）

#include <misc/cpp/imgui_stdlib.h> // InputText(std::string*)

#include <algorithm> // std::clamp（DragSplitter）
#include <string>

namespace shine::app::ui {
namespace {

// ImGui 的 %s 需要连续内存的 C 串；string_view 不保证 —— 统一在这里转
[[nodiscard]] std::string ToStr(std::string_view view) { return std::string{view}; }

} // namespace

void PanelHeader(std::string_view title) {
    ImGui::TextDisabled("%s", ToStr(title).c_str());
    ImGui::Separator();
}

void SectionText(std::string_view text) { ImGui::SeparatorText(ToStr(text).c_str()); }

void KvRow(std::string_view key, std::string_view value) {
    ImGui::TextDisabled("%s", ToStr(key).c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(ToStr(value).c_str());
}

void EmptyState(std::string_view text) { ImGui::TextDisabled("%s", ToStr(text).c_str()); }

void Badge(std::string_view text, const float color[4]) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    constexpr float kRadius = 5.f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + kRadius, pos.y + kRadius + 2.f), kRadius,
                                                ImGui::ColorConvertFloat4ToU32(
                                                    ImVec4(color[0], color[1], color[2], color[3])));
    ImGui::Dummy(ImVec2(kRadius * 2.f + 6.f, kRadius * 2.f + 4.f));
    ImGui::SameLine();
    ImGui::TextUnformatted(ToStr(text).c_str());
}

bool PathPickerRow(std::string_view label, std::string_view hint, std::string& buffer, std::string_view browseTitle,
                   bool folder) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint(ToStr(label).c_str(), ToStr(hint).c_str(), &buffer)) {
        changed = true;
    }
    const std::string buttonId = "浏览…##" + ToStr(label);
    if (ImGui::Button(buttonId.c_str())) {
        const std::string picked = folder ? SelectFolderDialog(ToStr(browseTitle), buffer)
                                          : OpenFileDialog(ToStr(browseTitle), {}, buffer);
        if (!picked.empty()) {
            buffer = picked;
            changed = true;
        }
    }
    return changed;
}

void ThumbTile(ImTextureID id, ImVec2 size, std::string_view stateText) {
    if (id != 0) {
        ImGui::Image(ImTextureRef(id), size);
        return;
    }
    ImGui::Dummy(size);
    if (!stateText.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ToStr(stateText).c_str());
    }
}

void TooltipWrapped(std::string_view text, float wrapEm) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * wrapEm); // 折行宽度（**必须**在 BeginTooltip 之后压）
    ImGui::TextUnformatted(ToStr(text).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool DragSplitter(const char* id, float* width, float minWidth, float maxWidth, float height) {
    if (width == nullptr) {
        return false;
    }
    constexpr float kHitWidth = 6.0f; // 命中区比视觉线宽，好抓
    ImGui::SameLine();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(kHitWidth, height));
    const bool active = ImGui::IsItemActive();
    const bool hot = active || ImGui::IsItemHovered();
    bool changed = false;
    if (active) {
        *width += ImGui::GetIO().MouseDelta.x;
        changed = true;
    }
    if (hot) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); // ⬌
    }
    *width = std::clamp(*width, minWidth, maxWidth);
    const float centerX = origin.x + kHitWidth * 0.5f;
    const ImU32 color = ImGui::GetColorU32(hot ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(centerX - 1.0f, origin.y), ImVec2(centerX + 1.0f, origin.y + height),
                                              color);
    return changed;
}

} // namespace shine::app::ui
