// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉参数默认值，声明在头里）。
// G-S13：「属性」追加图库「图片信息」段。
#include "app/views/inspector/DrawInspectorPanel.h"
#include "app/AppIncludes.h"
#include "gallery/ExifOrientation.h"
#include "gallery/Gallery.h"
#include "util/Encoding.h"

#include <fmt/format.h>

namespace shine::app {
namespace {

[[nodiscard]] std::string FormatTime(const std::filesystem::file_time_type& t) {
    if (t == std::filesystem::file_time_type{}) {
        return "—";
    }
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void DrawGalleryImageInfo() {
    ImGui::SeparatorText("图片信息");
    const auto* item = ::shine::gallery::Model().PrimaryItem();
    if (item == nullptr) {
        ImGui::TextDisabled("图库中未选中图片");
        return;
    }
    const auto exif = ::shine::gallery::ReadExif(item->path);
    const bool swap = ::shine::gallery::SwapsAxes(exif.orientation);
    const std::uint32_t dw = swap ? item->height : item->width;
    const std::uint32_t dh = swap ? item->width : item->height;
    ImGui::TextWrapped("%s", util::PathToUtf8(item->path).c_str());
    ImGui::TextDisabled("格式：%s", item->format.empty() ? "—" : item->format.c_str());
    ImGui::TextDisabled("原始尺寸：%u × %u", item->width, item->height);
    if (swap) {
        ImGui::TextDisabled("显示尺寸：%u × %u（已按方向校正）", dw, dh);
    }
    ImGui::TextDisabled("文件大小：%llu B",
                        static_cast<unsigned long long>(item->fileSize));
    ImGui::TextDisabled("修改时间：%s", FormatTime(item->modified).c_str());
    ImGui::TextDisabled("方向：%s", ::shine::gallery::OrientationLabel(exif.orientation));
    ImGui::TextDisabled("相机：%s", exif.camera.empty() ? "—" : exif.camera.c_str());
}

} // namespace

void DrawInspectorPanel() {
    ImGui::BeginChild("##inspector_content", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    const auto selected = graph::SelectedNodes();
    if (selected.empty()) {
        ImGui::TextDisabled("未选中节点");
        ImGui::Spacing();
        ImGui::TextWrapped("在中央画布点击节点后，此处显示名称、类型与插口。");
    } else {
        ImGui::Text("已选中 %zu 个节点", selected.size());
        ImGui::Separator();
        if (selected.size() == 1) {
            const auto& n = selected[0];
            ImGui::Text("名称：%s", n.name.c_str());
            ImGui::TextDisabled("类型：%s", n.type.c_str());
            ImGui::TextDisabled("ID：%s", n.id.c_str());
            ImGui::Spacing();
            ImGui::Text("位置：(%.0f, %.0f)", n.x, n.y);
            ImGui::Text("尺寸：%.0f × %.0f", n.w, n.h);
            ImGui::Text("输入 %d  ·  输出 %d", n.inputs, n.outputs);
            ImGui::Spacing();
            if (ImGui::Button("居中到节点", ImVec2(-1, 0))) {
                graph::CenterView();
            }
            if (ImGui::Button("删除此节点", ImVec2(-1, 0))) {
                graph::DeleteSelected();
            }
        } else {
            ImGui::BeginChild("##sel_list", ImVec2(0, 160), true);
            for (const auto& n : selected) {
                ImGui::BulletText("%s (%s)", n.name.c_str(), n.type.c_str());
            }
            ImGui::EndChild();
            if (ImGui::Button("删除全部选中", ImVec2(-1, 0))) {
                graph::DeleteSelected();
            }
        }
    }

    ImGui::Spacing();
    DrawGalleryImageInfo(); // G-S13

    ImGui::Spacing();
    ImGui::SeparatorText("Comfy 连接");
    auto& session = comfy::ComfySession::Instance();
    ImGui::TextWrapped("%s", session.BaseUrl().c_str());
    DrawConnectionStatus("状态：");
    ImGui::TextDisabled("clientId：%s", session.ClientId().c_str());
    ImGui::TextDisabled("队列剩余：%d", session.Queue().QueueRemaining());
    ImGui::TextDisabled("节点类：%d", session.ObjectInfoNodeCount());
    ImGui::TextDisabled("图：%zu 节点 / %zu 连线", graph::NodeCount(), graph::ConnectionCount());
    if (ImGui::SmallButton("重连")) {
        session.Connect();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("刷新队列")) {
        session.RefreshQueue();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("object_info")) {
        session.RefreshObjectInfo();
    }
    ImGui::EndChild();
}

} // namespace shine::app