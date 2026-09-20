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
#include "app/novel/NovelPipeline.h" // S17：生成/连跑的共用入口（与 CLI 同一条路）
#include "app/ui/Widgets.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelDb.h"
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "novel/NovelProjects.h"
#include "novel/NovelVisual.h" // S38：链路面板要读 shots / prompt_artifacts（权威在库）
#include "novel/NovelRunLoop.h" // S9：无人值守连跑（UI 入口）
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
// —— S9 无人值守连跑（`09` §2.1–§2.5；worker 跑，状态回 UI）——
std::atomic<bool> g_runLoopRunning{false};
std::string g_runLoopStatus;
std::string g_runLoopReport; // 最近一次 stop_report.md 的绝对路径
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
            // 创建成功 → 立刻打开；打开结果（含失败原因）由 OpenProjectDb 写 g_status
            if (!OpenProjectDb(buf)) {
                g_status = fmt::format("《{}》已创建，但打开失败（{}）", buf, g_status);
            }
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
            (void)OpenProjectDb(p.name); // 失败原因已写进 g_status（状态行可见），此处无需分支
        }

        ImGui::SetCursorPos(ImVec2(start.x, start.y + rowH + 4.f));
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// 真实 LLM 回调、交叉复核判定、生成一章、连跑 —— 全部在 `NovelPipeline`（S17）：
// UI 与 headless CLI **共用同一条路**（同一份按 `LlmRole` 选模型的回调、同一份前置判定）。

