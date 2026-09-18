// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/views/bottom/DrawBottomPanel.h"
#include "app/AppIncludes.h"

namespace shine::app {
namespace {

// 验收开关（与 `SHINE_SIDE_VIEW` / `SHINE_WINDOW` 同款）：`SHINE_BOTTOM_TAB=queue|log|output`
// 直接把底栏停在指定页 —— 截图验收要"确定的状态"，不能靠鼠标点（见 `Plan/坑与手法.md` §9）。
// ⚠️ 名字必须是 **ASCII**：环境变量在 Windows 上按 **ANSI 代码页**传进来，中文名跟源码里的 UTF-8
// 字面量对不上（实测 `SHINE_BOTTOM_TAB=输出` 永远不生效）。
// 只在前 60 帧生效，之后交回用户（否则用户永远切不走这一页）。
[[nodiscard]] bool ShouldForceTab(std::string_view asciiName) {
    static const std::string want = []() -> std::string {
        const char* raw = std::getenv("SHINE_BOTTOM_TAB");
        return raw == nullptr ? std::string{} : std::string{raw};
    }();
    static int frames = 0;
    return !want.empty() && want == asciiName && frames++ < 60;
}

} // namespace

void DrawBottomPanel() {
    if (!ImGui::BeginTabBar("BottomTabs")) return;
    const ImGuiTabItemFlags force = ShouldForceTab("queue") ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem("队列", nullptr, force)) {
        DrawQueueTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("日志", nullptr, ShouldForceTab("log") ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
        DrawLogTab();
        ImGui::EndTabItem();
    }
    // ⚠️ 三个页都要各判一次 `ShouldForceTab`：它内部有帧计数，只在匹配的那一页自增
    if (ImGui::BeginTabItem("输出", nullptr, ShouldForceTab("output") ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
        output::DrawOutputView(); // P4.4：历史结果列表（缩略图按需加载 + 右键菜单）
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace shine::app
