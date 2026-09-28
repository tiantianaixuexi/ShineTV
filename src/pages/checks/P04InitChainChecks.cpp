#include "pages/checks/P04InitChainChecks.h"
#include "app/AppEnvironment.h"
#include "pages/shell/MainWindow.h"
#include "pages/novel/InitChainView.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "project/Project.h"
#include "util/File.h"
#include "util/Random.h"

#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace shine::app::checks {

void RegisterP04InitChainChecks(MainWindow& window) {
    // P04-S4 判定：SHINE_P04_S4=<文件> 初始化链 I1–I16 + 门禁 N1–N14（可配置 + 人工审批）+ 前情导入
    if (const std::filesystem::path s4Out = EnvironmentPath(L"SHINE_P04_S4"); !s4Out.empty()) {
        QTimer::singleShot(600, &window, [s4Out] {
            namespace fs = std::filesystem;
            std::string text;
            bool allPass = true;
            const auto line = [&text, &allPass](std::string_view name, bool pass,
                                               std::string_view detail) {
                text += std::string{name} + (pass ? ": PASS " : ": FAIL ") + std::string{detail} + '\n';
                allPass = allPass && pass;
            };
            const auto lineOf = [](const std::string& s, std::string_view key) {
                std::size_t pos = 0;
                while (pos < s.size()) {
                    std::size_t end = s.find('\n', pos);
                    if (end == std::string::npos) {
                        end = s.size();
                    }
                    if (s.compare(pos, key.size(), key) == 0) {
                        return s.substr(pos, end - pos);
                    }
                    pos = end + 1;
                }
                return std::string{};
            };
            const auto countPrefix = [](const std::string& s, std::string_view key) {
                int n = 0;
                std::size_t pos = 0;
                while (pos < s.size()) {
                    std::size_t end = s.find('\n', pos);
                    if (end == std::string::npos) {
                        end = s.size();
                    }
                    if (s.compare(pos, key.size(), key) == 0) {
                        ++n;
                    }
                    pos = end + 1;
                }
                return n;
            };
            const auto grabInt = [](const std::string& s, std::string_view key) {
                const std::size_t p = s.find(key);
                if (p == std::string::npos) {
                    return -1;
                }
                std::size_t i = p + key.size();
                int v = 0;
                bool any = false;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                    v = v * 10 + (s[i] - '0');
                    ++i;
                    any = true;
                }
                return any ? v : -1;
            };
            const auto queryI64 = [](shine::db::sqlite::Database& d, std::string_view sql) {
                std::int64_t n = 0;
                if (auto st = d.Prepare(sql)) {
                    auto s = st->Step();
                    if (s && *s == shine::db::sqlite::StepResult::Row) {
                        n = st->ColumnInt(0);
                    }
                }
                return n;
            };

            // —— 夹具：临时项目 + 两本书（前作《灯语回声·上》：实体/伏笔/章节正文；本作 = 默认书空库）——
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s4-" + shine::util::RandomHex(8));
            std::error_code ec;
            fs::create_directories(root / "db", ec); // 夹具建库前 create_directories
            fs::create_directories(root / "books" / "灯语回声·上" / "db", ec);
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s4000000ff";
            pf.name = "灯语回声系列";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            (void)shine::project::SaveProjectFile(root, pf);
            const fs::path defDb = root / "db" / "novel.db";
            const fs::path priorDb = root / "books" / "灯语回声·上" / "db" / "novel.db";
            {
                shine::db::sqlite::Database db;
                (void)db.Open({.path = priorDb});
                (void)shine::novelcore::NovelDb::ApplyCanonicalSchema(db);
                shine::novelcore::NovelGraph g(db);
                (void)g.SetWorldMeta("book_title", "灯语回声·上");
                const auto addEntity = [&g](std::string_view kind, std::string_view name,
                                            std::string_view summary, std::string_view canonical,
                                            int sourceCh) {
                    shine::novelcore::EntityRow row;
                    row.kind = std::string{kind};
                    row.name = std::string{name};
                    row.summary = std::string{summary};
                    row.meta_json = std::string{"{\"canonical\":\""} + std::string{canonical} +
                                    "\",\"aliases\":[],\"source_book\":\"灯语回声·上\",\"source_ch\":" +
                                    std::to_string(sourceCh) + "}";
                    (void)g.UpsertEntity(row);
                };
                addEntity("person", "苏黎",
                          "灯塔看守人，前作主角；终态：接过守灯人契约，成为新一代守灯人。", "Su Li", 2);
                addEntity("person", "顾行舟",
                          "来客，知道灯语的来历；终态：交还铜钥匙后回到海上，身世已明。", "Gu Xingzhou", 1);
                addEntity("location", "灯塔",
                          "海岸灯塔，故事起点；地下室藏着守灯人档案室。", "Deng Tower", 1);
                addEntity("world_rule", "旧世灯规",
                          "守灯人契约规定灯语不可外传，违者灯灭；这是前作世界观的根。", "Old Lamp Rule", 1);
                shine::novelcore::ForeshadowRow fs1;
                fs1.title = "铜钥匙之谜";
                fs1.content = "铜钥匙能开灯塔地下室的守灯人档案室";
                fs1.status = "RESOLVED";
                fs1.setup_ch = 1;
                fs1.payoff_ch = 4;
                fs1.importance = 80;
                fs1.truth = "钥匙开的是守灯人档案室，里面是历代契约";
                (void)g.UpsertForeshadow(fs1);
                shine::novelcore::ForeshadowRow fs2;
                fs2.title = "回声的来源";
                fs2.content = "雪原上的回声并非风声";
                fs2.status = "PLANTED";
                fs2.setup_ch = 2;
                fs2.payoff_ch = 6;
                fs2.importance = 60;
                (void)g.UpsertForeshadow(fs2); // 未完结：不进前情摘要（决策 §7 只取已完结伏笔）
                shine::novelcore::VolumeRow vr;
                vr.title = "上卷";
                vr.ord = 1;
                auto vid = g.UpsertVolume(vr);
                for (int c = 1; c <= 5; ++c) {
                    shine::novelcore::ChapterRow ch;
                    ch.volume_id = vid.value_or(0);
                    ch.ord = c;
                    ch.title = "第 " + std::to_string(c) + " 章　灯下回声";
                    ch.summary = "第 " + std::to_string(c) +
                                 " 章结案摘要：灯语引路，回声指归途；苏黎与顾行舟在雪夜里各得其所（前作既定事实，不可改写）。";
                    ch.body = "正文 " + std::to_string(c) +
                              "：雪原上只剩下风声，一盏灯把迷路的人带回家。";
                    ch.words = static_cast<int>(ch.body.size());
                    ch.status = "done";
                    (void)g.UpsertChapter(ch);
                }
            }
            { // 本作（默认书）：空库
                shine::db::sqlite::Database db;
                (void)db.Open({.path = defDb});
                (void)shine::novelcore::NovelDb::ApplyCanonicalSchema(db);
            }
            const shine::project::ProjectRef ref = shine::project::MakeRef(pf, root);
            const auto priorRowCount = [&queryI64](const fs::path& dbPath) {
                shine::db::sqlite::Database d;
                if (auto r = d.Open({.path = dbPath, .readOnly = true, .create = false}); !r) {
                    return static_cast<std::int64_t>(-1);
                }
                return queryI64(d,
                                "SELECT (SELECT COUNT(*) FROM entities)+(SELECT COUNT(*) FROM chapters)"
                                "+(SELECT COUNT(*) FROM foreshadowings)+(SELECT COUNT(*) FROM volumes)"
                                "+(SELECT COUNT(*) FROM world_meta)+(SELECT COUNT(*) FROM secrets)"
                                "+(SELECT COUNT(*) FROM plots)+(SELECT COUNT(*) FROM mysteries)");
            };
            const std::int64_t priorBefore = priorRowCount(priorDb);

            shine::app::InitChainView icv;
            icv.LoadFromRef(ref, ""); // lastNovel 空 = 默认书（本作）

            // —— 判据 ①：I1–I16 共 16 阶段全在流水线（阶段名/产物/状态/可断点续跑）——
            const std::string stageProbe = icv.StageProbe().toStdString();
            bool stagesOk = countPrefix(stageProbe, "stage I") == 16 &&
                            stageProbe.find("stages=16") != std::string::npos;
            for (const char* code :
                 {"INIT_CONCEPT", "INIT_WORLD", "INIT_POWER", "INIT_CHARACTERS", "INIT_RELATIONS",
                  "INIT_LOCATIONS", "INIT_FACTIONS", "INIT_ITEMS", "INIT_SECRETS", "INIT_MYSTERIES",
                  "INIT_PLOTS", "INIT_STYLE", "INIT_FORESHADOW_SEED", "INIT_VOLUMES", "INIT_GATE",
                  "INIT_COMMIT"}) {
                stagesOk = stagesOk && stageProbe.find(code) != std::string::npos;
            }

            // —— 判据 ③（先跑）：空库门禁 N1–N14 逐条判定，至少抓出 N1 失败且带 fix_hint ——
            QString gateEmptyQ;
            (void)icv.RunGates(&gateEmptyQ);
            const std::string gateEmpty = gateEmptyQ.toStdString();
            bool gatesAllListed = countPrefix(gateEmpty, "N") == 14;
            for (int i = 1; i <= 14; ++i) {
                gatesAllListed = gatesAllListed &&
                                 !lineOf(gateEmpty, "N" + std::to_string(i) + " ").empty();
            }
            const std::string n1Line = lineOf(gateEmpty, "N1 ");
            const bool gateReportOk =
                gatesAllListed && n1Line.find("不通过") != std::string::npos &&
                n1Line.find("修法：") != std::string::npos;

            // —— 判据 ②：一键跑骨架成功 + I15_gate.json / I16_commit.json 落盘 ——
            QString skErr;
            const bool skOk = icv.RunSkeleton(&skErr);
            std::error_code ec2;
            const bool skFiles = fs::exists(root / "work" / "init" / "I15_gate.json", ec2) &&
                                 fs::exists(root / "work" / "init" / "I16_commit.json", ec2);
            const bool skeletonOk = skOk && skFiles;

            // —— 判据 ④：忽略一条 + 人工审批 → 报告标「人工确认放行」；未审批不允许放行 ——
            QString e1;
            const bool setOk = icv.SetGateRule(QStringLiteral("N4"), true, &e1); // S4 API：ignore=true = 申请忽略
            QString e2;
            const bool approveOk = setOk && icv.ApproveGate(QStringLiteral("N4"), &e2);
            std::int64_t auditCount = 0;
            {
                shine::db::sqlite::Database d;
                if (auto r = d.Open({.path = defDb, .readOnly = true, .create = false}); !r) {
                    auditCount = -1;
                } else {
                    auditCount = queryI64(
                        d, "SELECT COUNT(*) FROM audit_logs WHERE action='gate_ignore_approved'");
                }
            }
            QString rW;
            (void)icv.RunGates(&rW);
            const bool waivedOk = rW.toStdString().find("N4 人工确认放行") != std::string::npos;
            QString e3;
            (void)icv.SetGateRule(QStringLiteral("N5"), true, &e3); // 只申请、不审批
            QString rP;
            (void)icv.RunGates(&rP);
            const std::string pendingText = rP.toStdString();
            const bool pendingBlocked =
                pendingText.find("N5 不通过") != std::string::npos &&
                pendingText.find("还没过人工审批") != std::string::npos;
            QString e4;
            (void)icv.SetGateRule(QStringLiteral("N5"), false, &e4); // 复原 enforce
            const bool configurableOk =
                setOk && approveOk && auditCount >= 1 && waivedOk && pendingBlocked;

            // —— 判据 ⑤：关库重开配置读回一致 ——
            const std::string gateProbeA = icv.GateProbe().toStdString();
            shine::app::InitChainView icvRe;
            icvRe.LoadFromRef(ref, ""); // 全新实例 = 重启读回
            const std::string gateProbeB = icvRe.GateProbe().toStdString();
            const std::string n4Line = lineOf(gateProbeB, "gate N4|");
            const bool cfgPersistOk = gateProbeA == gateProbeB &&
                                      n4Line.find("mode=ignore-approved|approved=1") != std::string::npos &&
                                      n4Line.find("verdict=waived") != std::string::npos;

            // —— 判据 ⑥：前情摘要落 work/init/ + 条目出处含「(书, 章)」+ canonical 名 ——
            QString ir;
            const bool importOk =
                icv.ImportPriorBooks({QStringLiteral("灯语回声·上")}, 2000, &ir);
            std::string ctx;
            if (auto bytes = shine::util::ReadFileBytes(root / "work" / "init" / "prior_import.json")) {
                ctx = *bytes;
            }
            const std::string importProbe1 = icv.ImportProbe().toStdString();
            const bool importPriorOk =
                importOk && !ctx.empty() && ctx.find("(灯语回声·上, ") != std::string::npos &&
                ctx.find("\"canonical\"") != std::string::npos &&
                importProbe1.find("entry canonical=Su Li|kind=person|source=(灯语回声·上, 2)") !=
                    std::string::npos &&
                grabInt(importProbe1, "import db-entries=") > 0;

            // —— 判据 ⑦：预算 500 字强制截断并明说截断量 ——
            QString ir2;
            const bool import2Ok = icv.ImportPriorBooks({QStringLiteral("灯语回声·上")}, 500, &ir2);
            const std::string r2 = ir2.toStdString();
            const int fullChars = grabInt(r2, "原文 ");
            const int keptChars = grabInt(r2, "保留 ");
            const int truncChars = grabInt(r2, "已截断 ");
            const std::string importProbe2 = icv.ImportProbe().toStdString();
            const bool budgetOk =
                import2Ok && truncChars > 0 && keptChars > 0 && keptChars <= 500 &&
                truncChars == fullChars - keptChars &&
                importProbe2.find("import truncated=" + std::to_string(truncChars)) !=
                    std::string::npos &&
                importProbe2.find("import summary-chars=" + std::to_string(keptChars)) !=
                    std::string::npos;

            // —— 判据 ⑧：前作库行数前后不变（只读保证）——
            const std::int64_t priorAfter = priorRowCount(priorDb);
            const bool noWriteBackOk = priorBefore > 0 && priorBefore == priorAfter;

            // —— 判据 ⑨：重开读回一致（阶段 / 门禁配置与判定 / 前情导入三探针逐字一致；
            //    StageProbe 的 ms= 耗时列是运行时测量、不落盘，比对前剥离）——
            const auto stripMs = [](std::string s) {
                std::size_t pos = 0;
                while ((pos = s.find("|ms=", pos)) != std::string::npos) {
                    std::size_t end = pos + 4;
                    while (end < s.size() && s[end] >= '0' && s[end] <= '9') {
                        ++end;
                    }
                    s.erase(pos, end - pos);
                }
                return s;
            };
            const std::string stA = stripMs(icv.StageProbe().toStdString());
            const std::string gtA = icv.GateProbe().toStdString();
            const std::string imA = icv.ImportProbe().toStdString();
            shine::app::InitChainView icvRe2;
            icvRe2.LoadFromRef(ref, "");
            const bool reopenOk = stripMs(icvRe2.StageProbe().toStdString()) == stA &&
                                  icvRe2.GateProbe().toStdString() == gtA &&
                                  icvRe2.ImportProbe().toStdString() == imA;

            line("stage-catalog", stagesOk,
                 "I1–I16 共 16 阶段全在流水线（INIT_CONCEPT..INIT_COMMIT，含代码/产物/状态/可断点续跑）");
            line("skeleton-run", skeletonOk,
                 std::string{"RunInitSkeleton 成功（"} + (skOk ? "骨架 ok" : skErr.toStdString()) +
                     "）+ work/init/I15_gate.json 与 work/init/I16_commit.json 落盘=" +
                     (skFiles ? "1" : "0"));
            line("gate-report", gateReportOk,
                 "空库门禁 N1–N14 逐条判定（14 行）：" + n1Line);
            line("gate-configurable", configurableOk,
                 "N4 忽略+人工审批 →「N4 人工确认放行」=" + std::string(waivedOk ? "1" : "0") +
                     "；audit_logs(gate_ignore_approved)=" + std::to_string(auditCount) +
                     "；未审批的 N5 仍拦下（不允许警告后放行）=" + std::string(pendingBlocked ? "1" : "0"));
            line("gate-config-persist", cfgPersistOk,
                 "关库重开（新实例）GateProbe 逐字一致；" + n4Line);
            line("import-prior", importPriorOk,
                 "前情摘要落 work/init/prior_import.json；条目出处含「(灯语回声·上, N)」+ canonical 名"
                 "；落库条目=" + std::to_string(grabInt(importProbe1, "import db-entries=")));
            line("import-budget", budgetOk,
                 std::string{"预算 500 字截断："} +
                     (r2.find("预算护栏") != std::string::npos
                          ? r2.substr(r2.find("预算护栏"), 60)
                          : std::string{"（报告里没有「预算护栏」行：没触发截断或未跑成"}) +
                     "…（已截断 " + std::to_string(truncChars) + " 字 = 原文 " +
                     std::to_string(fullChars) + " − 保留 " + std::to_string(keptChars) + "）");
            line("import-no-write-back", noWriteBackOk,
                 "前作库行数 " + std::to_string(priorBefore) + " → " + std::to_string(priorAfter) +
                     "（只读句柄，零写入）");
            line("reopen-readback", reopenOk, "重开（新实例）Stage/Gate/Import 三探针逐字一致");

            text += allPass ? "[P04-S4] overall: PASS" : "[P04-S4] overall: FAIL";
            text += '\n';
            (void)shine::util::WriteFileBytes(s4Out, text);
            std::fflush(nullptr);
            std::_Exit(allPass ? 0 : 1);
        });
    }
}

} // namespace shine::app::checks
