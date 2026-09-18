// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/panels/assets/DrawAssetsPanel.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawAssetsPanel() {
    ImGui::TextDisabled("资源浏览器");
    ImGui::Separator();
    ImGui::TextUnformatted("工程");
    if (ImGui::TreeNode("工作流")) {
        ImGui::BulletText("demo_txt2img.json");
        ImGui::BulletText("h3_shot.json");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("图片")) {
        ImGui::BulletText("（空）");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("视频")) {
        ImGui::BulletText("（空）");
        ImGui::TreePop();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("P0 占位 — P5/P6 填充");
}

} // namespace shine::app
