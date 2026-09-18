// 工作流模板浏览器（P3.7）—— 从 ComfyUI 拉模板清单，**点击条目才创建新的工作流**。
//
// 布局（用户 2026-09-17 指定）：左边分类、右边该分类下的工作流；不要塞在侧栏里。
// 三个来源（真机实测见 `Plan/证据.md` → P3.7）：内置模板（官方 `workflow_templates` 包）、
// custom_nodes 示例、我在前端保存的工作流；拉取/解析在 `comfy/ComfyWorkflows.*`。
//
// ⚠️ 「节点」不来自模板：模板只给「类名 + 连线 + 控件值」，节点定义全部来自运行中服务器的
// `/object_info`（`ComfySession` 拉到后由 `graph::RegisterComfyNodes` 动态注册）。
// 所以模板里出现 `MarkdownNote` 这类**前端专有**（没有 Python 实现、不在 object_info 里）的类名，
// 我们建不出来 —— 会如实列进 `ImportReport::unknownTypes`，**不静默丢弃**。
#include "app/dialogs/DrawTemplateWindow.h"
#include "app/AppIncludes.h"
#include "app/ui/Widgets.h" // ui::TooltipWrapped / ui::DragSplitter

#include "util/Encoding.h" // 自检报告：路径 UTF-8 互转
#include "util/File.h"     // 自检报告：写文件（宽字符路径安全）

namespace shine::app {
namespace {

struct CategoryGroup {
    std::string label; // 「内置模板 · Video」（同时也是条目的分组键）
    std::size_t count = 0;
};

struct TplState {
    bool listing = false; // 正在拉清单
    bool listed = false;  // 拉过至少一次
    std::string error;    // 中文
    std::vector<comfy::RemoteWorkflow> items;
    std::vector<std::string> warnings;

    // 视图（分类 / 过滤 / 可见条目）—— 变化才重建，别每帧重算 500+ 条
    std::string filter;
    std::size_t category = 0; // 0 = 全部；否则 index+1 指向 groups
    std::vector<CategoryGroup> groups;
    std::vector<std::size_t> visible;
    std::string viewKey;

    // 导入
    std::string importing;                 // 正在拉正文的 name
    std::string pending;                   // 等待"替换画布"确认的 name
    std::string message;                   // 结果（中文）
    bool messageOk = false;
    std::vector<std::string> unknownTypes; // 未注册类名（不静默丢弃）

