#include "app/novel/NovelView.h"

#include <fmt/format.h>

#include <imgui.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "agent/NovelDirector.h"
#include "agent/AgentKit.h"
#include "app/ui/Widgets.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelDb.h"
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "novel/NovelProjects.h"
#include "openai/OpenAIClient.h"
#include "openai/OpenAIConfig.h"
#include "openai/OpenAIProvider.h"
#include "theme/Theme.h"
#include "util/Encoding.h"
#include "util/Json.h"

namespace shine::app::novel {
namespace biz = ::shine::novelcore;
using ::shine::novelcore::CreateProject;
using ::shine::novelcore::Items;
using ::shine::novelcore::ProjectInfo;
using ::shine::novelcore::Refresh;
using ::shine::novelcore::RootDir;
namespace {

std::string g_newName;
std::string g_status;
bool g_scannedOnce = false;

// —— P6 生成状态（UI 线程）——
std::string g_openProject;
std::string g_streamBuf;
std::string g_genStatus;
std::atomic<bool> g_generating{false};
std::atomic<bool> g_cancelGen{false};
std::int64_t g_lastChapterId = 0;
// PROPOSED 待确认
std::int64_t g_pendingCanonId = 0;
std::string g_pendingCanonKind;
std::string g_pendingCanonLabel;

int g_selectedAgentIdx = -1;
int g_selectedEntityId = 0;
int g_fieldChapterFilter = 0;
std::string g_uiParseNote;

void DrawAgentFieldPanels();
void EnsureProjectSchema();

void EnsureProjectSchema() {
    if (!biz::NovelDb::Instance().isOpen()) {
        return;
    }
    shine::agent::AgentKit kit(biz::NovelDb::Instance().raw(), true);
    (void)kit.EnsureSchemaAndSeed();
    // 启动回收：上次进程遗留的 RUNNING/QUEUED 行会让下游误以为「还在生成」（staleSeconds=0 = 无条件）
    if (auto reaped = shine::novelcore::ReapStaleImageJobs(biz::NovelDb::Instance().raw(), 0);
        reaped && *reaped > 0) {
        g_status = fmt::format("已回收 {} 条遗留出图任务（判为失败）", *reaped);
    }
}

void EnsureScan() {
    if (!g_scannedOnce) {
        Refresh();
        g_scannedOnce = true;
    }
}

[[nodiscard]] bool OpenProjectDb(const std::string& name) {
    const auto dir = RootDir() / util::PathFromUtf8(name);
    const auto dbPath = dir / "novel.db";
    if (auto r = biz::NovelDb::Instance().Open(dbPath); !r) {
        g_status = fmt::format("打开失败：{}", r.error().message);
        return false;
    }
    g_openProject = name;
    EnsureProjectSchema();
    g_status = fmt::format("已打开《{}》", name);
    return true;
}

void DrawCreateCard() {
    const auto& tc = theme::Current();
    const ImVec4 accent(tc.accent[0], tc.accent[1], tc.accent[2], 1.f);

    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.04f));
    ImGui::BeginChild("##novel_create", ImVec2(-1, 120), ImGuiChildFlags_Borders);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Indent(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted("新建小说");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("每个工程一个目录 + novel.db（章节 / 人物 / 伏笔）");
    ImGui::Dummy(ImVec2(0, 4));

    static char buf[128] = {};
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##novel_name", "输入书名…", buf, sizeof(buf));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 1.1f, accent.y * 1.1f, accent.z * 1.1f, 1.f));
    if (ImGui::Button("创建", ImVec2(72, 0))) {
        const auto msg = CreateProject(buf);
        if (msg.empty()) {
            g_status = fmt::format("已创建《{}》", buf);
            OpenProjectDb(buf);
            buf[0] = 0;
        } else {
            g_status = msg;
        }
    }
    ImGui::PopStyleColor(2);
    if (!g_status.empty()) {
        ImGui::TextDisabled("%s", g_status.c_str());
    }
    ImGui::Unindent(12.f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}

void DrawProjectList() {
    app::ui::SectionText("小说工程");
    const auto& items = Items();
    if (items.empty()) {
        app::ui::EmptyState("还没有小说工程，先在上方新建一本");
        return;
    }

    const float rowH = 44.f;
    ImGui::BeginChild("##novel_list", ImVec2(0, 0), ImGuiChildFlags_None);
    int i = 0;
    for (const auto& p : items) {
        ImGui::PushID(i++);
        const ImVec2 start = ImGui::GetCursorPos();
        const bool hovered = ImGui::IsMouseHoveringRect(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x,
                   ImGui::GetCursorScreenPos().y + rowH));
        if (hovered) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x,
                       ImGui::GetCursorScreenPos().y + rowH),
                IM_COL32(255, 255, 255, 16));
        }

        ImGui::BeginGroup();
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::TextDisabled("%s", util::PathToUtf8(p.dir).c_str());
        if (!p.hasDb) {
            ImGui::SameLine();
            ImGui::TextDisabled("（缺 novel.db）");
        }
        if (p.name == g_openProject) {
            ImGui::SameLine();
            ImGui::TextDisabled("· 已打开");
        }
        ImGui::EndGroup();

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && p.hasDb) {
            OpenProjectDb(p.name);
        }

        ImGui::SetCursorPos(ImVec2(start.x, start.y + rowH + 4.f));
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// 真实 LLM 回调（worker）：按当前 llmProvider 走 Chat/Responses
[[nodiscard]] shine::agent::LlmCallFn MakeLlmCall(const std::atomic<bool>* cancel) {
    return [cancel](std::string_view instructions,
                    std::string_view user) -> std::expected<std::string, shine::agent::AgentError> {
        auto r = openai::LlmComplete(instructions, user, std::chrono::seconds{180}, cancel);
        if (!r) {
            return std::unexpected(shine::agent::AgentError{r.error().code, r.error().message});
        }
        return *r;
    };
}