// 真实流程（S15）：把「生成本章」的 worker 段抽成函数 —— 按钮与 `SHINE_NOVEL_GENERATE`
// 验收开关**共用同一条路**（同一份 worker / 进度回调 / 状态），不另造一条并行实现。
// `09` §2.1 前置③（LLM 可用）在这里判：**没配 Key 就明确说清楚并停住**，不发请求（否则
// 用户点一下等半天才从网络层报错，auto 连跑更会白跑一整套）。
void StartChapterGeneration(std::int64_t chapterId) {
    if (chapterId <= 0 || !biz::NovelDb::Instance().isOpen()) {
        g_genStatus = "请先选中一章（或先「新建空章节」）";
        return;
    }
    const auto profile = openai::ResolveActiveProfile();
    if (profile.apiKey.empty()) {
        g_genStatus = fmt::format("未配置 {} 的 API Key：设置 → LLM 里填好后重试",
                                  std::string{openai::ProviderLabel(profile.provider)});
        log::Warn("章节 #{} 未开始生成：{} 的 API Key 为空", chapterId,
                  std::string{openai::ProviderLabel(profile.provider)});
        return;
    }
    g_generating = true;
    g_cancelGen = false;
    g_streamBuf.clear();
    g_genStatus = "排队中…";
    const auto dbPath = biz::NovelDb::Instance().path();
    const std::string projectDir = util::PathToUtf8(dbPath.parent_path());
    async::RunOnWorker([chapterId, dbPath, projectDir]() {
        ::shine::db::sqlite::Database db;
        if (auto r = db.Open({.path = dbPath}); !r) {
            const std::string fail = r.error().message;
            async::PostToUi([fail]() {
                g_genStatus = "失败：" + fail;
                g_generating = false;
            });
            return;
        }
        // S17：**与 CLI（`NovelCli`）共用同一条路** —— 生成逻辑在 `NovelPipeline`，
        // 这里只负责把进度搬到 UI 线程。
        const ChapterGenOutcome out = GenerateOneChapter(
            db, chapterId, projectDir, 2, &g_cancelGen,
            [](const shine::agent::GenerateChapterProgress& p) {
                if (g_cancelGen.load()) return;
                if (!p.text_delta.empty()) {
                    async::PostToUi([d = p.text_delta]() { g_streamBuf += d; });
                }
                if (!p.note.empty()) {
                    async::PostToUi([n = p.note, ph = p.phase]() {
                        g_genStatus = fmt::format("{} · {}", shine::agent::PhaseName(ph), n);
                    });
                }
            });
        async::PostToUi([out]() {
            if (!out.ok) {
                g_genStatus = (out.error == "已取消") ? "已取消" : ("失败：" + out.error);
                g_generating = false;
                return;
            }
            g_streamBuf = out.body;
            g_genStatus = fmt::format("完成（修订 {} 次，校验重做 {} 次，{} 字）· {}", out.revisions,
                                      out.validation_retries, out.body.size(),
                                      out.commit_note.empty() ? "未回写状态" : out.commit_note);
            g_generating = false;
            if (out.state_committed) {
                Refresh();
            }
        });
    });
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

    // 章列表（**自带固定高度的滚动子窗** —— 否则几十章会把下面的「生成本章」「无人值守」顶出可视区，
    // 与本文件其它列表 `##agent_list` / `##field_defs` 同款做法）
    if (biz::NovelDb::Instance().isOpen()) {
        biz::NovelGraph gg(biz::NovelDb::Instance().raw());
        if (auto chs = gg.ListChapters(50)) {
            ImGui::TextDisabled("章节（点击选中 · %zu 章）", chs->size());
            ImGui::BeginChild("##novel_chapters", ImVec2(0, 150), ImGuiChildFlags_Borders);
            for (const auto& c : *chs) {
                const bool sel = (c.id == g_lastChapterId);
                if (ImGui::Selectable(
                        fmt::format("#{} {} [{}] {}字", c.id, c.title, c.status, c.words).c_str(),
                        sel)) {
                    g_lastChapterId = c.id;
                }
            }
            ImGui::EndChild();
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
    // S16（09-8 / `09` §2.4 验收判据）：评审模型必须 ≠ 写作模型 —— 提前说清楚，
    // 否则用户会看到「auto 被拒」却不知道去哪儿改。
    if (CrossReviewEffective()) {
        ImGui::TextDisabled("评审模型 %s ≠ 写作模型 %s（09-8 交叉复核已生效）",
                            openai::ResolveModel("critic").c_str(),
                            openai::ResolveModel("writer").c_str());
    } else {
        ImGui::TextDisabled(
            "⚠ 评审模型与写作模型相同（09-8）：请在设置 → LLM 里给 Critic 配不同模型，否则 auto 连跑会被拒");
    }
    if (ImGui::Button(g_generating ? "生成中…" : "生成本章", ImVec2(140, 0)) && !g_generating) {
        StartChapterGeneration(g_lastChapterId);
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

    // —— S9（`09` §2.1–§2.5）：无人值守连跑 ——
    ImGui::SeparatorText("无人值守（S9）");
    {
        static int modeIdx = -1;
        if (modeIdx < 0) {
            modeIdx = static_cast<int>(biz::RunModeFromString(Settings().novelRunMode));
        }
        const char* kModeNames[] = {"manual", "semi", "auto"};
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo("运行模式", &modeIdx, kModeNames, IM_ARRAYSIZE(kModeNames))) {
            Settings().novelRunMode = kModeNames[modeIdx];
            SaveSettings();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("manual=每章停 · semi=到检查点停 · auto=连跑到目标（前置不满足会被拒启动）");

        ImGui::SetNextItemWidth(110);
        if (ImGui::InputInt("连跑章数上限", &Settings().novelRunMaxChapters)) {
            if (Settings().novelRunMaxChapters < 0) {
                Settings().novelRunMaxChapters = 0;
            }
            SaveSettings();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputInt("检查点周期（章）", &Settings().novelCheckpointEvery)) {
            if (Settings().novelCheckpointEvery < 1) {
                Settings().novelCheckpointEvery = 1;
            }
            SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("自动建下一章", &Settings().novelAutoCreateChapters)) {
            SaveSettings();
        }

        if (!g_runLoopRunning) {
            if (ImGui::Button("连跑", ImVec2(140, 0)) && !g_generating &&
                biz::NovelDb::Instance().isOpen()) {
                // S15（`09` §2.1 前置③）：没配 Key 直接说清楚，不进 worker（连跑会一章都成不了）
                const auto runProfile = openai::ResolveActiveProfile();
                if (runProfile.apiKey.empty()) {
                    g_runLoopStatus = fmt::format("未配置 {} 的 API Key：设置 → LLM 里填好后重试",
                                                  std::string{openai::ProviderLabel(runProfile.provider)});
                    log::Warn("连跑未启动：{} 的 API Key 为空",
                              std::string{openai::ProviderLabel(runProfile.provider)});
                } else {
                g_runLoopRunning = true;
                g_cancelGen = false;
                g_runLoopStatus = "连跑：准备中…";
                const auto dbPath = biz::NovelDb::Instance().path();
                const auto projectDir = dbPath.parent_path();
                const std::string mode = Settings().novelRunMode;
                const int maxChapters = Settings().novelRunMaxChapters;
                const int checkpointEvery = Settings().novelCheckpointEvery;
                const bool autoCreate = Settings().novelAutoCreateChapters;
                // S16/S17：09-12 的预算上限在主线程读好再进 worker（`Settings()` 是全局单例，
                // worker 里读会与主线程保存竞争）；交叉复核等前置由 `FillPreconditions` 统一填。
                const std::int64_t maxTotalCalls = Settings().novelMaxTotalLlmCalls;
                async::RunOnWorker([dbPath, projectDir, mode, maxChapters, checkpointEvery,
                                    autoCreate, maxTotalCalls]() {
                    ::shine::db::sqlite::Database db;
                    if (auto r = db.Open({.path = dbPath}); !r) {
                        const std::string err = r.error().message;
                        async::PostToUi([err]() {
                            g_runLoopStatus = "连跑失败：" + err;
                            g_runLoopRunning = false;
                        });
                        return;
                    }
                    auto call = MakeLlmCall(&g_cancelGen);
                    biz::NovelRunLoop loop(db, call);
                    biz::RunRequest req;
                    req.project_dir = projectDir;
                    req.mode = biz::RunModeFromString(mode);
                    req.max_chapters = maxChapters;
                    req.checkpoint_every = checkpointEvery;
                    req.auto_create_chapters = autoCreate;
                    // S15（`09` §2.1 前置③）：LLM 是否可用**真判** —— 空 Key 就别启动 auto，
                    // 由 `CheckAutoPrecondition` 给出可读的拒绝原因（原先这里硬编码 true）
                    // S17：与 CLI 共用同一份前置判定（LLM 可用 / 交叉复核 / 预算上限）
                    FillPreconditions(req, maxTotalCalls);
                    req.cancel = []() { return g_cancelGen.load(); };
                    req.on_progress = [](const biz::RunProgress& p) {
                        async::PostToUi([ord = p.chapter_ord, ph = p.phase, n = p.note]() {
                            g_runLoopStatus = fmt::format("第 {} 章 · {}{}", ord, ph,
                                                          n.empty() ? "" : (" · " + n));
                        });
                    };
                    auto out = loop.Run(req);
                    async::PostToUi([out]() {
                        if (!out.refuse_reason.empty()) {
                            g_runLoopStatus = "被拒启动：" + out.refuse_reason;
                        } else {
                            g_runLoopStatus = fmt::format("连跑结束：完成 {} · 续跑跳过 {}",
                                                          out.chapters_done,
                                                          out.chapters_resumed_skipped);
                            if (out.stop) {
                                g_runLoopStatus +=
                                    fmt::format(" · {} {}", biz::StopCodeName(out.stop->code),
                                                out.stop->detail);
                            }
                        }
                        g_runLoopReport = out.stop_report_path;
                        g_runLoopRunning = false;
                    });
                });
                } // S15：无 Key 的 else 分支结束
            }
        } else {
            if (ImGui::Button("停止连跑", ImVec2(140, 0))) {
                g_cancelGen = true;
                g_runLoopStatus = "停止中…";
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g_runLoopStatus.c_str());
        if (!g_runLoopReport.empty()) {
            ImGui::TextDisabled("停止报告：%s", g_runLoopReport.c_str());
        }
    }

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
    if (auto r = ::shine::novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
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
    shine::agent::LlmCallFn mock = [&](shine::agent::LlmRole,
                                        std::string_view instructions, std::string_view user)
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

// S38：**影视化链路面板** —— V1–V11 的阶段状态一眼可见。
// 为什么该有：这些阶段此前**只能看日志、翻盘上的 JSON**（用户："没有前端页面给我看吗"）。
// 链路的**可见性**本身就是质量要求（`11` §2.7 W2「偏离/降级必须可见」的同款精神）。
// ⚠️ 本面板**只读**：不在这里发起 LLM（那需要 worker + 进度状态机，见 `StartChapterGeneration`）。
//    要跑阶段链就用命令行（面板里给出确切命令，可复制）。
void DrawPipelineCard() {
    if (g_openProject.empty() || !biz::NovelDb::Instance().isOpen()) {
        return;
    }
    auto& db = biz::NovelDb::Instance().raw();
    std::int64_t chapterId = 0;
    int chapterOrd = 0;
    if (auto list = biz::NovelGraph(db).ListChapters(200); list && !list->empty()) {
        chapterId = list->front().id; // TODO：章选择器（现在固定第一章，够用）
        chapterOrd = static_cast<int>(list->front().ord);
    }
    if (chapterId <= 0) {
        return;
    }
    const auto artDir = RootDir() / util::PathFromUtf8(g_openProject) / "work" /
                        fmt::format("ch{:03}", chapterOrd);
    app::ui::SectionText(fmt::format("影视化链路（第 {} 章）", chapterOrd));
    ImGui::TextDisabled("盘上的阶段产物：%s", util::PathToUtf8(artDir).c_str());
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::BeginTable("##pipeline", 3,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("阶段", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableSetupColumn("规模", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        const std::pair<const char*, const char*> stages[] = {
            {"V1 SCENE_BREAKDOWN", "v01_scene_breakdown.json"},
            {"V2 DIRECTOR_INTENT", "v02_director_intent.json"},
            {"V3 PERFORMANCE", "v03_performance.json"},
            {"V4 SPATIAL", "v04_spatial.json"},
            {"V5 CAMERA", "v05_camera.json"},
            {"V6 TIMELINE", "v06_timeline.json"},
            {"V7 AUDIO", "v07_audio.json"},
            {"V8 CONTINUITY（机器校验）", "v08_continuity.json"},
            {"V9 STORYBOARD", "storyboard.json"},
        };
        for (const auto& [label, file] : stages) {
            std::error_code ec;
            const auto sz = std::filesystem::file_size(artDir / file, ec);
            const bool has = !ec && sz > 0;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            if (has) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "已产出");
            } else {
                ImGui::TextDisabled("未跑");
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", has ? fmt::format("{} B", sz).c_str() : "—");
        }
        // —— 库里的结果（V9/V10/V11 的账）——
        // 分工：盘上文件是**保真/审计**（LLM 原话），**权威在库**（`01` 的架构判断）。
        biz::NovelVisual vis(db);
        auto shots = vis.ListShotsByChapter(chapterId);
        const std::size_t nShots = shots ? shots->size() : 0;
        std::size_t nIntent = 0;
        if (shots) {
            for (const auto& s : *shots) {
                if (!s.intent_json.empty() && s.intent_json != "{}") {
                    ++nIntent;
                }
            }
        }
        auto arts = vis.ListPromptArtifacts(chapterId);
        const std::size_t nArts = arts ? arts->size() : 0;
        std::size_t nRefs = 0;
        if (arts) {
            for (const auto& a : *arts) {
                if (a.stage == "V10" && !a.generation_ref.empty()) {
                    ++nRefs; // PV5：`generation_ref` 非空 = 这一镜**已经出过图**（双向可查）
                }
            }
        }
        const std::pair<const char*, std::string> libRows[] = {
            {"V9 STORYBOARD → shots 表", fmt::format("{} 镜（其中 {} 镜有 V2 导演意图）", nShots, nIntent)},
            {"V10 PROMPT_GEN → prompt_artifacts", fmt::format("{} 条", nArts)},
            {"V11 GENERATE_IMAGES → 已回填 generation_ref", fmt::format("{} 条", nRefs)},
        };
        for (const auto& [label, val] : libRows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", val.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("库");
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("跑链路（命令行，可复制）：");
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f),
                       "  ShineTVStudio.exe --novel-stages %d        # V1–V7（真 LLM）", chapterOrd);
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f),
                       "  ShineTVStudio.exe --novel-storyboard %d    # V9", chapterOrd);
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f),
                       "  ShineTVStudio.exe --novel-continuity %d    # V8（不调 LLM）", chapterOrd);
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f),
                       "  ShineTVStudio.exe --novel-prompt %d        # V10（不调 LLM）", chapterOrd);
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f),
                       "  ShineTVStudio.exe --novel-generate-images %d  # V11（需 SD checkpoint）", chapterOrd);
    ImGui::Dummy(ImVec2(0, 8));
}

