#include "ui/verify/checks/P04WorldChecks.h"
#include "ui/app/AppEnvironment.h"
#include "ui/pages/novel/NovelWorkspace.h"
#include "ui/pages/novel/WorldBoardView.h"
#include "ui/pages/shell/MainWindow.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "project/Project.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Random.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::app::checks {

void RegisterP04WorldChecks(MainWindow& window) {
    // P04-S3 判定：SHINE_P04_S3=<文件> 伏笔状态机可视化 + 知情边界「谁知道什么」
    if (const std::filesystem::path s3Out = EnvironmentPath(L"SHINE_P04_S3"); !s3Out.empty()) {
        QTimer::singleShot(600, &window, [&window, s3Out] {
            namespace fs = std::filesystem;
            std::string text;
            bool allPass = true;
            auto line = [&text, &allPass](const char* name, bool ok, const std::string& detail) {
                text += std::string(name) + (ok ? ": PASS " : ": FAIL ") + detail + '\n';
                allPass = allPass && ok;
            };
            auto queryIds = [](const fs::path& dbPath, const char* sql) {
                std::vector<qint64> out;
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = dbPath}); !r) {
                    return out;
                }
                if (auto st = db.Prepare(sql)) {
                    for (;;) {
                        auto s = st->Step();
                        if (!s || *s == shine::db::sqlite::StepResult::Done) {
                            break;
                        }
                        out.push_back(st->ColumnInt(0));
                    }
                }
                return out;
            };

            // 夹具：临时项目 + 默认书空库（db/ 随用随建）
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s3-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s3000000ff";
            pf.name = "灯语回声系列";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            (void)shine::project::SaveProjectFile(root, pf);
            std::error_code ec;
shine::util::EnsureDir(root / "db");
            {
                shine::db::sqlite::Database db;
                (void)db.Open({.path = root / "db" / "novel.db"});
                (void)shine::novelcore::NovelDb::ApplyCanonicalSchema(db);
            }
            const shine::project::ProjectRef ref = shine::project::MakeRef(pf, root);
            shine::app::WorldBoardView wb;
            wb.LoadFromRef(ref, ""); // lastNovel 空 = 默认书

            // —— 判据 1：伏笔状态机 PLANNED→PLANTED→DEVELOPING→REVEALED→RESOLVED 可视化 ——
            for (int i = 0; i < 5; ++i) {
                QString e;
                (void)wb.CreateForeshadow(QStringLiteral("灯谜 %1").arg(i + 1),
                                          QStringLiteral("灯语暗指的第 %1 处").arg(i + 1), 1, 3, 80,
                                          QStringLiteral("真相 %1").arg(i + 1), &e);
            }
            auto probeIds = [&wb] {
                std::vector<qint64> out;
                const QString p = wb.ForeshadowProbe();
                qsizetype pos = 0;
                const QString tag = QStringLiteral("伏笔#");
                while ((pos = p.indexOf(tag, pos)) >= 0) {
                    pos += tag.size();
                    qsizetype end = pos;
                    while (end < p.size() && p.at(end).isDigit()) {
                        ++end;
                    }
                    if (bool ok = false; end > pos) {
                        out.push_back(p.mid(pos, end - pos).toLongLong(&ok));
                    }
                    pos = end;
                }
                return out;
            };
            const std::vector<qint64> fids = probeIds();
            line("foreshadow-create", fids.size() == 5, "5 条新建（起点 PLANNED）");
            if (fids.size() == 5) {
                for (int i = 1; i < 5; ++i) { // 第 i 条推进 i 步 → 五态铺满
                    for (int t = 0; t < i; ++t) {
                        QString e;
                        (void)wb.AdvanceForeshadow(fids[i], &e);
                    }
                }
                const QString viz = wb.ForeshadowProbe();
                bool segs = true;
                for (int s = 1; s <= 5; ++s) {
                    segs = segs && viz.contains(QStringLiteral("第 %1/5 段").arg(s));
                }
                line("foreshadow-seg-viz", segs,
                     "五段 Timeline 覆盖 第 1/5..5/5 段（可视化与状态同源）");

                QString eAdv;
                const bool adv = wb.AdvanceForeshadow(fids[0], &eAdv); // PLANNED→PLANTED
                const QString viz2 = wb.ForeshadowProbe();
                line("foreshadow-advance",
                     adv && !viz2.contains(QStringLiteral("「灯谜 1」：PLANNED")),
                     "合法推进 PLANNED→PLANTED（推进按钮同路径）");

                QString eSkip;
                const bool skip =
                    wb.TransitionForeshadow(fids[1], QStringLiteral("RESOLVED"), &eSkip); // 跳 2 段
                line("foreshadow-illegal-skip",
                     !skip && eSkip.contains(QStringLiteral("非法跃迁")),
                     "拦下：" + eSkip.toStdString());

                QString eBack;
                const bool back =
                    wb.TransitionForeshadow(fids[4], QStringLiteral("PLANTED"), &eBack); // 回退
                line("foreshadow-illegal-back", !back && eBack.contains(QStringLiteral("不得回退")),
                     "拦下：" + eBack.toStdString());

                QString eTerm;
                const bool term = wb.AdvanceForeshadow(fids[4], &eTerm); // 已收束再推进
                line("foreshadow-terminal", !term && eTerm.contains(QStringLiteral("已收束")),
                     "拦下：" + eTerm.toStdString());

                QString eShort;
                (void)wb.CreateForeshadow(QStringLiteral("短灯"), QStringLiteral("短伏笔直达揭示"),
                                          1, 2, 50, QStringLiteral("真相短"), &eShort);
                const std::vector<qint64> fids2 = probeIds();
                const bool shortOk = fids2.size() == 6 &&
                                     wb.TransitionForeshadow(fids2[5], QStringLiteral("REVEALED"),
                                                             &eShort);
                line("foreshadow-shortcut", shortOk,
                     "短伏笔例外 PLANNED→REVEALED（`01` §2.3.3）合法通过");
            }

            // —— 判据 2：知情边界可查「谁知道什么」（角色 × 秘密 × 章时点）——
            QString eE;
            (void)wb.CreateEntity(QStringLiteral("苏黎"), QStringLiteral("person"),
                                  QStringLiteral("灯塔看守人，故事主角"), QStringLiteral("Su Li"),
                                  QStringList{QStringLiteral("小黎"), QStringLiteral("苏家丫头")},
                                  QStringLiteral("灯语回声"), 3, &eE);
            (void)wb.CreateEntity(QStringLiteral("顾行舟"), QStringLiteral("person"),
                                  QStringLiteral("来客，知道灯语的来历"),
                                  QStringLiteral("Gu Xingzhou"), {}, QStringLiteral("灯语回声"), 1,
                                  &eE);
            QString eS;
            (void)wb.CreateSecret(QStringLiteral("灯语的来历"), QStringLiteral("灯语源自守灯人契约"),
                                  10, QStringLiteral("揭示于灯塔夜谈"), 0,
                                  QStringLiteral("character"), &eS);
            (void)wb.CreateSecret(QStringLiteral("铜钥匙的用途"), QStringLiteral("开灯塔地下室"), 0,
                                  QString{}, 0, QStringLiteral("character"), &eS);
            (void)wb.CreateSecret(QStringLiteral("顾行舟的真名"), QStringLiteral("顾家养子"), 8,
                                  QString{}, 0, QStringLiteral("character"), &eS);
            const std::vector<qint64> persons =
                queryIds(root / "db" / "novel.db",
                         "SELECT id FROM entities WHERE kind='person' ORDER BY id");
            const std::vector<qint64> secrets =
                queryIds(root / "db" / "novel.db", "SELECT id FROM secrets ORDER BY id");
            const bool seedOk = persons.size() == 2 && secrets.size() == 3;
            line("knowledge-seed", seedOk, "2 角色 x 3 秘密 id 解析");
            if (seedOk) {
                QString e1, e2, e3;
                (void)wb.SetKnows(persons[0], secrets[0], true, 3, &e1);  // 苏黎 第 3 章起知
                (void)wb.SetKnows(persons[0], secrets[1], true, 0, &e2);  // 苏黎 书前即知
                (void)wb.SetKnows(persons[1], secrets[0], false, 0, &e3); // 顾行舟 显式不知
                QString p2, p3;
                (void)wb.WhoKnowsWhat(2, &p2);
                (void)wb.WhoKnowsWhat(3, &p3);
                line("knowledge-ch2",
                     p2.contains(QStringLiteral("第 3 章起才知")) &&
                         p2.contains(QStringLiteral("已显式标记不知")),
                     "第 2 章时点：苏黎未知（第 3 章起才知）+ 顾行舟显式不知");
                line("knowledge-ch3",
                     p3.contains(QStringLiteral("知道（第 3 章起）")) &&
                         p3.contains(QStringLiteral("知道（书前即知）")),
                     "第 3 章时点：知道（第 3 章起）+ 知道（书前即知）");
                wb.CloseDb();
                wb.LoadFromRef(ref, ""); // 关库重开（又一个新 Database 实例）
                QString p3b;
                (void)wb.WhoKnowsWhat(3, &p3b);
                line("knowledge-reopen", !p3b.isEmpty() && p3b == p3,
                     "关库重开「谁知道什么」逐行一致");
            }

            text += allPass ? "[P04-S3] overall: PASS\n" : "[P04-S3] overall: FAIL\n";
            (void)shine::util::WriteFileBytes(s3Out, text);
            std::_Exit(allPass ? 0 : 1);
        });
    }
    // P04-S1 判定：SHINE_P04_S1=<文件> 千章压测（书→卷→章 载入 / 选章即时切换 / 滚动）
    if (const std::filesystem::path s1Out = EnvironmentPath(L"SHINE_P04_S1"); !s1Out.empty()) {
        QTimer::singleShot(600, &window, [&window, s1Out] {
            namespace fs = std::filesystem;
            std::string text;
            bool allPass = true;
            auto line = [&text](const std::string& s) { text += s + '\n'; };

            // 千章夹具：默认书 10 卷 × 100 章 + 系列书《灯语回声·上》2 卷 × 5 章（共 1010 章）
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s1-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s1000000ff";
            pf.name = "灯语回声系列";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            (void)shine::project::SaveProjectFile(root, pf);
            auto seedBook = [&allPass, &line](const fs::path& dbPath, int volumes, int perVolume) {
                std::error_code ec;
shine::util::EnsureDir(dbPath.parent_path());
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = dbPath}); !r) {
                    allPass = false;
                    line("seed-open: FAIL " + r.error().message);
                    return;
                }
                (void)shine::novelcore::NovelDb::ApplyCanonicalSchema(db);
                shine::novelcore::NovelGraph g(db);
                for (int v = 1; v <= volumes; ++v) {
                    shine::novelcore::VolumeRow vr;
                    vr.title = "第 " + std::to_string(v) + " 卷";
                    vr.ord = v;
                    auto vid = g.UpsertVolume(vr);
                    if (!vid) {
                        continue;
                    }
                    for (int c = 1; c <= perVolume; ++c) {
                        const int ord = (v - 1) * perVolume + c;
                        shine::novelcore::ChapterRow row;
                        row.volume_id = *vid;
                        row.ord = ord;
                        row.title = "第 " + std::to_string(ord) + " 章　灯下回声";
                        row.status = (ord % 5 == 0) ? "review" : ((ord % 7 == 0) ? "done" : "draft");
                        row.summary = "第 " + std::to_string(ord) + " 章摘要：灯语引路，回声指归途。";
                        row.body = (ord % 11 == 0)
                                       ? std::string{}
                                       : ("正文 " + std::to_string(ord) +
                                          "：雪原上只剩下风声，一盏灯把迷路的人带回家。");
                        row.words = static_cast<int>(row.body.size());
                        (void)g.UpsertChapter(row);
                    }
                }
            };
            seedBook(root / "db" / "novel.db", 10, 100);
            seedBook(root / "books" / "灯语回声·上" / "db" / "novel.db", 2, 5);

            (void)window.OpenProjectPath(root); // EnterProject → LoadNovelPages
            window.SwitchWorkspace(1);          // 小说工作区（书→卷→章）
            shine::app::NovelWorkspace* nw = window.NovelPage();
            if (nw == nullptr) {
                line("novel-page: FAIL 找不到小说工作区页");
                allPass = false;
            } else {
                line("load-ms=" + std::to_string(nw->LastLoadMs()) +
                     (nw->LastLoadMs() < 2000 ? " PASS(<2000ms, 1010 章)" : " FAIL(>=2000ms)"));
                allPass = allPass && nw->LastLoadMs() < 2000;
                const int tc = nw->TreeChapterCount();
                line("tree-chapters=" + std::to_string(tc) +
                     (tc == 1010 ? " PASS(10卷x100 + 2卷x5)" : " FAIL"));
                allPass = allPass && tc == 1010;
                const int cc = nw->CardCount();
                line("card-count=" + std::to_string(cc) + (cc == 1010 ? " PASS" : " FAIL"));
                allPass = allPass && cc == 1010;

                // 选章即时切换：100 次往返计时 + 树/卡双向同步核对
                QElapsedTimer sel;
                sel.start();
                for (int i = 0; i < 100; ++i) {
                    nw->SelectChapterAt((i * 37) % 1010);
                }
                const qint64 selMs = sel.elapsed();
                line("select-100-total-ms=" + std::to_string(selMs) +
                     (selMs < 500 ? " PASS(100 次 <500ms)" : " FAIL"));
                allPass = allPass && selMs < 500;
                (void)nw->SelectChapterAt(500);
                const std::string probe = nw->SelectionProbe().toStdString();
                const bool syncOk = probe.find("ord=501") != std::string::npos;
                line(std::string("selection-sync: ") + (syncOk ? "PASS " : "FAIL ") + probe);
                allPass = allPass && syncOk;

                // 千章滚动：热身 50 步（吸收首帧布局/Toast 动画）后 200 步逐帧计时
                QListWidget* cards = nw->CardList();
                QScrollBar* bar = cards->verticalScrollBar();
                const int maxV = bar->maximum();
                for (int step = 1; step <= 50; ++step) { // 热身：不计时
                    bar->setValue(maxV * step / 50);
                    cards->repaint();
                    QApplication::processEvents();
                }
                bar->setValue(0);
                qint64 worst = 0;
                qint64 total = 0;
                int worstStep = 0;
                for (int step = 1; step <= 200; ++step) {
                    QElapsedTimer f;
                    f.start();
                    bar->setValue(maxV * step / 200);
                    cards->repaint();
                    QApplication::processEvents();
                    const qint64 d = f.elapsed();
                    if (d > worst) {
                        worst = d;
                        worstStep = step;
                    }
                    total += d;
                }
                const qint64 avg = total / 200;
                line("scroll-200-avg-ms=" + std::to_string(avg) + (avg < 16 ? " PASS(<16ms)" : " FAIL"));
                line("scroll-200-max-ms=" + std::to_string(worst) + "@" + std::to_string(worstStep) +
                     (worst < 100 ? " PASS(<100ms)" : " FAIL"));
                allPass = allPass && avg < 16 && worst < 100;
            }
            line(allPass ? "[P04-S1] overall: PASS" : "[P04-S1] overall: FAIL");
            (void)shine::util::WriteFileBytes(s1Out, text);
            std::_Exit(allPass ? 0 : 1);
        });
    }
    // P04-S2 判定：SHINE_P04_S2=<文件> 设定台「新建实体 → 落库 → 关库重开读回一致」
    //              + 动态字段三步校验（别名解析 / 类型合法 / 重复键·键数上限）逐条拦下
    if (const std::filesystem::path s2Out = EnvironmentPath(L"SHINE_P04_S2"); !s2Out.empty()) {
        QTimer::singleShot(600, &window, [s2Out] {
            namespace fs = std::filesystem;
            std::string text;
            bool allPass = true;
            const auto line = [&text, &allPass](std::string_view name, bool pass,
                                               std::string_view detail) {
                text += std::string{name} + (pass ? ": PASS " : ": FAIL ") + std::string{detail} +
                        '\n';
                allPass = allPass && pass;
            };

            // —— 夹具：临时项目目录 + 空库（默认书 + 系列书《灯语回声·上》，一部一库）——
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p04s2-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p04s2000000ff";
            pf.name = "灯语回声系列";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            const bool prjOk = bool(shine::project::SaveProjectFile(root, pf));
            line("fixture-project", prjOk,
                 prjOk ? "临时项目 project.json 已落盘" : "SaveProjectFile 失败");
            const fs::path defDb = root / "db" / "novel.db";
            const fs::path seriesDb = root / "books" / "灯语回声·上" / "db" / "novel.db";
            const auto seedBook = [&line](const fs::path& dbPath) -> bool {
                std::error_code ec;
shine::util::EnsureDir(dbPath.parent_path());
                shine::db::sqlite::Database db;
                if (auto r = db.Open({.path = dbPath}); !r) {
                    line("fixture-schema", false, r.error().message);
                    return false;
                }
                if (auto r = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !r) {
                    line("fixture-schema", false, r.error().message);
                    return false;
                }
                return true;
            };
            const bool seeded = seedBook(defDb) && seedBook(seriesDb);
            line("fixture-schema", seeded,
                 seeded ? "默认书 + 系列书空库就绪（ApplyCanonicalSchema）" : "建库失败");

            const shine::project::ProjectRef ref = shine::project::MakeRef(pf, root);

            // —— 实体：新建 → 落库（表单与探针同路径 WorldBoardView::CreateEntity）——
            shine::app::WorldBoardView wb;
            wb.LoadFromRef(ref, ""); // lastNovel 空 = 默认书
            const QString bookProbe = wb.BookProbe();
            line("book-default", bookProbe.startsWith(QStringLiteral("book=灯语回声系列")),
                 bookProbe.toStdString());
            QString err;
            const QStringList aliases{QStringLiteral("小黎"), QStringLiteral("苏家丫头")};
            const bool created =
                wb.CreateEntity(QStringLiteral("苏黎"), QStringLiteral("person"),
                                QStringLiteral("灯塔看守人，故事主角"), QStringLiteral("Su Li"),
                                aliases, QStringLiteral("灯语回声"), 3, &err);
            line("create-entity", created,
                 created ? "CreateEntity 落库成功（canonical=Su Li / 别名 2 个 / 出处（灯语回声, 3））"
                         : err.toStdString());

            // —— 书作用域（一部一库）：lastNovel=系列书 → 只落 books/灯语回声·上 ——
            shine::app::WorldBoardView wbBook;
            wbBook.LoadFromRef(ref, "灯语回声·上");
            QString errBook;
            const bool createdBook = wbBook.CreateEntity(
                QStringLiteral("顾行舟"), QStringLiteral("person"),
                QStringLiteral("巡夜人，重要配角"), QString{}, QStringList{},
                QStringLiteral("灯语回声·上"), 1, &errBook);
            line("create-entity-series-book", createdBook,
                 createdBook ? "系列书《灯语回声·上》落库成功" : errBook.toStdString());
            wbBook.CloseDb();

            // —— 动态字段：成功登记 1 条（人工登记 → CANON）——
            QString errF;
            const bool defOk = wb.RegisterField(
                QStringLiteral("true_identity"), QStringLiteral("entity"), QString{},
                QStringLiteral("真实身份"), QStringLiteral("text"), QString{},
                QStringLiteral("人物真身/隐藏身份"), &errF);
            line("field-register", defOk,
                 defOk ? "true_identity 登记成功（text / entity / CANON）" : errF.toStdString());

            // 三步校验 ① 别名解析：别名指向未登记的规范键 → 别名无法解析
            {
                shine::db::sqlite::Database side;
                (void)side.Open({.path = defDb});
                shine::novelcore::NovelFields sideFields(side);
                (void)sideFields.UpsertFieldAlias("old_true_name", "ghost_canonical",
                                                  "S2 探针夹具");
            }
            QString errA;
            const bool aliasBlocked =
                !wb.RegisterField(QStringLiteral("old_true_name"), QStringLiteral("entity"),
                                  QString{}, QStringLiteral("旧真名"), QStringLiteral("text"),
                                  QString{}, QString{}, &errA) &&
                errA.contains(QStringLiteral("别名无法解析"));
            line("gate1-alias-unresolved", aliasBlocked, errA.toStdString());

            // 三步校验 ② 类型合法：value_type 不在 text|number|json|enum 内
            QString errT;
            const bool typeBlocked =
                !wb.RegisterField(QStringLiteral("mood_color"), QStringLiteral("entity"), QString{},
                                  QStringLiteral("情绪色"), QStringLiteral("boolean"), QString{},
                                  QString{}, &errT) &&
                errT.contains(QStringLiteral("类型非法"));
            line("gate2-bad-value-type", typeBlocked, errT.toStdString());

            // 三步校验 ③ 重复键 / 键数上限（`08` §2.3）
            QString errD;
            const bool dupBlocked =
                !wb.RegisterField(QStringLiteral("True-Identity"), QStringLiteral("entity"),
                                  QString{}, QStringLiteral("重复登记"), QStringLiteral("text"),
                                  QString{}, QString{}, &errD) &&
                errD.contains(QStringLiteral("重复键"));
            line("gate3a-duplicate-key", dupBlocked, errD.toStdString());
            {
                // 铺到键数上限（夹具直写 field_defs）；随后走 UI 路径应被键数上限拦下
                shine::db::sqlite::Database side;
                (void)side.Open({.path = defDb});
                shine::novelcore::NovelFields sideFields(side);
                for (int i = 0; i < shine::novelcore::NovelFields::kMaxFieldDefs + 8; ++i) {
                    auto defs = sideFields.ListFieldDefs();
                    if (!defs || static_cast<int>(defs->size()) >=
                                    shine::novelcore::NovelFields::kMaxFieldDefs) {
                        break;
                    }
                    shine::novelcore::FieldDefRow pad;
                    pad.scope = "entity";
                    pad.field_key = "probe_pad_" + std::to_string(i);
                    pad.title = pad.field_key;
                    pad.value_type = "text";
                    pad.created_by = "probe";
                    pad.status = "PROPOSED";
                    (void)sideFields.UpsertFieldDef(pad);
                }
            }
            QString errC;
            const bool capBlocked =
                !wb.RegisterField(QStringLiteral("late_key"), QStringLiteral("entity"), QString{},
                                  QStringLiteral("迟到键"), QStringLiteral("text"), QString{},
                                  QString{}, &errC) &&
                errC.contains(QStringLiteral("键数上限"));
            line("gate3b-key-cap", capBlocked, errC.toStdString());

            // —— entity_fields 写入（Drawer「已登记字段」保存同路径）——
            qint64 suLiId = 0;
            {
                shine::db::sqlite::Database side;
                (void)side.Open({.path = defDb});
                shine::novelcore::NovelGraph g(side);
                if (auto list = g.ListEntities("person", "苏黎", 10); list && !list->empty()) {
                    suLiId = list->front().id;
                }
            }
            QString errV;
            const bool valOk =
                suLiId > 0 &&
                wb.SaveEntityField(suLiId, QStringLiteral("true_identity"),
                                   QStringLiteral("灯语回声的引路人"), QString{},
                                   QStringLiteral("mask"), QString{}, &errV);
            line("entity-field-write", valOk,
                 valOk ? "entity_fields：true_identity=灯语回声的引路人（layer=mask）"
                       : (suLiId > 0 ? errV.toStdString() : std::string{"找不到实体 id"}));

            // —— 关库重开（新 Database 实例）→ 读回逐字段比对 ——
            wb.CloseDb();
            shine::db::sqlite::Database db2;
            const bool reopened = bool(db2.Open({.path = defDb}));
            line("reopen-db", reopened,
                 reopened ? "关库后以新 Database 实例重开默认书 novel.db" : "重开失败");
            if (reopened) {
                shine::novelcore::NovelGraph g2(db2);
                auto list = g2.ListEntities("person", "苏黎", 10);
                const bool found = list && !list->empty();
                line("readback-entity", found,
                     found ? "重启读回：实体可检索（ListEntities）" : "读不回实体");
                if (found) {
                    const shine::novelcore::EntityRow& e = list->front();
                    line("readback-name", e.name == "苏黎", e.name);
                    line("readback-kind", e.kind == "person", e.kind);
                    line("readback-summary", e.summary == "灯塔看守人，故事主角", e.summary);
                    line("readback-status", e.status == "active", e.status);
                    // meta_json 逐字段（多小说与分卷-架构决策 §7 约定键）
                    shine::util::json::OwnedDoc doc = shine::util::json::ParseDoc(e.meta_json);
                    yyjson_val* m = doc.root();
                    const std::string canon = shine::util::json::GetStrCopy(m, "canonical");
                    line("readback-meta-canonical", canon == "Su Li", canon);
                    const std::string srcBook = shine::util::json::GetStrCopy(m, "source_book");
                    line("readback-meta-source-book", srcBook == "灯语回声", srcBook);
                    const auto srcCh = shine::util::json::GetI64(m, "source_ch");
                    line("readback-meta-source-ch", srcCh == 3, std::to_string(srcCh));
                    std::vector<std::string> gotAliases;
                    if (yyjson_val* arr = shine::util::json::GetArr(m, "aliases"); arr != nullptr) {
                        const std::size_t n = yyjson_arr_size(arr);
                        for (std::size_t i = 0; i < n; ++i) {
                            yyjson_val* v = yyjson_arr_get(arr, i);
                            if (v != nullptr && yyjson_is_str(v)) {
                                gotAliases.emplace_back(yyjson_get_str(v));
                            }
                        }
                    }
                    const bool aliasOk = gotAliases.size() == 2 && gotAliases[0] == "小黎" &&
                                         gotAliases[1] == "苏家丫头";
                    std::string aliasView;
                    for (const std::string& a : gotAliases) {
                        aliasView += a + "/";
                    }
                    line("readback-meta-aliases", aliasOk, aliasView);
                }
                // entity_fields 写入后重开读回一致
                shine::novelcore::NovelFields f2(db2);
                QString gotVal;
                QString gotLayer;
                if (suLiId > 0) {
                    if (auto vals = f2.ListEntityFields(suLiId); vals) {
                        for (const shine::novelcore::EntityFieldRow& v : *vals) {
                            if (v.field_key == "true_identity") {
                                gotVal = QString::fromStdString(v.value_text);
                                gotLayer = QString::fromStdString(v.layer);
                            }
                        }
                    }
                }
                const bool valBack = gotVal == QStringLiteral("灯语回声的引路人") &&
                                     gotLayer == QStringLiteral("mask");
                line("readback-entity-field", valBack,
                     (gotVal + "|" + gotLayer).toStdString());
                // 登记读回（key / 类型 / 状态）
                QString defView;
                bool defBack = false;
                if (auto defs = f2.ListFieldDefs(); defs) {
                    for (const shine::novelcore::FieldDefRow& d : *defs) {
                        if (d.field_key == "true_identity") {
                            defView = QString::fromStdString(d.value_type + "|" + d.status);
                            defBack = d.value_type == "text" && d.status == "CANON";
                        }
                    }
                }
                line("readback-field-def", defBack, defView.toStdString());
                // 一部一库书作用域：顾行舟 在系列书、不在默认书
                shine::db::sqlite::Database dbS;
                const bool seriesOpened = bool(dbS.Open({.path = seriesDb}));
                int inSeries = 0;
                if (seriesOpened) {
                    shine::novelcore::NovelGraph gs(dbS);
                    if (auto l = gs.ListEntities("person", "顾行舟", 10); l) {
                        inSeries = static_cast<int>(l->size());
                    }
                }
                int inDefault = 0;
                if (auto l = g2.ListEntities("person", "顾行舟", 10); l) {
                    inDefault = static_cast<int>(l->size());
                }
                line("readback-book-scope", seriesOpened && inSeries == 1 && inDefault == 0,
                     "系列书=" + std::to_string(inSeries) + " 默认书=" + std::to_string(inDefault));
            }

            // —— UI 刷新路径（RefreshEntities / kind 计数徽标数据源）——
            wb.LoadFromRef(ref, ""); // 重开：又一个新 Database 实例
            const bool refreshed = wb.RefreshEntities();
            const int personCount = wb.EntityCount(QStringLiteral("person"));
            line("ui-refresh-entities", refreshed && personCount == 1,
                 "RefreshEntities=" + std::string(refreshed ? "1" : "0") +
                     " 默认书 person 计数=" + std::to_string(personCount));

            text += allPass ? "[P04-S2] overall: PASS" : "[P04-S2] overall: FAIL";
            text += '\n';
            (void)shine::util::WriteFileBytes(s2Out, text);
            std::fflush(nullptr);
            std::_Exit(allPass ? 0 : 1);
        });
    }}

} // namespace shine::app::checks