void DrawWorkspace() {
    if (g_openProject.empty()) {
        return;
    }
    ImGui::SeparatorText("工作区");
    ImGui::Text("当前工程：%s", g_openProject.c_str());

    if (ImGui::Button("写入样例设定（P7）") && biz::NovelDb::Instance().isOpen()) {
        std::string err;
        if (SeedSampleProject(biz::NovelDb::Instance().path(), &err)) {
            g_status = "样例设定已写入";
        } else {
            g_status = err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("新建空章节") && biz::NovelDb::Instance().isOpen()) {
        biz::NovelGraph g(biz::NovelDb::Instance().raw());
        biz::NovelGraph gg(biz::NovelDb::Instance().raw());
        int ord = 1;
        if (auto list = gg.ListChapters(100)) {
            ord = static_cast<int>(list->size()) + 1;
        }
        auto id = gg.UpsertChapter(
            {.ord = ord, .title = fmt::format("第{}章", ord), .status = "draft"});
        if (id) {
            g_lastChapterId = *id;
            g_status = fmt::format("已创建章节 #{}", *id);
        }
    }

    // 章列表
    if (biz::NovelDb::Instance().isOpen()) {
        biz::NovelGraph gg(biz::NovelDb::Instance().raw());
        if (auto chs = gg.ListChapters(50)) {
            ImGui::TextDisabled("章节（点击选中）");
            for (const auto& c : *chs) {
                const bool sel = (c.id == g_lastChapterId);
                if (ImGui::Selectable(
                        fmt::format("#{} {} [{}] {}字", c.id, c.title, c.status, c.words).c_str(),
                        sel)) {
                    g_lastChapterId = c.id;
                }
            }
        }
    }

    ImGui::Dummy(ImVec2(0, 6));
    const auto profile = openai::ResolveActiveProfile();
    ImGui::Text("模型：%s · %s", std::string{openai::ProviderLabel(profile.provider)}.c_str(),
                profile.model.c_str());
    ImGui::TextDisabled("%s", profile.baseUrl.c_str());
    if (profile.apiKey.empty()) {
        ImGui::TextDisabled("未配置该 Provider 的 API Key，「生成本章」将失败");
    }
    if (ImGui::Button(g_generating ? "生成中…" : "生成本章", ImVec2(140, 0)) && !g_generating &&
        g_lastChapterId > 0 && biz::NovelDb::Instance().isOpen()) {
        g_generating = true;
        g_cancelGen = false;
        g_streamBuf.clear();
        g_genStatus = "排队中…";
        const auto chapterId = g_lastChapterId;
        const auto dbPath = biz::NovelDb::Instance().path();
        async::RunOnWorker([chapterId, dbPath]() {
            ::shine::db::sqlite::Database db;
            std::string fail;
            if (auto r = db.Open({.path = dbPath}); !r) {
                fail = r.error().message;
            } else {
                auto call = MakeLlmCall(&g_cancelGen);
                shine::agent::NovelDirector dir(db, call);
                auto out = dir.GenerateChapter(
                    {.chapter_id = chapterId, .user_hint = "续写本章", .max_revisions = 2},
                    [](const shine::agent::GenerateChapterProgress& p) {
                        if (g_cancelGen.load()) return;
                        if (!p.text_delta.empty()) {
                            async::PostToUi([d = p.text_delta]() { g_streamBuf += d; });
                        }
                        if (!p.note.empty()) {
                            async::PostToUi([n = p.note, ph = p.phase]() {
                                g_genStatus = fmt::format("{} · {}",
                                                          shine::agent::PhaseName(ph), n);
                            });
                        }
                    });
                if (!out) {
                    fail = out.error().message;
                } else {
                    async::PostToUi([body = out->body, rev = out->revisions]() {
                        g_streamBuf = body;
                        g_genStatus = fmt::format("完成（修订 {} 次，{} 字）", rev, body.size());
                        g_generating = false;
                    });
                    return;
                }
            }
            async::PostToUi([fail]() {
                g_genStatus = (fail == "已取消") ? "已取消" : ("失败：" + fail);
                g_generating = false;
            });
        });
    }
    if (g_generating) {
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(72, 0))) {
            g_cancelGen = true;
            g_genStatus = "取消中…";
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_genStatus.c_str());

    // P6.2：PROPOSED 待确认
    if (biz::NovelDb::Instance().isOpen()) {
        auto& rawDb = biz::NovelDb::Instance().raw();
        if (auto st = rawDb.Prepare(
                "SELECT id,target_kind,target_id,note FROM canon_logs WHERE status='PROPOSED' "
                "ORDER BY id DESC LIMIT 1")) {
            if (auto s = st->Step(); s && *s == ::shine::db::sqlite::StepResult::Row) {
                g_pendingCanonId = st->ColumnInt(0);
                g_pendingCanonKind = st->ColumnText(1);
                g_pendingCanonLabel = fmt::format("{} #{} {}", st->ColumnText(1), st->ColumnInt(2),
                                                  st->ColumnText(3));
            } else {
                g_pendingCanonId = 0;
            }
        }
        if (g_pendingCanonId > 0) {
            ImGui::SeparatorText("待确认（PROPOSED）");
            ImGui::TextWrapped("%s", g_pendingCanonLabel.c_str());
            if (ImGui::Button("标为 CANON")) {
                (void)rawDb.Exec(
                    fmt::format("UPDATE canon_logs SET status='CANON', note='作者确认' WHERE id={}",
                                g_pendingCanonId));
                g_pendingCanonId = 0;
                g_status = "已标为 CANON";
            }
            ImGui::SameLine();
            if (ImGui::Button("标为 NON_CANON")) {
                (void)rawDb.Exec(
                    fmt::format("UPDATE canon_logs SET status='NON_CANON' WHERE id={}",
                                g_pendingCanonId));
                g_pendingCanonId = 0;
                g_status = "已标为 NON_CANON";
            }
        }
    }

    if (!g_streamBuf.empty()) {
        ImGui::SeparatorText("正文预览");
        ImGui::BeginChild("##novel_body", ImVec2(0, 200), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(g_streamBuf.c_str());
        ImGui::EndChild();
    }

    DrawAgentFieldPanels();
}

void DrawAgentFieldPanels() {
    if (!biz::NovelDb::Instance().isOpen()) {
        return;
    }
    auto& raw = biz::NovelDb::Instance().raw();
    EnsureProjectSchema();

    ImGui::SeparatorText("多 Agent / 动态字段");
    ImGui::TextDisabled("tools_json、enum_json、identity_layers 等数组字段均按 JSON 解析后展示");
    if (!g_uiParseNote.empty()) {
        ImGui::TextDisabled("%s", g_uiParseNote.c_str());
    }

    shine::agent::AgentKit kit(raw, false);
    shine::novelcore::NovelFields fields(raw);
    biz::NovelGraph graph(raw);

    if (ImGui::BeginTabBar("##novel_agent_tabs")) {
        if (ImGui::BeginTabItem("Agents")) {
            auto agents = kit.ListAgentDefs(false);
            if (!agents || agents->empty()) {
                app::ui::EmptyState("无 Agent（点「刷新」或重开工程会 seed）");
            } else {
                if (ImGui::Button("刷新")) {
                    (void)kit.EnsureSchemaAndSeed();
                }
                ImGui::BeginChild("##agent_list", ImVec2(-1, 180), ImGuiChildFlags_Borders);
                for (std::size_t i = 0; i < agents->size(); ++i) {
                    const auto& a = (*agents)[i];
                    const bool sel = static_cast<int>(i) == g_selectedAgentIdx;
                    const std::string label =
                        fmt::format("{}  [{}]{}", a.name, a.agent_id, a.enabled ? "" : " (停用)");
                    if (ImGui::Selectable(label.c_str(), sel)) {
                        g_selectedAgentIdx = static_cast<int>(i);
                    }
                }
                ImGui::EndChild();

                if (g_selectedAgentIdx >= 0 &&
                    static_cast<std::size_t>(g_selectedAgentIdx) < agents->size()) {
                    const auto& a = (*agents)[static_cast<std::size_t>(g_selectedAgentIdx)];
                    app::ui::KvRow("id", a.agent_id);
                    app::ui::KvRow("名称", a.name);
                    app::ui::KvRow("角色", a.role_tags);
                    app::ui::KvRow("版本", fmt::format("v{}", a.version));
                    app::ui::KvRow("启用", a.enabled ? "是" : "否");
                    // tools_json 是 JSON 数组 —— 必须解析，不能当纯文本逗号硬切
                    const auto tools = util::json::ParseStringArray(a.tools_json);
                    app::ui::KvRow("tools 数", fmt::format("{}", tools.size()));
                    app::ui::KvRow("tools", util::json::ArrayBrief(a.tools_json, 12, 160));
                    if (!tools.empty()) {
                        ImGui::Indent(8.f);
                        for (const auto& t : tools) {
                            ImGui::BulletText("%s", t.c_str());
                        }
                        ImGui::Unindent(8.f);
                    } else if (!a.tools_json.empty() && a.tools_json != "[]") {
                        ImGui::TextColored(ImVec4(1.f, 0.6f, 0.3f, 1.f),
                                           "tools_json 非数组或无法解析：%s",
                                           a.tools_json.substr(0, 60).c_str());
                    }
                    app::ui::KvRow("输出提示", a.output_hint);
                    if (!a.system_prompt.empty()) {
                        ImGui::TextDisabled("prompt（前 200 字）");
                        ImGui::BeginChild("##agent_prompt", ImVec2(-1, 72), ImGuiChildFlags_Borders);
                        ImGui::TextWrapped("%s", a.system_prompt.substr(0, 200).c_str());
                        ImGui::EndChild();
                    }
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("字段定义")) {
            auto defs = fields.ListFieldDefs({});
            if (!defs || defs->empty()) {
                app::ui::EmptyState("无 field_defs");
            } else {
                app::ui::KvRow("数量", fmt::format("{}", defs->size()));
                ImGui::BeginChild("##field_defs", ImVec2(-1, 220), ImGuiChildFlags_Borders);
                if (ImGui::BeginTable("##fd_table", 6,
                                       ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("scope");
                    ImGui::TableSetupColumn("kind");
                    ImGui::TableSetupColumn("key");
                    ImGui::TableSetupColumn("title");
                    ImGui::TableSetupColumn("type");
                    ImGui::TableSetupColumn("enum / 说明");
                    ImGui::TableHeadersRow();
                    for (const auto& d : *defs) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.scope.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.entity_kind.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.field_key.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.title.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.value_type.c_str());
                        ImGui::TableNextColumn();
                        // enum_json 是数组
                        const auto enums = util::json::ParseStringArray(d.enum_json);
                        if (!enums.empty()) {
                            ImGui::Text("enum(%zu) %s", enums.size(),
                                        util::json::JoinArray(d.enum_json).c_str());
                        } else {
                            ImGui::TextUnformatted(d.description.c_str());
                        }
                        if (d.is_system) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("[系统]");
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("实体字段")) {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("entity_id（0=世界级）", &g_selectedEntityId);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("chapter 过滤", &g_fieldChapterFilter);
            ImGui::SameLine();
            if (ImGui::SmallButton("人选")) {
                if (auto list = graph.ListEntities("person", {}, 20); list && !list->empty()) {
                    g_selectedEntityId = static_cast<int>(list->front().id);
                }
            }
            if (g_selectedEntityId > 0) {
                if (auto e = graph.GetEntity(g_selectedEntityId)) {
                    app::ui::KvRow("实体",
                                   fmt::format("#{} {} [{}] {}", e->id, e->name, e->kind, e->summary));
                }
            }

            auto rows = fields.ListEntityFields(g_selectedEntityId, g_fieldChapterFilter, {});
            if (!rows || rows->empty()) {
                app::ui::EmptyState("该实体/章暂无动态字段");
            } else {
                ImGui::BeginChild("##entity_fields", ImVec2(-1, 200), ImGuiChildFlags_Borders);
                if (ImGui::BeginTable("##ef_table", 6,
                                       ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("key");
                    ImGui::TableSetupColumn("layer");
                    ImGui::TableSetupColumn("ch");
                    ImGui::TableSetupColumn("value");
                    ImGui::TableSetupColumn("value_json 类型");
                    ImGui::TableSetupColumn("value_json 解析");
                    ImGui::TableHeadersRow();
                    for (const auto& f : *rows) {
                        const auto kind = util::json::Classify(f.value_json);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(f.field_key.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(f.layer.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%lld→%lld", static_cast<long long>(f.chapter_scope),
                                    static_cast<long long>(f.chapter_to));
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(f.value_text.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(std::string{util::json::KindLabel(kind)}.c_str());
                        ImGui::TableNextColumn();
                        if (kind == util::json::ValueKind::Array) {
                            const auto items = util::json::ParseStringArray(f.value_json);
                            ImGui::Text("数组(%zu) %s", items.size(),
                                        util::json::ArrayBrief(f.value_json, 4, 80).c_str());
                            // 对象数组：例如 identity_layers
                            const auto objs = util::json::ParseObjectArray(f.value_json);
                            if (!objs.empty() && !objs[0].fields.empty()) {
                                ImGui::TextDisabled("对象数组字段:");
                                for (std::size_t oi = 0; oi < objs.size() && oi < 4; ++oi) {
                                    std::string line;
                                    for (const auto& [k, v] : objs[oi].fields) {
                                        if (!line.empty()) line += " · ";
                                        line += k + "=" + v;
                                    }
                                    ImGui::BulletText("%s", line.c_str());
                                }
                            }
                        } else if (kind == util::json::ValueKind::Object) {
                            ImGui::TextUnformatted(util::json::ValueBrief(f.value_json, 80).c_str());
                        } else {
                            ImGui::TextUnformatted(util::json::ValueBrief(f.value_json, 80).c_str());
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("路由")) {
            static char routeBuf[256] = "创建一个有卧底身份的反派";
            ImGui::InputTextWithHint("任务描述", "例如：添加一个字段表示血脉封印", routeBuf,
                                     sizeof(routeBuf));
            if (ImGui::Button("路由")) {
                if (auto r = kit.Route(routeBuf)) {
                    g_uiParseNote = fmt::format("路由结果 → {}", *r);
                } else {
                    g_uiParseNote = fmt::format("路由失败：{}", r.error().message);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("复制到状态")) {
                g_status = g_uiParseNote;
            }
            ImGui::TextWrapped("%s", g_uiParseNote.c_str());
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

} // namespace

bool SeedSampleProject(const std::filesystem::path& dbPath, std::string* err) {
    ::shine::db::sqlite::Database db;
    if (auto r = db.Open({.path = dbPath}); !r) {
        if (err) *err = r.error().message;
        return false;
    }
    biz::NovelGraph g(db);
    auto pov = g.UpsertEntity(
        {.kind = std::string{biz::kind::person}, .name = "林默", .summary = "主角，隐忍少年"});
    auto ally = g.UpsertEntity(
        {.kind = std::string{biz::kind::person}, .name = "苏月", .summary = "神秘同行者"});
    auto loc = g.UpsertEntity(
        {.kind = std::string{biz::kind::location}, .name = "黑森林", .summary = "危机四伏的密林"});
    auto rule = g.UpsertEntity({.kind = std::string{biz::kind::world_rule},
                                .name = "灵力规则",
                                .summary = "灵力不可凭空产生，须以代价交换"});
    if (!pov || !ally || !loc || !rule) {
        if (err) *err = "写入实体失败";
        return false;
    }
    (void)g.UpsertPersona({.entity_id = *pov,
                           .age = "17",
                           .personality = "隐忍、观察力强",
                           .goal = "活下去并查明身世",
                           .fear = "黑森林"});
    (void)g.UpsertCharacterStatus({.entity_id = *pov,
                                   .chapter_id = 1,
                                   .location_id = *loc,
                                   .body_state = "轻伤",
                                   .emotion_json = R"({"fear":60,"hope":40})"});
    (void)g.UpsertRelation({.from_id = *pov, .to_id = *ally, .rel_type = "ally", .strength = 40});
    (void)g.UpsertForeshadow(
        {.title = "黑戒指", .content = "林默贴身的黑戒指似乎另有来历", .status = "PLANTED",
         .importance = 80});
    auto s1 = g.UpsertSecret({.content = "苏月是宗门卧底", .entity_id = *ally, .scope = "character"});
    if (s1) {
        (void)g.SetSecretKnowledge(*s1, *ally, 1, 1);
        (void)g.SetSecretKnowledge(*s1, *pov, 0, 0); // 林默不知情
    }
    // 动态字段：身份层 + 分章宇宙（前端数组解析样例）
    {
        shine::agent::AgentKit kit(db, true);
        (void)kit.EnsureSchemaAndSeed();
        shine::novelcore::NovelFields fields(db);
        shine::novelcore::EntityFieldRow mask;
        mask.entity_id = *pov;
        mask.field_key = "public_mask";
        mask.value_text = "普通少年";
        mask.layer = "mask";
        mask.created_by = "character";
        (void)fields.UpsertEntityField(mask);
        shine::novelcore::EntityFieldRow truth;
        truth.entity_id = *pov;
        truth.field_key = "true_faction";
        truth.value_text = "北境卧底网";
        truth.layer = "true";
        truth.created_by = "character";
        (void)fields.UpsertEntityField(truth);
        shine::novelcore::EntityFieldRow identity;
        identity.entity_id = *pov;
        identity.field_key = "identity_layers";
        identity.value_text = "双面身份";
        identity.value_json =
            R"([{"layer":"mask","label":"普通少年"},{"layer":"true","label":"北境卧底"}])";
        identity.layer = "global";
        identity.created_by = "character";
        (void)fields.UpsertEntityField(identity);
        (void)fields.UpsertWorldField("active_universe", "主宇宙",
                                      R"([{"id":"main","name":"主宇宙"}])", "global", "world");
    }
    // 两章空壳
    biz::NovelGraph gg(db);
    if (auto list = gg.ListChapters(10)) ; // existing
    auto c1 = g.UpsertChapter({.ord = 1,
                               .title = "第一章 雪原来客",
                               .status = "draft",
                               .summary = "林默在雪原边境遭遇黑衣人追杀，负伤逃入黑森林。",
                               .pov_entity_id = *pov});
    auto c2 = g.UpsertChapter(
        {.ord = 2, .title = "第二章 林中同行", .status = "draft", .pov_entity_id = *pov});
    if (c1 && c2) {
        g_lastChapterId = *c2;
    }
    (void)g.LogAudit("user", "seed_sample", "project", 0, util::PathToUtf8(dbPath));
    log::Info("样例设定已写入 {}", util::PathToUtf8(dbPath));
    return true;
}

bool RunJsonArrayParseSelfCheck() {
    namespace j = ::shine::util::json;
    // tools_json 字符串数组
    const auto tools = j::ParseStringArray(R"(["get_entity","list_entities","upsert_entity_field"])");
    if (tools.size() != 3 || tools[0] != "get_entity" || tools[2] != "upsert_entity_field") {
        log::Error("前端 JSON 自检：tools 数组解析失败 n={}", tools.size());
        return false;
    }
    if (j::JoinArray(R"(["a","b"])") != "a, b") {
        log::Error("前端 JSON 自检：JoinArray 失败");
        return false;
    }
    if (j::ArrayLen(R"(["x","y","z"])") != 3 || j::ArrayLen("not-json") != 0 ||
        j::ArrayLen("") != 0 || j::ArrayLen("null") != 0 || j::ArrayLen(R"({"a":1})") != 0) {
        log::Error("前端 JSON 自检：ArrayLen 边界失败");
        return false;
    }
    // 空数组 / 非数组不崩
    if (!j::ParseStringArray("[]").empty()) {
        return false;
    }
    // identity_layers 对象数组
    const char* identity =
        R"([{"layer":"mask","label":"反派军师"},{"layer":"true","label":"卧底"}])";
    const auto objs = j::ParseObjectArray(identity);
    if (objs.size() != 2 || objs[0].Get("layer") != "mask" || objs[1].Get("label") != "卧底") {
        log::Error("前端 JSON 自检：identity_layers 对象数组解析失败");
        return false;
    }
    const auto layers = j::ObjArrayField(identity, "layer");
    if (layers.size() != 2 || layers[0] != "mask" || layers[1] != "true") {
        log::Error("前端 JSON 自检：ObjArrayField 失败");
        return false;
    }
    // enum_json
    const auto enums = j::ParseStringArray(R"(["world","plane","planet","isekai"])");
    if (enums.size() != 4 || enums[3] != "isekai") {
        return false;
    }
    // 类型分类
    if (j::Classify(R"(["a"])") != j::ValueKind::Array ||
        j::Classify(R"({"k":1})") != j::ValueKind::Object ||
        j::Classify(R"("str")") != j::ValueKind::String ||
        j::Classify("42") != j::ValueKind::Number ||
        j::Classify("{broken") != j::ValueKind::Invalid) {
        log::Error("前端 JSON 自检：Classify 失败");
        return false;
    }
    // 预览含数组标记
    const auto brief = j::ValueBrief(R"([{"id":1},{"id":2}])");
    if (brief.find("数组") == std::string::npos) {
        log::Error("前端 JSON 自检：ValueBrief={} ", brief);
        return false;
    }
    // 坏 tools_json：前端应能识别而不是当成逗号分隔文本
    const auto badTools = j::ParseStringArray(R"({not an array})");
    if (!badTools.empty()) {
        return false;
    }
    log::Info("前端 JSON 数组解析自检通过");
    if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
        if (FILE* f = std::fopen(path, "ab")) {
            const char* line = "jsonparse:ok\n";
            std::fwrite(line, 1, std::strlen(line), f);
            std::fclose(f);
        }
    }
    return true;
}

bool RunMvpSelfCheck() {
    if (!RunJsonArrayParseSelfCheck()) {
        return false;
    }
    ::shine::db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) return false;
    // 复用 Director 自检表结构：直接跑 Director mock 两章
    if (auto r = mem.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS scenes(id INTEGER PRIMARY KEY AUTOINCREMENT,chapter_id INTEGER,ord INTEGER,title TEXT,location_id INTEGER,time_label TEXT,pov_entity_id INTEGER,conflict_id INTEGER,goal TEXT,action TEXT,conflict TEXT,result TEXT,emotion TEXT,info_reveal TEXT,hook TEXT,body TEXT);
CREATE TABLE IF NOT EXISTS foreshadowings(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,content TEXT,status TEXT,setup_ch INTEGER,payoff_ch INTEGER,importance INTEGER,truth TEXT,entity_ids_json TEXT);
CREATE TABLE IF NOT EXISTS secrets(id INTEGER PRIMARY KEY AUTOINCREMENT,content TEXT,truth TEXT,reveal_ch INTEGER,reveal_condition TEXT,entity_id INTEGER,scope TEXT);
CREATE TABLE IF NOT EXISTS secret_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,secret_id INTEGER,entity_id INTEGER,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS memories(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,entity_id INTEGER,chapter_id INTEGER,content TEXT,summary TEXT,embedding_blob BLOB,created INTEGER);
CREATE TABLE IF NOT EXISTS character_status(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,chapter_id INTEGER,location_id INTEGER,body_state TEXT,mind_state TEXT,emotion_json TEXT,goal TEXT,relation_note TEXT,resource_note TEXT,secret_note TEXT,updated INTEGER);
CREATE TABLE IF NOT EXISTS entity_personas(entity_id INTEGER PRIMARY KEY,age TEXT,appearance TEXT,personality TEXT,background TEXT,"values" TEXT,desire TEXT,goal TEXT,fear TEXT,weakness TEXT,strength TEXT,ability_note TEXT,knowledge_note TEXT,memory_note TEXT);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS world_meta(key TEXT PRIMARY KEY,value TEXT);
CREATE TABLE IF NOT EXISTS writing_style(id INTEGER PRIMARY KEY,pov_mode TEXT,sentence_len TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS author_rules(id INTEGER PRIMARY KEY AUTOINCREMENT,rule TEXT,severity TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS themes(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,statement TEXT,linked_plot_id INTEGER);
CREATE TABLE IF NOT EXISTS mysteries(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,question TEXT,answer TEXT,status TEXT,ask_ch INTEGER,answer_ch INTEGER,importance INTEGER);
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
)SQL"); !r) {
        return false;
    }
    biz::NovelGraph g(mem);
    auto pov = g.UpsertEntity({.kind = std::string{biz::kind::person}, .name = "林默"});
    auto c1 = g.UpsertChapter({.ord = 1, .title = "第一章", .summary = "林默进入黑森林",
                               .pov_entity_id = pov.value_or(0)});
    auto c2 = g.UpsertChapter({.ord = 2, .title = "第二章", .pov_entity_id = pov.value_or(0)});
    if (!c1 || !c2) return false;

    // mock：第二章 writer 提示里应包含第一章摘要
    bool sawCh1InCtx = false;
    shine::agent::LlmCallFn mock = [&](std::string_view instructions, std::string_view user)
        -> std::expected<std::string, shine::agent::AgentError> {
        const std::string u{user};
        const std::string ins{instructions};
        if (u.find("黑森林") != std::string::npos) sawCh1InCtx = true;
        if (ins.find("规划器") != std::string::npos) {
            return std::string{
                R"({"output_text":"{\"chapter_title\":\"同行\",\"goal\":\"结盟\",\"scenes\":[{\"ord\":1,\"location\":\"黑森林\",\"cast\":[\"林默\"],\"goal\":\"探路\",\"conflict\":\"迷雾\",\"result\":\"结伴\",\"emotion\":\"警惕\"}],\"foreshadowing\":[],\"ending_hook\":\"异响\"}"})"};
        }
        if (ins.find("审校") != std::string::npos) {
            return std::string{R"({"output_text":"{\"passed\":true,\"issues\":[]}"})"};
        }
        if (ins.find("抽取") != std::string::npos) {
            return std::string{R"({"output_text":"{\"summary\":\"林默与苏月结伴。\",\"new_entities\":[],\"events\":[],\"foreshadow_updates\":[]}"})"};
        }
        return std::string{R"({"output_text":"林默认出了苏月，决定同行。"})"};
    };
    shine::agent::NovelDirector dir(mem, mock);
    auto out = dir.GenerateChapter({.chapter_id = *c2, .user_hint = "写第二章"});
    if (!out) {
        log::Error("MVP 自检：第二章生成失败 {}", out.error().message);
        return false;
    }
    auto ch2 = g.GetChapter(*c2);
    if (!ch2 || ch2->body.find("苏月") == std::string::npos) {
        log::Error("MVP 自检：第二章正文不符");
        return false;
    }
    if (!sawCh1InCtx) {
        log::Error("MVP 自检：第二章上下文未包含第一章信息");
        return false;
    }
    log::Info("MVP 自检通过（两章上下文一致：L2 摘要进入第二章 prompt）");
    {
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "ab");
            if (f) {
                const char* line = "mvp:ok\nPASS\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return true;
}

void DrawNovelWindow() {
    EnsureScan();
    ImGui::TextDisabled("目录：%s", util::PathToUtf8(RootDir()).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("刷新")) {
        Refresh();
        g_status.clear();
    }
    ImGui::Dummy(ImVec2(0, 6));
    DrawCreateCard();
    ImGui::Dummy(ImVec2(0, 8));
    DrawWorkspace();
    ImGui::Dummy(ImVec2(0, 8));
    DrawProjectList();
}

void DrawNovelSidePanel() {
    EnsureScan();
    const auto& items = Items();
    app::ui::KvRow("工程数", fmt::format("{}", items.size()));
    app::ui::KvRow("目录", util::PathToUtf8(RootDir()));
    if (!g_openProject.empty()) {
        app::ui::KvRow("已打开", g_openProject);
    }
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button("刷新列表", ImVec2(-1, 0))) {
        Refresh();
    }
    ImGui::Dummy(ImVec2(0, 6));
    app::ui::SectionText("最近");
    if (items.empty()) {
        app::ui::EmptyState("暂无");
        return;
    }
    const std::size_t show = items.size() < 12 ? items.size() : 12;
    for (std::size_t i = 0; i < show; ++i) {
        ImGui::BulletText("%s", items[i].name.c_str());
    }
    if (items.size() > show) {
        ImGui::TextDisabled("… 共 %zu 本", items.size());
    }
}

} // namespace shine::app::novel
