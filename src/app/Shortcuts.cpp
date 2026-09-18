#include "app/Shortcuts.h"

#include "app/UiState.h"
#include "core/Settings.h"
#include "theme/Theme.h"

#include <imgui.h>

namespace shine::app::shortcuts {
namespace {
std::vector<Binding> g_bindings;
}

void Clear() { g_bindings.clear(); }

void Register(Binding b) { g_bindings.push_back(std::move(b)); }

const std::vector<Binding>& All() noexcept { return g_bindings; }

void Poll() {
    if (g_bindings.empty()) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    for (const Binding& b : g_bindings) {
        if (!b.action) {
            continue;
        }
        const bool ctrlOk = !b.ctrl || io.KeyCtrl;
        const bool shiftOk = !b.shift || io.KeyShift;
        const bool altOk = !b.alt || io.KeyAlt;
        if (b.ctrl && !io.KeyCtrl) {
            continue;
        }
        if (b.shift && !io.KeyShift) {
            continue;
        }
        if (b.alt && !io.KeyAlt) {
            continue;
        }
        if (!ctrlOk || !shiftOk || !altOk) {
            continue;
        }
        if (b.imguiKey != 0 && ImGui::IsKeyPressed(static_cast<ImGuiKey>(b.imguiKey), false)) {
            b.action();
        }
    }
}

void DrawHelpWindow(bool* open) {
    if (open == nullptr || !*open) {
        return;
    }
    ImGui::Begin("快捷键", open, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextDisabled("输入框聚焦时不会误触发");
    ImGui::Separator();
    for (const Binding& b : g_bindings) {
        ImGui::TextUnformatted(b.combo.c_str());
        ImGui::SameLine(120.f);
        ImGui::TextUnformatted(b.label.c_str());
    }
    if (g_bindings.empty()) {
        ImGui::TextDisabled("（尚未注册自定义组合键；面板内快捷键见各窗口提示）");
    }
    ImGui::Separator();
    ImGui::TextDisabled("版本 %s", Settings().appVersion.c_str());
    ImGui::End();
}

} // namespace shine::app::shortcuts
