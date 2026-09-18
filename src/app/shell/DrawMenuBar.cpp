// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/DrawMenuBar.h"
#include "app/AppIncludes.h"
#include "app/gallery/FolderPicker.h" // G-S5：PickAndScanLocalFolder（UI 层）

namespace shine::app {

void DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("文件")) {
        // P3.7：模板浏览器（不搞下拉，直接开窗；图工具条上也有按钮）
        if (ImGui::MenuItem("从模板新建工作流...")) State().showTemplateWindow = true;
        ImGui::Separator();
        // G-S5：图库入口（真正的 Ctrl+O 处理在 `App::DrawFrame`，这里只显示快捷键提示）
        if (ImGui::MenuItem("打开图片文件夹...", "Ctrl+O")) {
            gallery::PickAndScanLocalFolder();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("设置...")) State().showSettings = true;
        ImGui::Separator();
        if (ImGui::MenuItem("退出")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Comfy")) {
        if (ImGui::MenuItem("连接 / 重连")) {
            comfy::ComfySession::Instance().Connect();
        }
        if (ImGui::MenuItem("刷新队列")) {
            comfy::ComfySession::Instance().RefreshQueue();
        }
        if (ImGui::MenuItem("刷新历史")) {
            media::MediaLibrary::Instance().Refresh(200); // P4.2 S5
        }
        if (ImGui::MenuItem("拉取 object_info")) {
            comfy::ComfySession::Instance().RefreshObjectInfo();
        }
        if (ImGui::MenuItem("中断当前任务")) {
            comfy::ComfySession::Instance().Interrupt([](comfy::OperationResult) {});
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("视图")) {
        if (ImGui::MenuItem("侧栏", "Ctrl+B", State().sideOpen)) {
            State().sideOpen = !State().sideOpen;
        }
        ImGui::Separator();
        for (const auto& a : Activities()) {
            if (ImGui::MenuItem(a.title, nullptr, State().sideView == a.id && State().sideOpen)) {
                State().sideView = a.id;
                State().sideOpen = true;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("重置布局")) {
            dock::RequestRebuild();
            log::Info("已请求重置停靠布局");
        }
        // P3.7b：节点浏览器也是独立窗口（图工具条上另有按钮）
        if (ImGui::MenuItem("节点浏览器...")) State().showNodeWindow = true;
        ImGui::MenuItem("查看器", nullptr, &State().showViewer); // G-S5：图库的独立查看器（完整功能 G-S10）
        ImGui::MenuItem("样式编辑器", nullptr, &State().showStyleEditor);
        ImGui::MenuItem("ImGui 演示", nullptr, &Settings().showDemoWindow);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("主题")) {
        for (const auto& p : theme::Presets()) {
            const bool sel = Settings().themeId == p.id;
            if (ImGui::MenuItem(p.name.c_str(), nullptr, sel)) {
                Settings().themeId = p.id;
                theme::ApplyPresetById(p.id);
                SaveSettings();
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("帮助")) {
        if (ImGui::MenuItem("关于")) State().showAbout = true;
        if (ImGui::MenuItem("快捷键…")) State().showShortcuts = true;
        ImGui::EndMenu();
    }

    auto& session = comfy::ComfySession::Instance();
    const ImVec4 col = StateColor(session.State());
    const char* status = "ShineTV Studio  ·  P2 GraphHost  ·  ";
    const char* stateLabel = comfy::ConnectionStateLabel(session.State());
    const ImVec2 ts = ImGui::CalcTextSize(status);
    const ImVec2 ts2 = ImGui::CalcTextSize(stateLabel);
    ImGui::SameLine(ImGui::GetWindowWidth() - ts.x - ts2.x - 36);
    ImGui::TextDisabled("%s", status);
    ImGui::SameLine();
    ImGui::TextColored(col, "%s", stateLabel);
    ImGui::EndMenuBar();
}

} // namespace shine::app
