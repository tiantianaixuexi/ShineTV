// 节点浏览器（P3.7b）—— 与「工作流模板」窗同一套布局：左边分类、右边条目，**点击才创建**。
//
// 数据来源：`graph::NodeCatalog()`（= `/object_info` 解析后注册的目录；P3.1/P3.3）。
//   ⚠️ 目录很大（本机 2173 项 × 每条几个字符串），`NodeCatalog()` 返回的是**值拷贝** →
//   绝不能每帧调（侧栏那份是老代码，暂未动）。这里按 `RegisteredComfyNodeCount()` 当**版本号**缓存，
//   只在"注册数量变了"时重建一次。
// 落点：`graph::SpawnNodeAtViewCenter()` —— 建在当前视口中心并阶梯错开，而不是固定 (240,160) 叠在一起。
#include "app/dialogs/DrawNodeWindow.h"
#include "app/AppIncludes.h"
#include "app/ui/Widgets.h" // ui::TooltipWrapped / ui::DragSplitter

#include <limits>

namespace shine::app {
namespace {

struct NodeCat {
    std::string label; // 分类名（"全部" 或 object_info 的 category）
    std::size_t count = 0;
};

struct NodeBrowserState {
    std::size_t catalogRevision = std::numeric_limits<std::size_t>::max(); // 版本号 = 已注册节点数
    std::vector<graph::CatalogGroup> groups;
    std::vector<NodeCat> cats;
    std::size_t category = 0; // 0 = 全部
    std::string filter;
    std::string viewKey;
    struct Row {
        std::string displayName;
        std::string className;
    };
    std::vector<Row> rows; // 当前分类 + 过滤后的可见条目（拷贝一份，避免每帧拼字符串）
    std::string detailTitle;
    std::string detailBody;
};

NodeBrowserState g_node;
float g_catWidth = 210.0f; // 左栏宽度（可拖，见 ui::DragSplitter）

// 目录按"已注册数量"缓存；变了才重建（幂等注册 → 数量稳定）
void EnsureCatalog() {
    const std::size_t revision = graph::RegisteredComfyNodeCount();
    if (revision == g_node.catalogRevision) {
        return;
    }
    g_node.catalogRevision = revision;
    g_node.groups = graph::NodeCatalog();
    g_node.viewKey.clear();
}

void RebuildView() {
    const std::string lower = util::ToLower(g_node.filter);
    const std::string key = fmt::format("{}\x1f{}\x1f{}", g_node.category, lower, g_node.catalogRevision);
    if (key == g_node.viewKey) {
        return;
    }
    g_node.viewKey = key;

    g_node.cats.clear();
    std::size_t total = 0;
    for (const graph::CatalogGroup& group : g_node.groups) {
        total += group.entries.size();
    }
    g_node.cats.push_back(NodeCat{"全部", total});
    for (const graph::CatalogGroup& group : g_node.groups) {
        g_node.cats.push_back(NodeCat{group.category, group.entries.size()});
    }
    if (g_node.category >= g_node.cats.size()) {
        g_node.category = 0;
    }

    g_node.rows.clear();
    for (std::size_t gi = 0; gi < g_node.groups.size(); ++gi) {
        const graph::CatalogGroup& group = g_node.groups[gi];
        if (g_node.category != 0 && gi + 1 != g_node.category) {
            continue; // 选中了某个分类 → 只看它
        }
        for (const graph::CatalogEntry& e : group.entries) {
            if (!lower.empty()) {
                const std::string hay = util::ToLower(e.displayName + " " + e.className + " " + e.category);
                if (hay.find(lower) == std::string::npos) {
                    continue;
                }
            }
            g_node.rows.push_back(NodeBrowserState::Row{e.displayName, e.className});
        }
    }
}

void CreateNode(std::string_view className) {
    if (!graph::SpawnNodeAtViewCenter(className)) {
        log::Warn("建节点失败：{}（未注册？先连 ComfyUI 刷新 object_info）", className);
    }
}

void DrawCategoryPane() {
    ImGui::BeginChild("##node_cats", ImVec2(g_catWidth, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("分类");
    ImGui::Separator();
    for (std::size_t i = 0; i < g_node.cats.size(); ++i) {
        const NodeCat& cat = g_node.cats[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = fmt::format("{} ({})", cat.label, cat.count);
        if (ImGui::Selectable(label.c_str(), g_node.category == i)) {
            g_node.category = i;
        }
        // 列表窄 → 长分类名（`ControlNet Preprocessors/Foo/Bar`）会被截断，hover 给**多行**全名
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ui::TooltipWrapped(fmt::format("{}\n{} 个节点", cat.label, cat.count));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void DrawItemPane() {
    ImGui::BeginChild("##node_items", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-136.0f); // 右侧给「刷新 object_info」留够宽度（原先按钮被截断）
    if (ImGui::InputTextWithHint("##node_filter", "搜索：显示名 / 类名 / 分类", &g_node.filter)) {
        g_node.viewKey.clear();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(comfy::ComfySession::Instance().BaseUrl().empty());
    if (ImGui::Button("刷新 object_info", ImVec2(-1.0f, 0.0f))) {
        comfy::ComfySession::Instance().RefreshObjectInfo();
    }
    ImGui::EndDisabled();

    const int classCount = comfy::ComfySession::Instance().ObjectInfoNodeCount();
    if (classCount <= 0) {
        ImGui::TextDisabled("未连 ComfyUI：下面是内置示例节点；连上并「刷新 object_info」后换成服务器定义");
    } else {
        ImGui::TextDisabled("服务器 %s · %d 类 · 当前分类 %zu 项 · 单击建到画布视口中心（也可拖过去）",
                            comfy::ComfySession::Instance().BaseUrl().c_str(), classCount, g_node.rows.size());
    }
    ImGui::Separator();

    const float detailH = 64.0f;
    const ImVec2 listSize(0.0f, ImGui::GetContentRegionAvail().y - detailH - ImGui::GetFrameHeightWithSpacing());
    if (ImGui::BeginTable("##node_table", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          listSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("节点", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("类名", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_node.rows.size()), ImGui::GetTextLineHeightWithSpacing());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const NodeBrowserState::Row& row = g_node.rows[static_cast<std::size_t>(r)];
                ImGui::PushID(static_cast<int>(r));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(row.displayName.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    CreateNode(row.className);
                }
                if (ImGui::IsItemHovered()) {
                    const comfy::NodeTypeDef* def =
                        comfy::ComfySession::Instance().FindNodeDef(row.className); // 找不到→内置示例节点
                    g_node.detailTitle = fmt::format("{}（{}）", row.displayName, row.className);
                    if (def != nullptr) {
                        g_node.detailBody = fmt::format(
                            "分类 {} · 输入 {} / 输出 {}{}{}", def->category.empty() ? "未分类" : def->category,
                            def->inputs.size(), def->outputs.size(),
                            def->deprecated ? " · 已废弃" : "", def->experimental ? " · 实验性" : "");
                    } else {
                        g_node.detailBody = "内置示例节点（不在 object_info 里；连上 ComfyUI 后可用服务器定义）";
                    }
                }
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("SHINE_NODE_TYPE", row.className.c_str(), row.className.size() + 1);
                    ImGui::TextUnformatted(row.displayName.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", row.className.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::BeginChild("##node_detail", ImVec2(0.0f, detailH), ImGuiChildFlags_Borders);
    if (g_node.detailTitle.empty()) {
        ImGui::TextDisabled("鼠标移到节点上看详情；单击即建到画布视口中心，也可拖到「图」窗口里落点");
    } else {
        ImGui::TextUnformatted(g_node.detailTitle.c_str());
        ImGui::TextWrapped("%s", g_node.detailBody.c_str());
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace

void DrawNodeWindow() {
    if (!State().showNodeWindow) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(920.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("节点浏览器（ComfyUI）", &State().showNodeWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    EnsureCatalog();
    if (g_node.groups.empty()) {
        ImGui::TextDisabled("还没有节点目录：连上 ComfyUI 后会自动拉 /object_info（或点下面的按钮）");
        if (ImGui::Button("刷新 object_info")) {
            comfy::ComfySession::Instance().RefreshObjectInfo();
        }
        ImGui::End();
        return;
    }
    RebuildView();
    DrawCategoryPane();
    ui::DragSplitter("##node_split", &g_catWidth, 150.0f, 520.0f, ImGui::GetContentRegionAvail().y);
    ImGui::SameLine();
    DrawItemPane();
    ImGui::End();
}

} // namespace shine::app
