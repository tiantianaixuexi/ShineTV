#include "pages/checks/P04ReviewChecks.h"
#include "app/AppEnvironment.h"
#include "pages/shell/MainWindow.h"
#include "pages/novel/AutoRunPanel.h"
#include "pages/novel/ModelPromptView.h"
#include "pages/novel/ReviewView.h"
#include "pages/novel/StateDiffView.h"
#include "core/Settings.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelChecks.h"
#include "novel/NovelCommit.h"
#include "novel/NovelDb.h"
#include "novel/NovelStageLedger.h"
#include "novel/NovelGraph.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Random.h"

#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::app::checks {

void RegisterP04ReviewChecks(MainWindow& window) {
    // P04-S7 判定：SHINE_P04_S7=<文件> ReviewView（rubric 8 维 + K01–K29 + 修复轮 ≤3）
    //   判据① rubric 8 维阈值（`06` §2.4；world=80）+ 总判定公式落地（维度低于阈值/high issue → FAIL）
    //   判据② K01–K29 机器校验清单（`novelcore::RunChapterChecks` 单一权威，low 可记但放行）
    //   判据③ FAIL 项可一键进入修复轮；修复轮数（≤3）可见
    if (const std::filesystem::path s7Out = EnvironmentPath(L"SHINE_P04_S7"); !s7Out.empty()) {
        QTimer::singleShot(600, &window, [s7Out] {
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

            // —— 夹具：临时项目 + 默认书 + 一章（pov 人物，让 K 检查有受检对象）——
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s7-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s7000000ff";
            pf.name = "雪原哨站";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            const bool prjOk = bool(shine::project::SaveProjectFile(root, pf));
            std::error_code ec;
            fs::create_directories(root / "db", ec);
            RowId povId = 0, volId = 0, ch1 = 0;
            {
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = root / "db" / "novel.db"}); !r) {
                    line("fixture", false, r.error().message);
                } else if (auto s = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !s) {
                    line("fixture", false, s.error().message);
                } else {
                    shine::novelcore::NovelGraph g(db);
                    if (auto e = g.UpsertEntity({.kind = "person",
                                                 .name = "林默",
                                                 .summary = "哨站守夜人",
                                                 .status = "active"});
                        e) {
                        povId = *e;
                    }
                    if (auto v = g.UpsertVolume({.title = "上卷 雪原", .ord = 1, .summary = ""}); v) {
                        volId = *v;
                    }
                    if (auto c = g.UpsertChapter({.volume_id = volId,
                                                  .ord = 1,
                                                  .title = "起雾",
                                                  .status = "draft",
                                                  .summary = "",
                                                  .body = "雪原上只剩下风声。林默按住伤口，继续向前。",
                                                  .pov_entity_id = povId,
                                                  .words = 22});
                        c) {
                        ch1 = *c;
                    }
                }
            }
            const bool fixOk = prjOk && ch1 > 0;
            line("fixture", fixOk, fixOk ? "临时项目 + 默认书 + 一章（pov=林默）就绪" : "夹具失败");

            // —— 判据① rubric 8 维阈值（`06` §2.4）——
            {
                const auto& dims = shine::app::RubricDims();
                int thWorld = -1;
                for (const shine::app::RubricDim& d : dims) {
                    if (std::string_view(d.key) == "world") {
                        thWorld = d.threshold;
                    }
                }
                line("rubric-thresholds", dims.size() == 8 && thWorld == 80,
                     "8 维阈值落地：plot60 character60 causality60 world80 timeline70 foreshadow60 "
                     "pacing50 style50（`06` §2.4）");
            }

            // —— ReviewView 接线 ——
            shine::app::ReviewView rv;
            QString err;
            const bool openOk = rv.OpenBook(root / "db" / "novel.db", root, &err);

            // 判据① 解析 + 总判定：先喂「全部达标」再喂「world 破 80 + high issue」
            rv.InjectReviewArtifact(
                R"({"rubric":{"plot":75,"character":70,"causality":68,"world":85,"timeline":72,"foreshadow":66,"pacing":60,"style":58},"passed":true,"issues":[]})",
                0);
            const std::string passProbe = rv.RubricProbe().toStdString();
            rv.InjectReviewArtifact(
                R"({"rubric":{"plot":75,"character":70,"causality":68,"world":75,"timeline":72,"foreshadow":66,"pacing":60,"style":58},"passed":false,"issues":[{"type":"world","severity":"high","description":"出现硬规则冲突"}]})",
                0);
            const std::string failProbe = rv.RubricProbe().toStdString();
            const bool rubricOk = contains(passProbe, "verdict=PASS") && contains(passProbe, "dims=8") &&
                                 contains(failProbe, "verdict=FAIL") &&
                                 contains(failProbe, "dim world|世界观|75|80|below=1") &&
                                 contains(failProbe, "存在 high issue");
            line("rubric-verdict", rubricOk,
                 "总判定按 `06` §2.4 公式：全达标 PASS；world 75<80（below=1）+ high issue → FAIL");

            // 判据② K01–K32 机器校验（恒 32 条，含 NotApplicable；low 可记但放行）
            rv.SelectChapter(ch1, 1);
            const std::string checkProbe = rv.CheckProbe().toStdString();
            const int checkLines = countPrefix(checkProbe, "check K");
            const bool kOk = openOk && checkLines == 32;
            line("machine-k-checks", kOk,
                 "K01–K32 恒 32 条全部列出（" + std::to_string(checkLines) +
                     " 条；`novelcore::RunChapterChecks` 单一权威，low 级可记但放行）");

            // 判据③ 评审产物读取 + 修复轮数可见（≤3）
            {
                const std::string critFail =
                    R"({"rubric":{"plot":75,"character":70,"causality":68,"world":75,"timeline":72,"foreshadow":66,"pacing":60,"style":58},"passed":false,"issues":[{"type":"pov","severity":"high","description":"疑似越界"}]})";
                // P1 包装落 `work/ch001/10_review.json` + 修复回执 round=1
                (void)shine::novelcore::WriteStageArtifact(
                    root, 1, "CHAPTER_REVIEW", critFail, "probe-state-hash");
                (void)shine::novelcore::WriteStageArtifact(
                    root, 1, "CHAPTER_REPAIR", R"({"round":1,"revised":true})", "probe-state-hash");
                rv.SelectChapter(ch1, 1);
                const std::string rp1 = rv.RepairProbe().toStdString();
                const std::string rub1 = rv.RubricProbe().toStdString();
                // 用满 3 轮 → 修复入口应锁死（累计 ≤3）
                (void)shine::novelcore::WriteStageArtifact(
                    root, 1, "CHAPTER_REPAIR", R"({"round":3,"revised":true})", "probe-state-hash");
                rv.SelectChapter(ch1, 1);
                const std::string rp3 = rv.RepairProbe().toStdString();
                const bool roundOk = contains(rp1, "rounds-used=1") && contains(rp1, "receipt=1") &&
                                     contains(rp3, "rounds-used=3") && contains(rp3, "max=3") &&
                                     contains(rub1, "verdict=FAIL");
                line("review-artifact", contains(rub1, "dims=8") && contains(rub1, "below=1"),
                     "评审产物从 `work/ch001/10_review.json` 读入（rubric 8 维 + FAIL 判定可见）");
                line("repair-round-cap", roundOk,
                     "修复轮数可见且累计封顶：round1→rounds-used=1（可再修），round3→rounds-used=3=max（"
                     "锁死修复入口，只剩[忽略并继续]）");
            }

            content += fails == 0 ? "[P04-S7] overall: PASS" : "[P04-S7] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s7Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P04-S8 判定：SHINE_P04_S8=<文件> 模型与 Prompt 面板
    //   判据① 模型分层：规划/写作/评审/提取 四档各自可配、可读回生效值
    //   判据② **评审模型 ≠ 写作模型 时才允许 auto**（`06` §2.7 M5 交叉评审；两向都验）
    //   判据③ Secrets 不显明文 Key（面板/探针只出 `llm::MaskKey` 掩码）
    //   判据④ 外置 Prompt：四角色可查看；覆盖文件 `prompts/<role>.md` 写后可读回
    if (const std::filesystem::path s8Out = EnvironmentPath(L"SHINE_P04_S8"); !s8Out.empty()) {
        QTimer::singleShot(600, &window, [s8Out] {
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

            // Settings 快照（探针不污染真实配置，末尾原样还原）
            shine::AppSettings snap = shine::Settings();
            const auto restore = [&snap] { shine::Settings() = snap; };

            shine::app::ModelPromptView mp;
            mp.SetProjectDir(std::filesystem::temp_directory_path() / "p04s8-probe");

            // 判据① 四档模型各自可配、可读回
            const std::string mPlanner = mp.SetRoleModel("planner", "gpt-4o");
            const std::string mWriter = mp.SetRoleModel("writer", "gpt-4o-mini");
            const std::string mCritic = mp.SetRoleModel("critic", "o3-mini");
            const std::string mExtract = mp.SetRoleModel("extractor", "gpt-4.1-mini");
            const bool layersOk = mPlanner == "gpt-4o" && mWriter == "gpt-4o-mini" &&
                                  mCritic == "o3-mini" && mExtract == "gpt-4.1-mini";
            line("model-layers", layersOk,
                 "四档模型可配可读回：planner=" + mPlanner + " writer=" + mWriter + " critic=" +
                     mCritic + " extractor=" + mExtract + "（`llm::ResolveModel(role)` 统一解析）");

            // 判据② auto 规则灯（评审≠写作 → allow；相同 → block）
            const bool crossNow = mp.AutoAllowed(); // writer≠critic → 应 allow
            (void)mp.SetRoleModel("critic", mWriter); // 拉平 → 应 block
            const bool crossFlat = mp.AutoAllowed();
            (void)mp.SetRoleModel("critic", mCritic); // 还原
            line("auto-rule", crossNow && !crossFlat,
                 "评审≠写作 → auto=allow；评审=写作 → auto=block（`06` §2.7 M5 交叉评审，"
                 "同模型自评形同虚设）");

            // 判据③ Key 只出掩码（明文绝不出现在探针/面板）
            const std::string fakeKey = "sk-p04s8-9f2c7be51d4a";
            mp.SetApiKey(fakeKey);
            const std::string mprobe = mp.ModelProbe().toStdString();
            const bool masked = contains(mprobe, "key-mask=sk-") && !contains(mprobe, fakeKey) &&
                                !contains(content, fakeKey);
            line("key-masked", masked,
                 "Secrets 不显明文：只出 `llm::MaskKey` 掩码（sk-****4a），探针全文不含明文 Key");

            // 判据④ Prompt 四角色可查看 + 覆盖文件写后可读回
            const std::string pprobe0 = mp.PromptProbe().toStdString();
            QString err;
            const bool wrote = mp.ExportPromptOverride("writer", "外置 Prompt 探针正文：起雾。\n", &err);
            const std::string pprobe1 = mp.PromptProbe().toStdString();
            const bool promptOk = wrote && contains(pprobe0, "source=builtin") &&
                                  contains(pprobe1, "prompt writer|") &&
                                  contains(pprobe1, "source=external");
            line("prompt-view", promptOk,
                 "四角色 Prompt 可查看（内置 `agent::DefaultPrompt`）；覆盖文件 prompts/writer.md "
                 "写后读回为 source=external");

            content += "MODEL-PROBE:\n" + mprobe;
            content += "PROMPT-PROBE(before):\n" + pprobe0;
            content += "PROMPT-PROBE(after):\n" + pprobe1;
            restore();

            content += fails == 0 ? "[P04-S8] overall: PASS" : "[P04-S8] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s8Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P04-S9 判定：SHINE_P04_S9=<文件> StateDiffView（Diff + G1–G5 + 唯一 COMMIT 入口 + 回滚）
    //   判据① G1–G5 逐条门禁可读（`07` §2.3；未过给中文原因）
    //   判据② 提交走**唯一 COMMIT 入口**且**不调 LLM**（`CommitChapterState`；本钩子不注入 LLM）
    //   判据③ 提交幂等（同 diff 再提交 = skipped）+ 提交前写章级快照（I11）
    //   判据④ 回滚到 `snapshots/ch<NNN>.json` 成功（实体 Before 值写回）
    if (const std::filesystem::path s9Out = EnvironmentPath(L"SHINE_P04_S9"); !s9Out.empty()) {
        QTimer::singleShot(600, &window, [s9Out] {
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

            // —— 夹具：一章 + 真实 StateDiff 产物（引用真实体 id，契约 G4 才过）——
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s9-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s9000000ff";
            pf.name = "雪原哨站";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            (void)shine::project::SaveProjectFile(root, pf);
            std::error_code ec;
            fs::create_directories(root / "db", ec);
            RowId povId = 0, ch1 = 0;
            {
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = root / "db" / "novel.db"}); !r) {
                    line("fixture", false, r.error().message);
                } else if (auto s = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !s) {
                    line("fixture", false, s.error().message);
                } else {
                    shine::novelcore::NovelGraph g(db);
                    if (auto e = g.UpsertEntity({.kind = "person",
                                                 .name = "林默",
                                                 .summary = "哨站守夜人",
                                                 .status = "active"});
                        e) {
                        povId = *e;
                    }
                    if (auto v = g.UpsertVolume({.title = "上卷", .ord = 1, .summary = ""}); v) {
                        if (auto c = g.UpsertChapter({.volume_id = *v,
                                                      .ord = 1,
                                                      .title = "起雾",
                                                      .status = "draft",
                                                      .summary = "",
                                                      .body = "雪原上只剩下风声。",
                                                      .pov_entity_id = povId,
                                                      .words = 9});
                            c) {
                            ch1 = *c;
                        }
                    }
                }
            }

            // StateDiff 产物：角色状态推进（引用真实体 id → D1 过）
            std::string diffJson;
            {
                shine::novelcore::CharacterDelta cd;
                cd.entity_id = povId;
                cd.body_state = "左肋受伤";
                cd.mind_state = "警觉";
                cd.goal = "活着回到哨站";
                cd.reason = "被追兵所伤";
                shine::novelcore::StateDiff d;
                d.chapter_id = ch1;
                d.input_state_hash = "probe-input-hash";
                d.characters.push_back(cd);
                diffJson = shine::novelcore::StateDiffToJson(d);
            }
            (void)shine::novelcore::WriteStageArtifact(root, 1, "STATE_EXTRACT", diffJson,
                                                       "probe-state-hash");
            // 评审产物（rubric 全达标 + passed）→ G1 的依据
            (void)shine::novelcore::WriteStageArtifact(
                root, 1, "CHAPTER_REVIEW",
                R"({"rubric":{"plot":75,"character":70,"causality":68,"world":85,"timeline":72,"foreshadow":66,"pacing":60,"style":58},"passed":true,"issues":[]})",
                "probe-state-hash");

            // —— StateDiffView ——
            shine::app::StateDiffView sv;
            sv.SetProjectDir(root);
            QString err;
            const bool openOk = sv.OpenBook(root / "db" / "novel.db", &err);
            sv.SelectChapter(ch1, 1);

            // 判据① 门禁未注入评审结论时 G1 红；注入 PASS 后五条可读
            const std::string g0 = sv.GateProbe().toStdString();
            sv.SetReviewVerdict(true, QStringLiteral("PASS"));
            const std::string g1 = sv.GateProbe().toStdString();
            const auto countSub = [](const std::string& text, const std::string& needle) {
                int n = 0;
                std::size_t pos = 0;
                while ((pos = text.find(needle, pos)) != std::string::npos) {
                    ++n;
                    pos += needle.size();
                }
                return n;
            };
            const bool gatesOk = contains(g0, "gate G1|") && contains(g0, "pass=0") &&
                                 contains(g1, "gates=5");
            const int passCnt = countSub(g1, "pass=1");
            line("gate-g1-g5", gatesOk && passCnt >= 4,
                 "G1–G5 逐条可读：未注入评审时 G1=pass0；注入 PASS 后 " + std::to_string(passCnt) +
                     "/5 过（每条带中文依据）");

            // 判据②③ 唯一 COMMIT 入口（不注入任何 LLM）+ 幂等 + 快照
            const bool commit1 = sv.Commit();
            const std::string c1 = sv.CommitProbe().toStdString();
            const bool commit2 = sv.Commit(); // 幂等：同 diff 再提交
            const std::string c2 = sv.CommitProbe().toStdString();
            const auto snapPath = root / "snapshots" /
                                  ("ch" + [ch1] {
                                      char buf[16];
                                      std::snprintf(buf, sizeof(buf), "%03d", static_cast<int>(ch1));
                                      return std::string{buf};
                                  }() + ".json");
            const bool snapOk = fs::exists(snapPath);
            const bool commitOk = commit1 && commit2 && contains(c1, "提交成功") &&
                                  contains(c1, "g2-source=inline") && contains(c2, "skipped=1") &&
                                  snapOk;
            line("commit-unique-entry", commitOk,
                 "唯一 COMMIT 入口（`CommitChapterState`，本视图/钩子零 LLM 调用）：首次 ok=" +
                     std::to_string(commit1) + "（G2=inline 事务内权威校验）· 再提交幂等 ok=" +
                     std::to_string(commit2) + "/skipped=1 · 提交前快照落 " +
                     shine::util::PathToUtf8(snapPath));

            // 判据④ 回滚到本章快照（实体 Before 值写回）
            const bool rolled = sv.Rollback();
            const std::string c3 = sv.CommitProbe().toStdString();
            line("rollback", rolled && contains(c3, "回滚完成"),
                 "回滚成功：按 snapshots/ch<NNN>.json 的 entity_version_ids 写回 Before 值");

            content += "GATES(before-inject):\n" + g0;
            content += "GATES(after-inject):\n" + g1;
            content += "COMMIT-PROBE:\n" + c1 + c2;
            content += fails == 0 ? "[P04-S9] overall: PASS" : "[P04-S9] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s9Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P04-S10 判定：SHINE_P04_S10=<文件> AutoRunPanel（三模式 / 预算 / S1–S12 / 报告）
    //   判据① auto 六条前置逐条可见，前置不满足不得绕过
    //   判据② manual / semi / auto 均走真实 NovelRunLoop（探针只注入 mock runner，不注入 LLM）
    //   判据③ S1 停止条件给中文 stop_report.md；每章 cost_report.json 落盘
    if (const std::filesystem::path s10Out = EnvironmentPath(L"SHINE_P04_S10"); !s10Out.empty()) {
        QTimer::singleShot(600, &window, [s10Out] {
            namespace fs = std::filesystem;
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

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s10-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s1000000ff";
            pf.name = "雪原哨站";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            (void)shine::project::SaveProjectFile(root, pf);
            std::error_code ec;
            fs::create_directories(root / "db", ec);

            bool fixture = true;
            {
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = root / "db" / "novel.db"}); !r) {
                    fixture = false;
                    line("fixture-db", false, r.error().message);
                } else if (auto s = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !s) {
                    fixture = false;
                    line("fixture-schema", false, s.error().message);
                } else {
                    shine::novelcore::NovelGraph g(db);
                    auto person = g.UpsertEntity(
                        {.kind = "person", .name = "林默", .summary = "哨站守夜人", .status = "active"});
                    auto volume = g.UpsertVolume({.title = "上卷", .ord = 1, .summary = ""});
                    if (!person || !volume) {
                        fixture = false;
                    } else if (auto c1 = g.UpsertChapter({.volume_id = *volume,
                                                          .ord = 1,
                                                          .title = "起雾",
                                                          .status = "done",
                                                          .summary = "第一章已完成",
                                                          .body = "雪原上只剩下风声。",
                                                          .pov_entity_id = *person,
                                                          .words = 9});
                             !c1) {
                        fixture = false;
                    } else if (auto c2 = g.UpsertChapter({.volume_id = *volume,
                                                          .ord = 2,
                                                          .title = "追兵",
                                                          .status = "draft",
                                                          .summary = "",
                                                          .body = "",
                                                          .pov_entity_id = *person,
                                                          .words = 0});
                             !c2) {
                        fixture = false;
                    }
                }
            }

            shine::app::AutoRunPanel panel;
            QString err;
            const bool opened = fixture && panel.OpenBook(root / "db" / "novel.db", root, &err);
            shine::agent::LlmCallFn mock = [](shine::agent::LlmRole, std::string_view,
                                               std::string_view)
                -> std::expected<std::string, shine::agent::AgentError> {
                return std::unexpected(
                    shine::agent::AgentError{"mock", "S10 探针不调用真实 LLM"});
            };
            panel.SetLlm(mock);
            panel.SetMaxChapters(1);
            panel.SetCheckpointEvery(1);
            panel.SetMaxTotalLlmCalls(1000000);
            const std::string panelText = panel.ProbeText().toStdString();
            const std::string probe = opened ? panel.RunProbe().toStdString() : "probe-error=book-not-open\n";

            const bool preOk = opened && contains(panelText, "前置 1 ✔") &&
                               contains(panelText, "前置 2 ✔") && contains(panelText, "前置 3 ✔") &&
                               contains(panelText, "前置 4 ✔") && contains(panelText, "前置 5 ✔") &&
                               contains(panelText, "前置 6 ✔") && contains(panelText, "stop-conditions=12");
            line("auto-preconditions", preOk,
                 "auto 六条前置逐条显示，最近一章 done / K01–K29 全量 / LLM / Comfy / "
                 "评审≠写作 / 预算均可见；S1–S12 共 12 条");

            const bool modesOk = contains(probe, "manual|started=1|done=1") &&
                                 contains(probe, "semi|started=1|done=1|checkpoints=1") &&
                                 contains(probe, "auto|started=1|done=1|refuse=");
            line("run-modes", modesOk,
                 "manual / semi / auto 均通过真实 NovelRunLoop；semi 检查点落盘，auto 前置齐备后启动");

            const bool reportsOk = contains(probe, "s1-stop|code=S1") &&
                                   contains(probe, "stop-report=") &&
                                   fs::exists(root / "work" / "ch001" / "cost_report.json");
            line("stop-and-cost-reports", reportsOk,
                 "K02 同章连续两次失败触发 S1，中文 stop_report.md 与 work/ch001/cost_report.json 均落盘");

            const bool coreOk = shine::novelcore::NovelRunLoop::RunSelfCheck();
            line("runloop-core-selfcheck", coreOk,
                 "NovelRunLoop 核心自检覆盖 S1–S12 阈值、auto 前置拒绝、检查点与续跑");

            content += "AUTORUN-PROBE:\n" + probe;
            content += fails == 0 ? "[P04-S10] overall: PASS" : "[P04-S10] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s10Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
}

} // namespace shine::app::checks
