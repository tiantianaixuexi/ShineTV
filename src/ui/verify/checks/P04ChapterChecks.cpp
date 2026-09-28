#include "ui/verify/checks/P04ChapterChecks.h"
#include "ui/app/AppEnvironment.h"
#include "ui/pages/shell/MainWindow.h"
#include "ui/pages/novel/ChapterFlowView.h"
#include "ui/pages/novel/DraftView.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelCommit.h"
#include "novel/NovelDb.h"
#include "novel/NovelChecks.h"
#include "novel/NovelStageLedger.h"
#include "novel/NovelGraph.h"
#include "project/Project.h"
#include "util/File.h"
#include "util/Random.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextEdit>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace shine::app::checks {

void RegisterP04ChapterChecks(MainWindow& window) {
    // P04-S5 判定：SHINE_P04_S5=<文件> 章节流水线 T1–T17
    //   判据① 全链 T1–T17 无人工干预跑完（含 T13 修复轮触发）+ 每阶段状态实时（迁移日志）
    //   判据② 产物落 work/chNN/（`03` §2.7 文件名表）+ _manifest.json；产物点开可看 JSON
    //   判据③ 断点续跑从中间阶段接着跑（P1 哈希复用 plan / P4 正文复用 / P5 缺失重跑）
    if (const std::filesystem::path s5Out = EnvironmentPath(L"SHINE_P04_S5"); !s5Out.empty()) {
        QTimer::singleShot(600, &window, [s5Out] {
            namespace fs = std::filesystem;
            using shine::novelcore::RowId;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) {
                    ++fails;
                }
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };
            const auto countPrefix = [](const std::string& text, const std::string& prefix) {
                int n = 0;
                std::size_t pos = 0;
                while ((pos = text.find(prefix, pos)) != std::string::npos) {
                    ++n;
                    pos += prefix.size();
                }
                return n;
            };
            const auto lineOf = [](const std::string& text, const std::string& marker) {
                const std::size_t p = text.find(marker);
                if (p == std::string::npos) {
                    return std::string{};
                }
                const std::size_t e = text.find('\n', p);
                return text.substr(p, e == std::string::npos ? std::string::npos : e - p);
            };

            // —— 夹具：临时项目 + 默认书 + 一卷三章 + 主角实体（mock LLM 离线全链）——
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s5-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s5000000ff";
            pf.name = "雪原哨站";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            const bool prjOk = bool(shine::project::SaveProjectFile(root, pf));
            line("fixture-project", prjOk,
                 prjOk ? "临时项目 project.json 已落盘" : "SaveProjectFile 失败");
            std::error_code ec;
shine::util::EnsureDir(root / "db");
            RowId povId = 0, volId = 0, ch1 = 0, ch2 = 0, ch3 = 0;
            {
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = root / "db" / "novel.db"}); !r) {
                    line("fixture-schema", false, r.error().message);
                } else if (auto s = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !s) {
                    line("fixture-schema", false, s.error().message);
                } else {
                    shine::novelcore::NovelGraph g(db);
                    if (auto e = g.UpsertEntity({.kind = "person",
                                                 .name = "林默",
                                                 .summary = "哨站守夜人，故事主角",
                                                 .status = "active"});
                        e) {
                        povId = *e;
                    }
                    if (auto v = g.UpsertVolume({.title = "上卷 雪原", .ord = 1, .summary = "哨站篇"});
                        v) {
                        volId = *v;
                    }
                    const auto addCh = [&g, volId, povId](int ord, const char* title) -> RowId {
                        if (auto c = g.UpsertChapter({.volume_id = volId,
                                                      .ord = ord,
                                                      .title = title,
                                                      .status = "draft",
                                                      .summary = "",
                                                      .body = "",
                                                      .pov_entity_id = povId,
                                                      .words = 0});
                            c) {
                            return *c;
                        }
                        return 0;
                    };
                    ch1 = addCh(1, "起雾");
                    ch2 = addCh(2, "灯语");
                    ch3 = addCh(3, "雪线");
                    line("fixture-schema", povId > 0 && volId > 0 && ch1 > 0 && ch2 > 0 && ch3 > 0,
                         "默认书就绪：主角实体 + 一卷三章（pov=林默）");
                }
            }
            const shine::project::ProjectRef ref = shine::project::MakeRef(pf, root);

            // —— mock LLM（探针注入；错误码用不可重试档，失败即中断不退避）——
            struct MockState {
                int plan = 0, write = 0, critic = 0, extract = 0;
                int critic_fail_first = 0; // 前 N 次 critic 给 FAIL（触发 T13 修复轮）
                bool writer_fails = false;
                bool critic_errors = false;
                bool extract_errors = false;
                RowId pov = 0;
                RowId chapter = 0;
                int ord = 0;
            };
            const auto makeMock = [](std::shared_ptr<MockState> m) {
                return [m](shine::agent::LlmRole role, std::string_view /*ins*/,
                           std::string_view /*user*/)
                           -> std::expected<std::string, shine::agent::AgentError> {
                    using shine::agent::AgentError;
                    using shine::agent::LlmRole;
                    if (role == LlmRole::Planner) {
                        ++m->plan;
                        return std::string{R"({"output_text":"{\"chapter_title\":\"雪原\",\"goal\":\"逃脱\",\"scenes\":[{\"ord\":1,\"location\":\"黑森林\",\"cast\":[\"林默\"],\"goal\":\"活下来\",\"conflict\":\"追兵\",\"result\":\"受伤\",\"emotion\":\"恐惧\"}],\"foreshadowing\":[\"plant|黑戒指\"],\"ending_hook\":\"林中异响\"}"})"};
                    }
                    if (role == LlmRole::Writer) {
                        ++m->write;
                        if (m->writer_fails) {
                            return std::unexpected(
                                AgentError{"unsupported", "mock：模拟写盘前中断（P5 续跑测试）"});
                        }
                        return std::string{
                            R"({"output_text":"雪原上只剩下风声。林默按住伤口，继续向前。"})"};
                    }
                    if (role == LlmRole::Critic) {
                        ++m->critic;
                        if (m->critic_errors) {
                            return std::unexpected(
                                AgentError{"unsupported", "mock：模拟评审中断（P5 续跑测试）"});
                        }
                        if (m->critic <= m->critic_fail_first) {
                            return std::string{R"({"output_text":"{\"passed\":false,\"issues\":[{\"type\":\"pov\",\"severity\":\"high\",\"description\":\"疑似越界\"}]}"})"};
                        }
                        return std::string{R"({"output_text":"{\"passed\":true,\"issues\":[]}"})"};
                    }
                    ++m->extract;
                    if (m->extract_errors) {
                        return std::unexpected(
                            AgentError{"unsupported", "mock：模拟提取中断（P5 续跑测试）"});
                    }
                    shine::novelcore::StateDiff d;
                    d.chapter_id = m->chapter;
                    d.input_state_hash = "sha1:p04s5-mock";
                    d.characters.push_back({.entity_id = m->pov,
                                            .body_state = "旧伤",
                                            .mind_state = "警觉",
                                            .goal = "活着走出雪原",
                                            .reason = "S5 夹具：雪原负伤前行"});
                    d.foreshadows.push_back({.op = "new",
                                             .title = "黑戒指",
                                             .content = "雪原上捡到的黑戒指",
                                             .status = "PLANTED",
                                             .setup_ch = m->ord,
                                             .importance = 70});
                    const std::string inner = shine::novelcore::StateDiffToJson(d);
                    std::string esc;
                    for (const char c : inner) {
                        if (c == '"' || c == '\\') {
                            esc += '\\';
                        }
                        if (c == '\n') {
                            esc += "\\n";
                            continue;
                        }
                        esc += c;
                    }
                    return std::string{"{\"output_text\":\"" + esc + "\"}"};
                };
            };
            const auto mockOf = [](RowId chapter, int ord, RowId pov) {
                auto m = std::make_shared<MockState>();
                m->chapter = chapter;
                m->ord = ord;
                m->pov = pov;
                return m;
            };
            const auto fileOf = [](const char* stage) {
                return std::string{shine::novelcore::StageFileName(stage)};
            };

            shine::app::ChapterFlowView cfv;
            cfv.LoadFromRef(ref, "");
            line("view-load", cfv.CurrentChapterId() == ch1,
                 cfv.CurrentChapterId() == ch1 ? "ChapterFlowView 载入默认书并选中第 1 章"
                                               : "载入后未选中首章");

            // —— 判据①：全链 T1–T17 无人工干预跑完（critic 先 FAIL 一次 → T13 修复轮触发）——
            auto mA = mockOf(ch1, 1, povId);
            mA->critic_fail_first = 1;
            cfv.SetLlm(makeMock(mA));
            QString r1;
            const bool run1 = cfv.RunChapter(ch1, false, &r1);
            line("full-chain-run", run1, r1.toStdString());
            const std::string probe1 = cfv.StageProbe().toStdString();
            bool stagesDone = countPrefix(probe1, "stage T") == 17;
            for (int i = 1; i <= 17; ++i) {
                const std::string l = lineOf(probe1, "stage T" + std::to_string(i) + "|");
                stagesDone = stagesDone && (contains(l, "|done|") || contains(l, "|skipped|"));
            }
            const std::string t13Line = lineOf(probe1, "stage T13|");
            const bool repairDone = contains(t13Line, "|done|"); // 修复轮触发 → 11_repair_receipt.json
            line("full-chain-stages", stagesDone, "17/17 节点收束 done/skipped");
            line("full-chain-repair", repairDone, t13Line);

            // 每阶段状态实时：跑中迁移日志逐阶段有 running → done
            const std::string live1 = cfv.LiveProbe().toStdString();
            bool liveOk = true;
            for (const char* code : {"T1:", "T5:", "T10:", "T11:", "T12:", "T13:", "T14:", "T15:"}) {
                liveOk = liveOk && contains(live1, std::string{code} + "running");
            }
            liveOk = liveOk && contains(live1, "T17:done");
            line("stage-live", liveOk,
                 std::string{"迁移日志 "} + std::to_string(countPrefix(live1, "live ")) +
                     " 条（T1→T17 逐阶段 running→done）");

            // —— 判据②：产物落 work/chNN/ + _manifest.json；产物点开可看 JSON ——
            const fs::path chDir = root / "work" / "ch001";
            bool artsOk = true;
            std::string arts;
            for (const char* stage : {"CONTEXT_ASSEMBLY", "SCENE_EVENT_ORDER", "CHAPTER_REVIEW",
                                      "CHAPTER_REPAIR", "STATE_EXTRACT", "STATE_VALIDATE"}) {
                const std::string f = fileOf(stage);
                const bool ex = fs::exists(chDir / f, ec);
                artsOk = artsOk && ex;
                arts += f + (ex ? " ✔ " : " ✘ ");
            }
            line("work-artifacts", artsOk, arts + "(work/ch001/)");
            const std::string manifest1 = cfv.ManifestProbe().toStdString();
            const bool manifestOk = fs::exists(chDir / "_manifest.json", ec) &&
                                    contains(manifest1, "\"stage_artifacts\"") &&
                                    contains(manifest1, "12_state_diff.json") &&
                                    contains(manifest1, "\"status\":\"");
            line("manifest", manifestOk,
                 manifestOk ? "_manifest.json 落盘（stage_artifacts 含产物指纹账）" : manifest1);

            // 产物点开可看 JSON（T5 包装产物 / T2 合并执行注记 / T11 chapters.body）
            const std::string artT5 = cfv.ArtifactProbe(QStringLiteral("T5")).toStdString();
            const std::string artT2 = cfv.ArtifactProbe(QStringLiteral("T2")).toStdString();
            const std::string artT11 = cfv.ArtifactProbe(QStringLiteral("T11")).toStdString();
            const bool artOk = contains(artT5, "CONTEXT_ASSEMBLY") &&
                               contains(artT5, "input_state_hash") &&
                               contains(artT2, "09_chapter_plan.json") &&
                               contains(artT2, "chapter_plan") && contains(artT11, "chapters.body") &&
                               contains(artT11, "\"words\"");
            line("artifact-json", artOk,
                 "T5=包装产物 / T2=合并执行注记+chapter_plan / T11=chapters.body");

            // T16 提交（G1–G5）：快照落 snapshots/ch<id>.json + chapters.status='done'
            int ch1Status = 0; // 0=查不到 1=done 2=其它
            {
                shine::db::sqlite::Database d;
                if (auto r = d.Open({.path = root / "db" / "novel.db", .readOnly = true}); r) {
                    if (auto st = d.Prepare("SELECT status FROM chapters WHERE id=?1")) {
                        (void)st->BindInt(1, static_cast<int>(ch1));
                        auto s = st->Step();
                        if (s && *s == shine::db::sqlite::StepResult::Row) {
                            ch1Status = st->ColumnText(0) == "done" ? 1 : 2;
                        }
                    }
                }
            }
            char snapBuf[32];
            std::snprintf(snapBuf, sizeof snapBuf, "ch%03lld.json", static_cast<long long>(ch1));
            const std::string snapName = snapBuf;
            const bool commitOk = fs::exists(root / "snapshots" / snapName, ec) && ch1Status == 1;
            line("commit-snapshot", commitOk,
                 commitOk ? "T16 提交：快照 snapshots/" + snapName + " + status=done"
                          : "快照缺失或 status≠done（门禁拒绝时看 commit_note）");

            // —— 判据③a：断点续跑 · 崩在 WRITE 之前 → 从 T11 写作接着跑（P1 复用 plan）——
            auto mB = mockOf(ch2, 2, povId);
            mB->writer_fails = true;
            cfv.SetLlm(makeMock(mB));
            QString r2;
            const bool run2 = cfv.RunChapter(ch2, false, &r2);
            line("resumeA-abort", !run2 && mB->plan == 1 && mB->write == 1,
                 "第 2 章在写作中断（plan 已落 09_chapter_plan.json，writer 失败）");
            const std::string rpA = cfv.ResumeProbe().toStdString();
            line("resumeA-point", rpA.find("resume-point=T11") == 0, rpA);
            auto mC = mockOf(ch2, 2, povId);
            cfv.SetLlm(makeMock(mC));
            QString r3;
            const bool run3 = cfv.RunChapter(ch2, true, &r3);
            line("resumeA-run", run3 && mC->plan == 0 && mC->write == 1,
                 "续跑 Planner 调用 " + std::to_string(mC->plan) +
                     " 次（P1 复用盘上 plan）· Writer " + std::to_string(mC->write) +
                     " 次（从 T11 接着跑）");

            // —— 判据③b：断点续跑 · 崩在 WRITE 之后 → 从 T12 评审接着跑（P1+P4 复用）——
            auto mD = mockOf(ch3, 3, povId);
            mD->critic_errors = true;
            mD->extract_errors = true;
            cfv.SetLlm(makeMock(mD));
            QString r4;
            const bool run4 = cfv.RunChapter(ch3, false, &r4);
            line("resumeB-crash", run4 && mD->plan == 1 && mD->write == 1 && mD->critic >= 1,
                 "第 3 章：正文已落库（chapters.body），评审/提取中断未回写");
            const std::string rpB = cfv.ResumeProbe().toStdString();
            line("resumeB-point",
                 rpB.find("resume-point=T12") == 0 && contains(rpB, "artifact-stage=CHAPTER_REVIEW"),
                 rpB);
            auto mE = mockOf(ch3, 3, povId);
            cfv.SetLlm(makeMock(mE));
            QString r5;
            const bool run5 = cfv.RunChapter(ch3, true, &r5);
            line("resumeB-run",
                 run5 && mE->plan == 0 && mE->write == 0 && mE->critic >= 1 && mE->extract >= 1,
                 "续跑 Planner " + std::to_string(mE->plan) + " / Writer " +
                     std::to_string(mE->write) + " 次（T1–T11 全复用，不重复产生产物）· Critic " +
                     std::to_string(mE->critic) + " / Extractor " + std::to_string(mE->extract) +
                     " 次（从 T12 接着跑）");
            const std::string probe5 = cfv.StageProbe().toStdString();
            const bool t13Skip = contains(lineOf(probe5, "stage T13|"), "|skipped|");
            line("repair-conditional", t13Skip, "评审一次通过 → T13 标跳过（条件产物不误报缺产物）");

            content += fails == 0 ? "[P04-S5] overall: PASS" : "[P04-S5] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s5Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P04-S6 判定：SHINE_P04_S6=<文件> DraftView 正文流式（PLAN §5 S6）
    //   判据：流式不卡 UI（心跳持续走）+ 逐 token 只追加末段（append-only 证据）+ 呼吸光边框
    //         + 中断后状态正确落盘 + 失败段末重试 + 手改后重算哈希（Sha1Hex）
    if (const std::filesystem::path s6Out = EnvironmentPath(L"SHINE_P04_S6"); !s6Out.empty()) {
        QTimer::singleShot(600, &window, [s6Out] {
            namespace fs = std::filesystem;
            using shine::novelcore::RowId;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) {
                    ++fails;
                }
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };
            const auto numAfter = [](const std::string& text, const std::string& from,
                                     const std::string& key) {
                const std::size_t p = text.find(from);
                if (p == std::string::npos) {
                    return -1;
                }
                const std::size_t k = text.find(key, p);
                if (k == std::string::npos) {
                    return -1;
                }
                return std::atoi(text.c_str() + k + key.size());
            };

            // —— 夹具：临时项目 + 默认书 + 一章 ——
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s6-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s6000000ff";
            pf.name = "雪原哨站";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            const bool prjOk = bool(shine::project::SaveProjectFile(root, pf));
            std::error_code ec;
shine::util::EnsureDir(root / "db");
            RowId ch1 = 0;
            {
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = root / "db" / "novel.db"}); !r) {
                    line("fixture", false, r.error().message);
                } else if (auto s = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !s) {
                    line("fixture", false, s.error().message);
                } else {
                    shine::novelcore::NovelGraph g(db);
                    if (auto v = g.UpsertVolume({.title = "上卷 雪原", .ord = 1, .summary = ""}); v) {
                        if (auto c = g.UpsertChapter({.volume_id = *v,
                                                      .ord = 1,
                                                      .title = "起雾",
                                                      .status = "draft",
                                                      .summary = "",
                                                      .body = "",
                                                      .pov_entity_id = 0,
                                                      .words = 0});
                            c) {
                            ch1 = *c;
                        }
                    }
                }
            }
            const bool fixOk = prjOk && ch1 > 0;
            line("fixture", fixOk, fixOk ? "临时项目 + 默认书 + 一章就绪" : "夹具失败");

            // —— mock writer 流式（慢发可中断 / 可注入失败；错误码用不可重试档）——
            struct StreamMock {
                int calls = 0;
                int tokens = 8;
                int delay_ms = 25;
                int fail_at = -1; // 第 N 个 token 处失败
            };
            auto mock = std::make_shared<StreamMock>();
            const auto makeStream = [mock](shine::agent::LlmRole, std::string_view, std::string_view,
                                           const std::function<void(std::string_view)>& on_delta)
                -> std::expected<std::string, shine::agent::AgentError> {
                ++mock->calls;
                // 配置在入参时快照（不活读）：运行中改 mock 不影响在飞的那次调用
                const int tokens = mock->tokens;
                const int delayMs = mock->delay_ms;
                const int failAt = mock->fail_at;
                std::string full;
                for (int i = 0; i < tokens; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    if (i == failAt) {
                        return std::unexpected(shine::agent::AgentError{
                            "unsupported", "mock：模拟流式生成中途失败（段末重试测试）"});
                    }
                    const std::string tok = "雪原第" + std::to_string(i) + "段的风声，";
                    full += tok;
                    on_delta(tok);
                }
                return full;
            };

            shine::app::DraftView dv;
            dv.SetLlm(makeStream, nullptr);
            const bool openOk = dv.OpenBook(root / "db" / "novel.db");
            dv.SelectChapter(ch1, QStringLiteral("起雾"), QString{});
            line("book-open", openOk && dv.CurrentChapterId() == ch1,
                 "DraftView 开默认书库 + 选中第 1 章（基线哈希就绪）");

            // ① 逐 token 只追加末段 + 流式不卡 UI（worker 跑线程，UI 心跳持续走）
            mock->tokens = 8;
            mock->delay_ms = 25;
            mock->fail_at = -1;
            const int hb0 = numAfter(dv.StreamProbe().toStdString(), "stream appends", "heartbeats=");
            dv.StartStream(QStringLiteral("写一段"));
            const bool doneA = dv.WaitStream(15000);
            const std::string probeA = dv.StreamProbe().toStdString();
            const int hb1 = numAfter(probeA, "stream appends", "heartbeats=");
            int appendCount = 0;
            bool prefixAll = true;
            bool totalInc = true;
            int lastTotal = 0;
            {
                std::istringstream ss(probeA);
                std::string l;
                while (std::getline(ss, l)) {
                    if (l.rfind("append #", 0) != 0) {
                        continue;
                    }
                    ++appendCount;
                    if (contains(l, "prefix_ok=1") == false) {
                        prefixAll = false;
                    }
                    const int tot = numAfter(l, "append #", "total=");
                    if (tot <= lastTotal) {
                        totalInc = false;
                    }
                    lastTotal = tot;
                }
            }
            line("stream-append-only", doneA && appendCount == 8 && prefixAll && totalInc,
                 "逐 token 只追加末段：8 个增量全 prefix_ok=1、total 单调增（旧前缀逐字不变）");
            content += "PROBE-A(done=" + std::to_string(doneA) + "):\n" + probeA;
            line("stream-ui-live", hb1 - hb0 > 3,
                 "流式不卡 UI：worker 线程跑 writer，UI 心跳 " + std::to_string(hb0) + "→" +
                     std::to_string(hb1) + "（事件循环一直活着）");

            // ② 呼吸光边框（生成中）+ 中断后状态正确落盘
            mock->tokens = 40;
            mock->delay_ms = 25;
            mock->fail_at = -1;
            dv.SelectChapter(ch1, QStringLiteral("起雾"), QString{});
            dv.StartStream(QStringLiteral("慢速长段"));
            bool breathingDuring = false;
            {
                QEventLoop peek;
                QTimer::singleShot(250, &peek,
                                   [&] {
                                       breathingDuring = dv.Breathing();
                                       peek.quit();
                                   });
                peek.exec();
            }
            dv.StopStream();
            const std::string probeB = dv.DraftProbe().toStdString();
            const int savedWords = numAfter(probeB, "draft ", "saved-words=");
            line("stream-breath", breathingDuring && contains(probeB, "breathing=0"),
                 "呼吸光边框：生成中 breathing=1（accent token 透明度脉动），收束后归 0");
            line("stream-abort-persist", contains(probeB, "body-match=1") && savedWords > 2,
                 "中断后状态正确落盘：已收 " + std::to_string(savedWords) +
                     " 字 → chapters.body（body-match=1，status 不破坏）");
            content += "PROBE-B:\n" + probeB;

            // ③ 生成失败 → 段末原因 + 「重试本段」→ 重试成功清除标记并续写
            mock->tokens = 6;
            mock->delay_ms = 5;
            mock->fail_at = 2;
            dv.SelectChapter(ch1, QStringLiteral("起雾"), QString{});
            dv.StartStream({});
            (void)dv.WaitStream(15000);
            const std::string probeC = dv.DraftProbe().toStdString();
            const bool failShown = contains(probeC, "mock：模拟流式生成中途失败");
            mock->fail_at = -1;
            const int callsBeforeRetry = mock->calls;
            dv.RetryLastSegment();
            const bool doneD = dv.WaitStream(15000);
            const std::string probeD = dv.DraftProbe().toStdString();
            const bool retryOk = doneD && contains(probeD, "failure=-") &&
                                 contains(probeD, "body-match=1") && mock->calls == callsBeforeRetry + 1;
            line("stream-retry",
                 failShown && retryOk,
                 "失败段末 ⛔+原因+「重试本段」→ 重试清除标记、接着写并落盘（failure=-）");
            content += "PROBE-C:\n" + probeC + "PROBE-D:\n" + probeD;

            // ④ 手改后重算哈希（Sha1Hex）+ 保存落盘
            const QString h0 = dv.BodyHash();
            // DraftView 的正文控件是 QTextEdit（吃 textIndent，见 DraftView.h 注），
            // 它没有 QPlainTextEdit 的 appendPlainText；insertPlainText 在光标处
            // 插入纯文本，等价。判据本身不变：哈希变化且 == Sha1Hex(正文)。
            dv.Edit()->insertPlainText(QStringLiteral("手改一行。"));
            const QString h1 = dv.BodyHash();
            const std::string expect =
                shine::novelcore::Sha1Hex(dv.Edit()->toPlainText().toStdString());
            const bool hashOk = h0 != h1 && h1.toStdString() == expect;
            dv.SaveManualEdit();
            const std::string probeE = dv.DraftProbe().toStdString();
            line("edit-hash", hashOk && contains(probeE, "body-match=1"),
                 "手改后重算哈希：hash 变化且 == Sha1Hex(正文)；「保存」落盘 body-match=1");
            content += "PROBE-E(h0=" + h0.toStdString() + " h1=" + h1.toStdString() +
                       " expect=" + expect + "):\n" + probeE;

            content += fails == 0 ? "[P04-S6] overall: PASS" : "[P04-S6] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s6Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
}

} // namespace shine::app::checks