void DrawNovelWindow() {
    // 本页是 dock 页（宿主 `NoScrollbar|NoScrollWithMouse`）：**页级滚动恒为 0**，滚动只发生在
    // 内部子窗（`##novel_workspace` / 工程列表）。统一钉住的是 `DrawDockedPanels::beginDock`，
    // 这里再钉一次是防御 —— 本页内容最多（工作区 + 多 Agent + 工程列表），最经不起被
    // `SetNextWindowFocus()` 的 `ScrollToBringRectIntoView` 滚下去。
    ImGui::SetScrollY(0.f);
    EnsureScan();
    // 截图验收（S9-ui）：`SHINE_NOVEL_OPEN=<书名>` 开局直接打开该工程。只在前 60 帧尝试（之后交回用户），
    // 开关名必须 ASCII（环境变量在 Windows 上按 ANSI 代码页传入，见 `Plan/坑与手法.md`）。
    {
        static int autoOpenFrames = 60;
        static const std::string want = []() -> std::string {
            const char* raw = std::getenv("SHINE_NOVEL_OPEN");
            if (raw == nullptr || *raw == '\0') {
                return {};
            }
            return util::AcpToUtf8(raw);
        }();
        if (autoOpenFrames > 0) {
            --autoOpenFrames;
            if (!want.empty() && g_openProject.empty()) {
                (void)OpenProjectDb(want);
            }
        }
    }
    // 真实流程验收（S15）：`SHINE_NOVEL_GENERATE=<chapter_id>` 开局自动跑一次「生成本章」——
    // **走的就是按钮那条路**（`StartChapterGeneration`：同一份 worker / 进度回调 / 状态），
    // 只是不用手点。`=0` → 库里第一张未完成的章。配合 `SHINE_NOVEL_OPEN=<书名>`，一条命令
    // 就能跑真实流程（`SHINE_NOVEL_OPEN=rain-signal` + `SHINE_NOVEL_GENERATE=0`）。
    {
        // ⚠️ 用**时间**而不是帧数：帧率受 GPU/后台加载影响（实测 12 秒内可能跑不到 30 帧），
        // 帧计数会让「等 N 帧」变成不确定的等待。
        static double readyAt = -1.0;
        static bool started = false;
        static const std::string genWant = []() -> std::string {
            const char* raw = std::getenv("SHINE_NOVEL_GENERATE");
            const std::string v = (raw == nullptr || *raw == '\0') ? std::string{} : util::AcpToUtf8(raw);
            // 这条**每次启动都打**：开关状态要能一眼看见（否则「没触发」时无从判断是没传还是没到帧）
            log::Info("真实流程验收：SHINE_NOVEL_GENERATE 原始=[{}] 解析=[{}]",
                      raw == nullptr ? "(null)" : raw, v);
            return v;
        }();
        if (!started && !genWant.empty()) {
            if (g_openProject.empty()) {
                readyAt = -1.0; // 还没开工程 → 重新计时
            } else if (readyAt < 0.0) {
                readyAt = ImGui::GetTime(); // 工程刚打开 → 起算
            } else if (ImGui::GetTime() - readyAt >= 1.5) {
                started = true;
                std::int64_t cid = static_cast<std::int64_t>(std::atoll(genWant.c_str()));
                if (cid <= 0 && biz::NovelDb::Instance().isOpen()) {
                    if (auto list = biz::NovelGraph(biz::NovelDb::Instance().raw()).ListChapters(200);
                        list) {
                        for (const auto& c : *list) {
                            if (c.status != "done") {
                                cid = c.id;
                                break;
                            }
                        }
                    }
                }
                log::Info("真实流程验收：自动生成章节 #{}（SHINE_NOVEL_GENERATE={}）", cid, genWant);
                StartChapterGeneration(cid);
            }
        }
    }
    ImGui::TextDisabled("目录：%s", util::PathToUtf8(RootDir()).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("刷新")) {
        Refresh();
        g_status.clear();
    }
    ImGui::Dummy(ImVec2(0, 6));
    DrawCreateCard();
    ImGui::Dummy(ImVec2(0, 8));
    // 工作区（含「生成本章」与「无人值守 S9」）必须能拿到滚动 —— 「小说」是 dock 页，
    // 宿主窗口是 `NoScrollbar|NoScrollWithMouse`（`DrawDockedPanels` 的约定：要滚的段落自带
    // inner child）。此前这里是**内联**排布 → 内容超出面板高度就被裁、面板又不会滚，等于不可达。
    // 现在：工作区 = 页面剩下的全部高度（自己滚），把底部 200px 留给工程列表（它本来就自带滚动）。
    {
        constexpr float kProjectListH = 200.f;
        const float avail = ImGui::GetContentRegionAvail().y;
        float h = avail - kProjectListH;
        if (h < 240.f) {
            h = avail > 0.f ? avail : 240.f; // 窗口太矮时工作区吃满，工程列表自然被挤掉
        }
        ImGui::BeginChild("##novel_workspace", ImVec2(0, h), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_None);
        DrawWorkspace();
        ImGui::Dummy(ImVec2(0, 10));
        DrawPipelineCard(); // S38：影视化链路面板（V1–V11 状态一眼可见）
        ImGui::EndChild();
    }
    ImGui::Dummy(ImVec2(0, 8));
    DrawProjectList(); // 自带 `##novel_list` 子窗（吃剩余空间 + 可滚）
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