    std::string detailTitle; // 鼠标悬停条目的详情（标题 / 来源 / 描述）
    std::string detailBody;
};

TplState g_tpl;
float g_catWidth = 230.0f; // 左栏宽度（可拖，见 ui::DragSplitter）

[[nodiscard]] std::string GroupKeyOf(const comfy::RemoteWorkflow& w) {
    return fmt::format("{} · {}", comfy::WorkflowSourceLabel(w.source), w.category);
}

[[nodiscard]] bool MatchesFilter(const comfy::RemoteWorkflow& w, std::string_view lowerFilter) {
    if (lowerFilter.empty()) {
        return true;
    }
    return util::ToLower(w.title).find(lowerFilter) != std::string::npos ||
           util::ToLower(w.name).find(lowerFilter) != std::string::npos ||
           util::ToLower(w.category).find(lowerFilter) != std::string::npos;
}

// 分类表 + 当前分类/过滤下的可见条目，一次算好（key 不变就复用）
void RebuildView() {
    const std::string lower = util::ToLower(g_tpl.filter);
    const std::string key = fmt::format("{}\x1f{}\x1f{}", g_tpl.category, lower, g_tpl.items.size());
    if (key == g_tpl.viewKey) {
        return;
    }
    g_tpl.viewKey = key;

    g_tpl.groups.clear();
    g_tpl.groups.push_back(CategoryGroup{"全部", g_tpl.items.size()});
    // 分组顺序 = 拉取顺序（模板 → 节点包示例 → 我的），不用 map 以免顺序抖动
    for (const comfy::RemoteWorkflow& w : g_tpl.items) {
        const std::string label = GroupKeyOf(w);
        auto it = std::ranges::find_if(g_tpl.groups, [&label](const CategoryGroup& g) { return g.label == label; });
        if (it == g_tpl.groups.end()) {
            g_tpl.groups.push_back(CategoryGroup{label, 1});
        } else {
            ++it->count;
        }
    }
    if (g_tpl.category >= g_tpl.groups.size()) {
        g_tpl.category = 0; // 清单换了、旧分类不存在了
    }

    const std::string currentLabel = g_tpl.category == 0 ? std::string{} : g_tpl.groups[g_tpl.category].label;
    g_tpl.visible.clear();
    for (std::size_t i = 0; i < g_tpl.items.size(); ++i) {
        const comfy::RemoteWorkflow& w = g_tpl.items[i];
        if (!MatchesFilter(w, lower)) {
            continue;
        }
        if (!currentLabel.empty() && GroupKeyOf(w) != currentLabel) {
            continue;
        }
        g_tpl.visible.push_back(i);
    }
}

void RefreshList() {
    if (g_tpl.listing) {
        return;
    }
    g_tpl.listing = true;
    g_tpl.error.clear();
    g_tpl.message.clear();
    g_tpl.viewKey.clear(); // 清单会变
    comfy::FetchWorkflowListAsync(comfy::ComfySession::Instance().BaseUrl(), [](comfy::RemoteWorkflowList r) {
        g_tpl.listing = false;
        g_tpl.listed = true;
        g_tpl.items = std::move(r.items);
        g_tpl.warnings = std::move(r.warnings);
        g_tpl.error = r.ok ? std::string{} : std::move(r.error);
    });
}

void StartImport(std::string_view name) {
    const auto it = std::ranges::find_if(g_tpl.items,
                                         [name](const comfy::RemoteWorkflow& w) { return w.name == name; });
    if (it == g_tpl.items.end()) {
        return;
    }
    const comfy::RemoteWorkflow ref = *it; // 按值：回调是异步的，别持有 items 元素的引用
    g_tpl.importing = ref.name;
    g_tpl.message.clear();
    g_tpl.unknownTypes.clear();
    comfy::FetchWorkflowAsync(comfy::ComfySession::Instance().BaseUrl(), ref,
                              [ref](comfy::RemoteWorkflowText text) {
                                  g_tpl.importing.clear();
                                  if (!text.ok) {
                                      g_tpl.messageOk = false;
                                      g_tpl.message = fmt::format("拉取失败：{}", text.error);
                                      return;
                                  }
                                  const graph::WorkflowFormat format = graph::DetectFormat(text.text);
                                  graph::ImportReport report;
                                  const bool ok = graph::ImportGraphText(
                                      text.text, fmt::format("ComfyUI 模板：{}", ref.title), &report);
                                  g_tpl.messageOk = ok;
                                  g_tpl.unknownTypes = report.unknownTypes;
                                  g_tpl.message = fmt::format("{}「{}」（格式 {}，{} 节点 / {} 连线）",
                                                              ok ? "已创建：" : "创建失败：", ref.title,
                                                              graph::WorkflowFormatLabel(format), report.nodes,
                                                              report.links);
                                  if (ok) {
                                      State().showTemplateWindow = false; // 建完就收摊，回画布
                                  }
                              });
}

// 点击条目：画布非空先问一句（免得手一滑把在做的图冲掉），空了就直接建
void RequestCreate(std::string_view name) {
    if (graph::NodeCount() > 0) {
        g_tpl.pending = std::string{name};
        ImGui::OpenPopup("替换当前画布？");
        return;
    }
    StartImport(name);
}

void DrawCategoryPane() {
    ImGui::BeginChild("##tpl_cats", ImVec2(g_catWidth, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("分类");
    ImGui::Separator();
    for (std::size_t i = 0; i < g_tpl.groups.size(); ++i) {
        const CategoryGroup& g = g_tpl.groups[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = fmt::format("{} ({})", g.label, g.count);
        if (ImGui::Selectable(label.c_str(), g_tpl.category == i)) {
            g_tpl.category = i;
        }
        // 列表窄 → 长分组名（`节点包示例 · ComfyUI-Impact-Pack`）会被截断，hover 给**多行**全名
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ui::TooltipWrapped(fmt::format("{}\n{} 个工作流", g.label, g.count));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void DrawItemPane() {
    ImGui::BeginChild("##tpl_items", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    const bool busy = g_tpl.importing.size() > 0;
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputTextWithHint("##tpl_filter", "搜索：标题 / 名字 / 分类", &g_tpl.filter)) {
        g_tpl.viewKey.clear();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || g_tpl.listing);
    if (ImGui::Button("刷新清单", ImVec2(-1.0f, 0.0f))) {
        RefreshList();
    }
    ImGui::EndDisabled();

    if (g_tpl.listing) {
        ImGui::TextDisabled("正在从 ComfyUI 拉取清单…");
    }
    if (!g_tpl.error.empty()) {
        const theme::ThemeColors& c = theme::Current();
        ImGui::TextColored(ImVec4(c.danger[0], c.danger[1], c.danger[2], 1.f), "拉取失败：%s", g_tpl.error.c_str());
    }
    for (const std::string& warning : g_tpl.warnings) {
        ImGui::TextDisabled("· %s", warning.c_str());
    }
    ImGui::TextDisabled("%zu 项（当前分类 %zu 项）· 单击条目即创建新工作流", g_tpl.items.size(), g_tpl.visible.size());
    ImGui::Separator();

    // 列表（500+ 条 → clipper；表头冻结）
    const float detailH = 70.0f;
    const ImVec2 listSize(0.0f, ImGui::GetContentRegionAvail().y - detailH - ImGui::GetFrameHeightWithSpacing());
    if (ImGui::BeginTable("##tpl_table", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          listSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("工作流", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("分类", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_tpl.visible.size()), ImGui::GetTextLineHeightWithSpacing());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const std::size_t index = g_tpl.visible[static_cast<std::size_t>(r)];
                const comfy::RemoteWorkflow& w = g_tpl.items[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (busy && g_tpl.importing == w.name) {
                    ImGui::TextDisabled("创建中… %s", w.title.c_str());
                } else if (ImGui::Selectable(w.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    RequestCreate(w.name);
                }
                if (ImGui::IsItemHovered()) {
                    g_tpl.detailTitle = fmt::format("{}（{}）", w.title, w.name);
                    g_tpl.detailBody = w.description.empty() ? w.fetchPath : w.description;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", w.category.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    // 详情（鼠标悬停的条目；点之前先看清楚）
    ImGui::BeginChild("##tpl_detail", ImVec2(0.0f, detailH), ImGuiChildFlags_Borders);
    if (g_tpl.detailTitle.empty()) {
        ImGui::TextDisabled("把鼠标移到条目上看描述；单击即创建（会替换当前画布）");
    } else {
        ImGui::TextUnformatted(g_tpl.detailTitle.c_str());
        ImGui::TextWrapped("%s", g_tpl.detailBody.c_str());
    }
    ImGui::EndChild();

    if (!g_tpl.importing.empty()) {
        ImGui::TextDisabled("正在拉取正文并生成节点… %s", g_tpl.importing.c_str());
    }
    if (!g_tpl.message.empty()) {
        const theme::ThemeColors& c = theme::Current();
        const float* col = g_tpl.messageOk ? c.success : c.danger;
        ImGui::TextColored(ImVec4(col[0], col[1], col[2], 1.f), "%s", g_tpl.message.c_str());
    }
    if (!g_tpl.unknownTypes.empty()) {
        const theme::ThemeColors& c = theme::Current();
        ImGui::TextColored(ImVec4(c.danger[0], c.danger[1], c.danger[2], 1.f),
                           "有 %zu 个节点类名服务器没给定义（这些节点没建）：", g_tpl.unknownTypes.size());
        for (const std::string& type : g_tpl.unknownTypes) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", type.c_str());
        }
        ImGui::TextDisabled("（如 MarkdownNote 属于前端专有节点，不在 /object_info 里；先连 ComfyUI 刷新一次再试）");
    }
    ImGui::EndChild();

    // 替换画布的确认（画布非空时）
    if (ImGui::BeginPopupModal("替换当前画布？", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("当前画布上已有节点，新建工作流会替换掉它们。");
        ImGui::TextDisabled("（如需保留，先点「保存」把当前图存成 graph.json）");
        ImGui::Separator();
        if (ImGui::Button("创建", ImVec2(110.0f, 0.0f))) {
            const std::string name = g_tpl.pending;
            g_tpl.pending.clear();
            ImGui::CloseCurrentPopup();
            StartImport(name);
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 0.0f))) {
            g_tpl.pending.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ———————————————————————————————————————————————————————————————— P3.7 真机自检钩子
//
// `SHINE_WORKFLOW_SELFTEST=<模板名>`（如 `video_minimax_h3_r2v`）：连上 ComfyUI 后把
// 「拉清单 → 找目标 → 拉正文 → 识别格式 → 生成节点」整条真机链路跑一遍，报告写
// `%TEMP%\shine_p37_report.txt`；顺带把窗口打开并灌入真实清单（截图验收看得到）。
// 驱动点是本函数 —— 它在 `DrawDockedPanels` 里每帧都会跑（不受侧栏/窗口开关影响）。
std::string g_p37Notes;
int g_p37Stage = 0;

void P37Note(std::string line) { g_p37Notes += line + "\n"; }

void P37WriteReport(const char* verdict) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / L"shine_p37_report.txt";
    if (util::WriteFileBytes(path, fmt::format("P3.7 远端工作流自检：{}\n\n{}", verdict, g_p37Notes))) {
        log::Warn("P3.7 自检报告已写出：{}", util::PathToUtf8(path));
    } else {
        log::Error("P3.7 自检报告写不出去：{}", util::PathToUtf8(path));
    }
}

void MaybeRunRemoteSelfTest() {
    const char* raw = std::getenv("SHINE_WORKFLOW_SELFTEST");
    if (raw == nullptr || *raw == '\0') {
        return;
    }
    const std::string want{raw};
    if (g_p37Stage != 0) {
        return; // 推进在异步回调里
    }
    // 等 /object_info 就绪（节点类型靠它注册，否则整图都是"未注册"）
    static int waitFrames = 0;
    if (comfy::ComfySession::Instance().ObjectInfoNodeCount() <= 0) {
        if (++waitFrames > 900) {
            P37Note("FAIL 等不到 /object_info（ComfyUI 没连上？）");
            P37WriteReport("FAIL");
            g_p37Stage = 3;
        }
        return;
    }
    State().showTemplateWindow = true; // 截图里要看到模板窗
    P37Note(fmt::format("object_info 就绪：{} 个节点类；H3 参考图节点已注册：{}",
                        comfy::ComfySession::Instance().ObjectInfoNodeCount(),
                        graph::IsRegistered("MiniMaxH3ReferenceToVideo") ? "是" : "否"));
    g_p37Stage = 1;
    comfy::FetchWorkflowListAsync(comfy::ComfySession::Instance().BaseUrl(),
                                  [want](comfy::RemoteWorkflowList list) {
                                      P37Note(fmt::format("{} ① 拉清单：条目={} 错误={}", list.ok ? "PASS" : "FAIL",
                                                          list.items.size(), list.error));
                                      for (const std::string& w : list.warnings) {
                                          P37Note(fmt::format("    warn: {}", w));
                                      }
                                      std::size_t templates = 0;
                                      std::size_t custom = 0;
                                      std::size_t user = 0;
                                      const comfy::RemoteWorkflow* found = nullptr;
                                      for (const comfy::RemoteWorkflow& w : list.items) {
                                          switch (w.source) {
                                          case comfy::WorkflowSource::Template: ++templates; break;
                                          case comfy::WorkflowSource::CustomNode: ++custom; break;
                                          case comfy::WorkflowSource::User: ++user; break;
                                          }
                                          if (w.name == want) {
                                              found = &w;
                                          }
                                      }
                                      P37Note(fmt::format("    内置模板={} 节点包示例={} 我的={}", templates, custom,
                                                          user));
                                      // 灌进窗口状态：截图里能看到真实的分类 + 条目
                                      g_tpl.listed = true;
                                      g_tpl.items = list.items;
                                      g_tpl.warnings = list.warnings;
                                      g_tpl.viewKey.clear();
                                      if (found == nullptr) {
                                          P37Note(fmt::format("FAIL 清单里没有「{}」", want));
                                          P37WriteReport("FAIL");
                                          g_p37Stage = 3;
                                          return;
                                      }
                                      const comfy::RemoteWorkflow ref = *found;
                                      // 顺带把分类切到目标所在那一组（截图证据 = "左分类右条目"的真实样子；
                                      // 也避免真机鼠标恰好停在窗口上把分类点走导致截图不确定）
                                      RebuildView();
                                      const std::string wantGroup = GroupKeyOf(ref);
                                      for (std::size_t i = 0; i < g_tpl.groups.size(); ++i) {
                                          if (g_tpl.groups[i].label == wantGroup) {
                                              g_tpl.category = i;
                                              break;
                                          }
                                      }
                                      g_tpl.viewKey.clear();
                                      RebuildView();
                                      P37Note(fmt::format(
                                          "    模板窗：共 {} 项 / {} 个分类；当前分类「{}」显示 {} 项",
                                          g_tpl.items.size(), g_tpl.groups.size(),
                                          g_tpl.category == 0 ? std::string_view{"全部"}
                                                              : std::string_view{g_tpl.groups[g_tpl.category].label},
                                          g_tpl.visible.size()));
                                      P37Note(fmt::format("PASS ② 找到目标：{} → {}", ref.title, ref.fetchPath));
                                      g_p37Stage = 2;
                                      comfy::FetchWorkflowAsync(comfy::ComfySession::Instance().BaseUrl(), ref,
                                                                [ref](comfy::RemoteWorkflowText text) {
                                                                    P37Note(fmt::format("{} ③ 拉正文：{} 字节 错误={}",
                                                                                        text.ok ? "PASS" : "FAIL",
                                                                                        text.text.size(), text.error));
                                                                    if (!text.ok) {
                                                                        P37WriteReport("FAIL");
                                                                        g_p37Stage = 3;
                                                                        return;
                                                                    }
                                                                    const graph::WorkflowFormat format =
                                                                        graph::DetectFormat(text.text);
                                                                    P37Note(fmt::format("    格式识别：{}",
                                                                                        graph::WorkflowFormatLabel(format)));
                                                                    graph::ImportReport report;
                                                                    const bool ok = graph::ImportGraphText(
                                                                        text.text,
                                                                        fmt::format("P3.7 自检：{}", ref.title), &report);
                                                                    P37Note(fmt::format(
                                                                        "{} ④ 生成节点：报告 {} 节点 / {} 连线；画布 {} 节点 / {} 连线",
                                                                        ok ? "PASS" : "FAIL", report.nodes,
                                                                        report.links, graph::NodeCount(),
                                                                        graph::ConnectionCount()));
                                                                    P37Note(fmt::format(
                                                                        "    未注册类名 {} 个：{}",
                                                                        report.unknownTypes.size(),
                                                                        report.unknownTypes.empty()
                                                                            ? std::string_view{"（无）"}
                                                                            : std::string_view{report.unknownTypes.front()}));
                                                                    for (const std::string& w : report.warnings) {
                                                                        P37Note(fmt::format("    warn: {}", w));
                                                                    }
                                                                    const bool good = ok && graph::NodeCount() > 0;
                                                                    P37WriteReport(good ? "PASS" : "FAIL");
                                                                    g_p37Stage = 3;
                                                                });
                                  });
}

} // namespace

void DrawTemplateWindow() {
    MaybeRunRemoteSelfTest(); // 未设 SHINE_WORKFLOW_SELFTEST 时直接返回
    if (!State().showTemplateWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(940.0f, 580.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("工作流模板（ComfyUI）", &State().showTemplateWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("服务器 %s", comfy::ComfySession::Instance().BaseUrl().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("拉取 / 刷新清单")) {
        RefreshList();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("从 ComfyUI 取：内置模板 / 节点包示例 / 我保存的工作流");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("· 节点定义来自服务器的 /object_info（%d 类）",
                        comfy::ComfySession::Instance().ObjectInfoNodeCount());
    ImGui::Separator();

    if (g_tpl.items.empty() && !g_tpl.listing) {
        ImGui::TextDisabled(g_tpl.listed ? "服务器没给出任何工作流。" : "还没拉取清单 —— 点上面的「拉取 / 刷新清单」。");
        if (!g_tpl.error.empty()) {
            ImGui::TextWrapped("%s", g_tpl.error.c_str());
        }
        ImGui::End();
        return;
    }

    RebuildView();
    DrawCategoryPane();
    ui::DragSplitter("##tpl_split", &g_catWidth, 150.0f, 520.0f, ImGui::GetContentRegionAvail().y);
    ImGui::SameLine();
    DrawItemPane();
    ImGui::End();
}

} // namespace shine::app
