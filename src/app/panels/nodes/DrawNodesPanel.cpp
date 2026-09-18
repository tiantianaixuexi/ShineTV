// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/panels/nodes/DrawNodesPanel.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawNodesPanel() {
    ImGui::TextDisabled("节点面板");
    ImGui::Separator();
    static std::string filter; // 用 std::string + imgui_stdlib，避免定长缓冲区截断（Doc/RULES-LANG.md §13.3）
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "搜索节点...", &filter);
    ImGui::Spacing();

    auto& session = comfy::ComfySession::Instance();
    if (ImGui::Button("刷新 object_info")) {
        session.RefreshObjectInfo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d 类", session.ObjectInfoNodeCount());
    ImGui::Spacing();
    ImGui::TextDisabled("单击创建到图中；可拖到画布");

    // —— P3.3：面板数据来自 graph::NodeCatalog()（= object_info 解析结果），不再硬编码 ——
    const auto groups = graph::NodeCatalog();
    if (graph::RegisteredComfyNodeCount() == 0) {
        ImGui::TextDisabled("未连接 ComfyUI，显示内置示例节点");
    }
    const std::string needle = util::ToLower(filter);
    for (const auto& group : groups) {
        // 先按关键字过滤（子串、大小写不敏感，匹配显示名 / 类名 / 分类）
        std::vector<const graph::CatalogEntry*> visible;
        visible.reserve(group.entries.size());
        for (const auto& e : group.entries) {
            if (!needle.empty()) {
                const std::string hay = util::ToLower(e.displayName + " " + e.className + " " + e.category);
                if (hay.find(needle) == std::string::npos) {
                    continue;
                }
            }
            visible.push_back(&e);
        }
        if (visible.empty()) {
            continue;
        }
        if (!ImGui::TreeNodeEx(group.category.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)",
                               group.category.c_str(), visible.size())) {
            continue;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const graph::CatalogEntry& e = *visible[static_cast<std::size_t>(i)];
                const bool dim = e.deprecated || e.experimental; // 废弃/实验节点：标灰但仍可创建
                if (dim) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.f));
                }
                ImGui::PushID(e.className.c_str());
                if (ImGui::Selectable(e.displayName.c_str())) {
                    graph::SpawnNode(e.className, 240.f, 160.f);
                }
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("SHINE_NODE_TYPE", e.className.c_str(), e.className.size() + 1);
                    ImGui::TextUnformatted(e.displayName.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered()) {
                    if (e.deprecated) {
                        ImGui::SetTooltip("已废弃（deprecated）：仍可执行，但不建议新用");
                    } else if (e.experimental) {
                        ImGui::SetTooltip("实验性（experimental）");
                    } else {
                        ImGui::SetTooltip("%s\n%s", e.className.c_str(), group.category.c_str());
                    }
                }
                ImGui::PopID();
                if (dim) {
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::TreePop();
    }
}

} // namespace shine::app
