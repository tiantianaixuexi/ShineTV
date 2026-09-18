#pragma once
// shine::app —— 快捷键注册表（P8.2）
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::app::shortcuts {

struct Binding {
    std::string id;       // 稳定 id
    std::string combo;    // 展示用，如 "Ctrl+O"
    std::string label;    // 中文说明
    int imguiKey = 0;     // ImGuiKey
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::function<void()> action;
    bool global = false; // true = 菜单帮助里展示；实际触发仍由各面板判断 WantTextInput
};

void Clear();
void Register(Binding b);
[[nodiscard]] const std::vector<Binding>& All() noexcept;
// 每帧在 UI 线程调用：未聚焦文本框时触发
void Poll();
void DrawHelpWindow(bool* open);

} // namespace shine::app::shortcuts
