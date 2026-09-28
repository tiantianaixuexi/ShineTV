#include "pages/checks/P06Checks.h"

#include "app/AppEnvironment.h"
#include "pages/shell/MainWindow.h"
#include "pages/storyboard/StoryboardWorkspace.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "util/File.h"
#include "util/Random.h"

#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>

#include <cstdio>
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>
#include <string>

namespace shine::app::checks {

void RegisterP06Checks(MainWindow& window) {
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S1"); !out.empty()) {
        QTimer::singleShot(600, &window, [&window, out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s1-" + shine::util::RandomHex(8));
            fs::create_directories(root / "db");
            std::int64_t committed_scene = 0;
            std::int64_t draft_scene = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto done = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                               .title = "已提交章", .status = "done"})
                                       : std::unexpected(volume.error());
                    auto draft = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 2,
                                                                .title = "草稿章", .status = "draft"})
                                        : std::unexpected(volume.error());
                    if (!done || !draft) {
                        line("fixture-chapter", false, "章节创建失败");
                    } else {
                        auto scene_a = graph.UpsertScene({.chapter_id = *done, .ord = 1,
                                                          .title = "已提交场景 A", .goal = "目标A"});
                        auto scene_b = graph.UpsertScene({.chapter_id = *done, .ord = 2,
                                                          .title = "已提交场景 B", .goal = "目标B"});
                        auto scene_draft = graph.UpsertScene({.chapter_id = *draft, .ord = 1,
                                                               .title = "草稿场景", .goal = "不应出现"});
                        if (!scene_a || !scene_b || !scene_draft) {
                            line("fixture-scene", false, "场景创建失败");
                        } else {
                            committed_scene = *scene_a;
                            draft_scene = *scene_draft;
                        }
                    }
                }
            }

            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = committed_scene > 0 && draft_scene > 0 &&
                                workspace.OpenBook(root / "db" / "novel.db", root, &error);
            const std::string before = workspace.SceneProbe().toStdString();
            line("committed-only", opened && before.find("committed_chapters=1") != std::string::npos &&
                                      before.find("scenes=2") != std::string::npos,
                 "只列已提交章节 Scene，草稿场景被排除");
            const bool selected = workspace.SelectScene(committed_scene);
            const std::string after = workspace.SceneProbe().toStdString();
            line("scene-select", selected && after.find("selected=" + std::to_string(committed_scene)) !=
                                         std::string::npos,
                 "场景选择可定位并显示详情");
            window.SwitchWorkspace(3);
            line("workspace-navigation", window.StoryboardPage() != nullptr,
                 "活动栏「分镜」创建真实 StoryboardWorkspace 文档页");

            content += "SCENE-PROBE(before):\n" + before + "\n";
            content += "SCENE-PROBE(after):\n" + after + "\n";
            content += fails == 0 ? "[P06-S1] overall: PASS" : "[P06-S1] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S2"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s2-" + shine::util::RandomHex(8));
            fs::create_directories(root / "db");
            std::int64_t scene_id = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "阶段章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else {
                        auto scene = graph.UpsertScene({.chapter_id = *chapter, .ord = 1,
                                                          .title = "阶段场景", .goal = "拆成两镜"});
                        if (!scene) {
                            line("fixture-scene", false, scene.error().message.c_str());
                        } else {
                            scene_id = *scene;
                        }
                    }
                }
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            int calls = 0;
            workspace.SetLlm([&calls](shine::agent::LlmRole, std::string_view instructions,
                                      std::string_view) -> std::expected<std::string, shine::agent::AgentError> {
                ++calls;
                if (std::string{instructions}.find("场分析") != std::string::npos) {
                    return std::string{R"({"scenes":[{"scene_ord":1,"goal":"拆场","conflict":"追兵逼近","emotion":"紧张","information":"钥匙在守卫身上","environment":"码头","character_goals":[],"important_props":["钥匙"],"shots":[{"ord":1,"duration":3.0,"beat":"发现追兵"},{"ord":2,"duration":2.0,"beat":"躲入货箱"}]}]})"};
                }
                return std::string{R"({"items":[{"scene_ord":1,"ord":1}]})"};
            });
            const bool started = opened && workspace.RunStagePipeline();
            const auto wait_stage = [&workspace] {
                QEventLoop loop;
                QElapsedTimer elapsed;
                elapsed.start();
                QTimer poll;
                bool timeout = false;
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                    if (workspace.StageProbe().contains(QStringLiteral("running=0")) ||
                        elapsed.elapsed() > 15000) {
                        timeout = elapsed.elapsed() > 15000;
                        poll.stop();
                        loop.quit();
                    }
                });
                poll.start(20);
                loop.exec(QEventLoop::ExcludeUserInputEvents);
                return !timeout;
            };
            const bool first_done = started && wait_stage();
            const std::string first_probe = workspace.StageProbe().toStdString();
            const std::array<const char*, 7> files{
                "v01_scene_breakdown.json", "v02_director_intent.json", "v03_performance.json",
                "v04_spatial.json", "v05_camera.json", "v06_timeline.json", "v07_audio.json"};
            bool artifacts_ok = first_done;
            for (const char* file : files) {
                artifacts_ok = artifacts_ok &&
                                fs::is_regular_file(root / "work" / "ch001" / file);
            }
            line("v1-v8-pipeline", first_done && first_probe.find("states=8") != std::string::npos &&
                                        first_probe.find("V8=done") != std::string::npos,
                 "V1–V8 全部进入完成态，V8 连续性阶段有结果");
            line("stage-artifacts", artifacts_ok,
                 "V1–V7 每阶段 JSON 产物落盘");
            const bool second_started = opened && workspace.RunStagePipeline();
            const bool second_done = second_started && wait_stage();
            line("stage-rerun", second_done && calls == 7,
                 "二次运行按哈希复用阶段产物，不重复调用 LLM");

            content += "STAGE-PROBE(first):\n" + first_probe + "\n";
            content += "STAGE-PROBE(second):\n" + workspace.StageProbe().toStdString() + "\n";
            content += fails == 0 ? "[P06-S2] overall: PASS" : "[P06-S2] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S3"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s3-" + shine::util::RandomHex(8));
            fs::create_directories(root / "db");
            std::int64_t scene_id = 0;
            std::int64_t chapter_id = 0;
            std::vector<shine::novelcore::ShotRow> shots;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "故事板章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else {
                        chapter_id = *chapter;
                        auto scene = graph.UpsertScene({.chapter_id = chapter_id, .ord = 1,
                                                          .title = "故事板场景", .goal = "动作"});
                        if (!scene) {
                            line("fixture-scene", false, scene.error().message.c_str());
                        } else {
                            scene_id = *scene;
                            shine::novelcore::NovelVisual visual(db);
                            for (int ord = 1; ord <= 3; ++ord) {
                                shine::novelcore::ShotRow shot;
                                shot.scene_id = scene_id;
                                shot.ord = ord;
                                shot.action = "动作 " + std::to_string(ord);
                                shot.duration_note = std::to_string(ord) + ".0s";
                                auto id = visual.UpsertShot(shot);
                                if (!id) {
                                    line("fixture-shot", false, id.error().message.c_str());
                                } else {
                                    shot.id = *id;
                                    shots.push_back(shot);
                                }
                            }
                        }
                    }
                }
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            const std::string before = workspace.TimelineProbe().toStdString();
            line("timeline-items", opened && before.find("shots=3") != std::string::npos,
                 "Scene → Sequence → Shot 故事板显示 3 个镜头占位");
            std::vector<shine::novelcore::RowId> order;
            for (const auto& shot : shots) order.push_back(shot.id);
            std::reverse(order.begin(), order.end());
            const bool started = opened && workspace.ReorderTimeline(order);
            QEventLoop loop;
            QElapsedTimer elapsed;
            elapsed.start();
            QTimer poll;
            bool timeout = false;
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                if (workspace.TimelineProbe().contains(QStringLiteral("busy=0")) ||
                    elapsed.elapsed() > 10000) {
                    timeout = elapsed.elapsed() > 10000;
                    poll.stop();
                    loop.quit();
                }
            });
            poll.start(20);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            bool persisted = false;
            {
                shine::db::sqlite::Database verify;
                if (auto db = verify.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false});
                    db) {
                    shine::novelcore::NovelVisual visual(verify);
                    auto listed = visual.ListShotsByChapter(chapter_id);
                    persisted = listed && listed->size() == 3 && (*listed)[0].id == order[0] &&
                                 (*listed)[1].id == order[1] && (*listed)[2].id == order[2];
                }
            }
            line("drag-reorder", started && !timeout && persisted,
                 "拖拽重排写回 shots.ord，镜头表读回顺序一致");
            content += "TIMELINE-PROBE:\n" + workspace.TimelineProbe().toStdString() + "\n";
            content += fails == 0 ? "[P06-S3] overall: PASS" : "[P06-S3] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S4"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s4-" + shine::util::RandomHex(8));
            fs::create_directories(root / "db");
            std::int64_t scene_id = 0;
            std::int64_t chapter_id = 0;
            std::vector<shine::novelcore::ShotRow> shots;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "镜头表章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else {
                        chapter_id = *chapter;
                        auto scene = graph.UpsertScene({.chapter_id = chapter_id, .ord = 1,
                                                          .title = "镜头表场景", .goal = "编辑"});
                        if (!scene) {
                            line("fixture-scene", false, scene.error().message.c_str());
                        } else {
                            scene_id = *scene;
                            shine::novelcore::NovelVisual visual(db);
                            for (int ord = 1; ord <= 3; ++ord) {
                                shine::novelcore::ShotRow shot;
                                shot.scene_id = scene_id;
                                shot.ord = ord;
                                shot.action = "原动作";
                                shot.mood = "平静";
                                shot.duration_note = "1.0s";
                                auto id = visual.UpsertShot(shot);
                                if (!id) {
                                    line("fixture-shot", false, id.error().message.c_str());
                                } else {
                                    shot.id = *id;
                                    shots.push_back(shot);
                                }
                            }
                        }
                    }
                }
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            const std::string table_before = workspace.ShotTableProbe().toStdString();
            line("virtual-table", opened && table_before.find("rows=3") != std::string::npos &&
                                      table_before.find("virtual=1") != std::string::npos,
                 "镜头表虚拟化显示 3 行，列按 id 管理宽度");
            const bool edited = opened && !shots.empty() &&
                                workspace.UpdateShot(shots[0].id, "新动作", "紧张", "2.0s");
            const auto wait_table = [&workspace] {
                QEventLoop loop;
                QElapsedTimer elapsed;
                elapsed.start();
                QTimer poll;
                bool timeout = false;
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                    if (workspace.TimelineProbe().contains(QStringLiteral("busy=0")) ||
                        elapsed.elapsed() > 10000) {
                        timeout = elapsed.elapsed() > 10000;
                        poll.stop();
                        loop.quit();
                    }
                });
                poll.start(20);
                loop.exec(QEventLoop::ExcludeUserInputEvents);
                return !timeout;
            };
            const bool edit_done = edited && wait_table();
            std::vector<shine::novelcore::RowId> ids;
            for (const auto& shot : shots) ids.push_back(shot.id);
            const bool batch = opened && workspace.ApplyMoodBatch(ids, QStringLiteral("决意"));
            const bool batch_done = batch && wait_table();
            bool persisted = false;
            {
                shine::db::sqlite::Database verify;
                if (auto db = verify.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false});
                    db.has_value() && !shots.empty()) {
                    shine::novelcore::NovelVisual visual(verify);
                    auto edited_shot = visual.GetShot(shots[0].id);
                    persisted = edited_shot && edited_shot->action == "新动作" &&
                                edited_shot->mood == "决意" && edited_shot->duration_note == "2.0s";
                }
            }
            line("row-edit", edit_done && persisted, "行内编辑动作/情绪/时长写回镜头");
            line("batch-command", batch_done && persisted, "批量情绪命令作用于多镜头并可读回");
            content += "SHOT-TABLE-PROBE:\n" + workspace.ShotTableProbe().toStdString() + "\n";
            content += fails == 0 ? "[P06-S4] overall: PASS" : "[P06-S4] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S5"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s5-" + shine::util::RandomHex(8));
            fs::create_directories(root / "db");
            std::int64_t scene_id = 0;
            std::int64_t chapter_id = 0;
            std::int64_t shot_id = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "详情章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else {
                        chapter_id = *chapter;
                        auto scene = graph.UpsertScene({.chapter_id = chapter_id, .ord = 1,
                                                          .title = "详情场景", .goal = "表演"});
                        if (!scene) {
                            line("fixture-scene", false, scene.error().message.c_str());
                        } else {
                            scene_id = *scene;
                            shine::novelcore::NovelVisual visual(db);
                            shine::novelcore::ShotRow shot;
                            shot.scene_id = scene_id;
                            shot.ord = 1;
                            shot.action = "走向镜头";
                            shot.expression = "警觉";
                            shot.camera_id = 3;
                            shot.composition_id = 4;
                            shot.lighting_id = 5;
                            shot.start_state_json = R"({"characters":[{"entity_id":1,"pose":"stand"}]})";
                            shot.end_state_json = R"({"environment":"night"})";
                            shot.reference_json = R"({"@char":"苏黎","@image":"灯塔_夜"})";
                            shot.timeline_json = R"({"duration_s":3.0,"beats":[{"begin_s":0.0,"end_s":1.5}]})";
                            auto id = visual.UpsertShot(shot);
                            if (!id) line("fixture-shot", false, id.error().message.c_str());
                            else shot_id = *id;
                        }
                    }
                }
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            const std::string detail = workspace.ShotDetailProbe().toStdString();
            line("shot-detail", opened && detail.find("performance=12") != std::string::npos &&
                                  detail.find("spatial=1") != std::string::npos &&
                                  detail.find("camera=1") != std::string::npos &&
                                  detail.find("references=1") != std::string::npos,
                 "单镜头详情显示表演 12 项、空间、机位与引用");
            const std::string updatedTimeline = R"({"duration_s":4.0,"beats":[{"begin_s":0.0,"end_s":4.0}]})";
            const bool saved = opened && workspace.SaveSelectedTimeline(updatedTimeline);
            QEventLoop loop;
            QElapsedTimer elapsed;
            elapsed.start();
            QTimer poll;
            bool timeout = false;
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                if (workspace.TimelineProbe().contains(QStringLiteral("busy=0")) ||
                    elapsed.elapsed() > 10000) {
                    timeout = elapsed.elapsed() > 10000;
                    poll.stop();
                    loop.quit();
                }
            });
            poll.start(20);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            bool persisted = false;
            {
                shine::db::sqlite::Database verify;
                if (auto db = verify.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false});
                    db.has_value() && shot_id > 0) {
                    shine::novelcore::NovelVisual visual(verify);
                    auto shot = visual.GetShot(shot_id);
                    persisted = shot && shot->timeline_json == updatedTimeline;
                }
            }
            line("beat-edit", saved && !timeout && persisted,
                 "Beat[] 可编辑并写回 timeline_json");
            content += "SHOT-DETAIL-PROBE:\n" + detail + "\n";
            content += fails == 0 ? "[P06-S5] overall: PASS" : "[P06-S5] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S6"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s6-" + shine::util::RandomHex(8));
            std::error_code ec;
            fs::create_directories(root / "db", ec);
            std::int64_t scene_id = 0;
            std::int64_t chapter_id = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "连续性章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else {
                        chapter_id = *chapter;
                        auto scene = graph.UpsertScene({.chapter_id = chapter_id, .ord = 1,
                                                          .title = "连续性场景", .goal = "角色离场"});
                        if (!scene) {
                            line("fixture-scene", false, scene.error().message.c_str());
                        } else {
                            scene_id = *scene;
                            shine::novelcore::NovelVisual visual(db);
                            shine::novelcore::ShotRow first;
                            first.scene_id = scene_id;
                            first.ord = 1;
                            first.end_state_json = R"({"characters":[{"entity_id":1}]})";
                            first.timeline_json = R"({"duration_s":2.0,"beats":[]})";
                            shine::novelcore::ShotRow second;
                            second.scene_id = scene_id;
                            second.ord = 2;
                            second.start_state_json = R"({"characters":[]})";
                            second.timeline_json = R"({"duration_s":2.0,"beats":[]})";
                            auto a = visual.UpsertShot(first);
                            auto b = visual.UpsertShot(second);
                            if (!a || !b) line("fixture-shot", false, "连续性镜头创建失败");
                        }
                    }
                }
                const fs::path work = root / "work" / "ch001";
                fs::create_directories(work, ec);
                (void)shine::util::WriteFileBytes(
                    work / "storyboard.json",
                    R"({"shots":[{"scene_ord":1,"ord":1,"transition":""},{"scene_ord":1,"ord":2,"performance":{},"spatial":{},"camera":{}}]})");
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            const bool started = opened && workspace.RunContinuityCheck();
            QEventLoop loop;
            QElapsedTimer elapsed;
            elapsed.start();
            QTimer poll;
            bool timeout = false;
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                if (workspace.ContinuityProbe().contains(QStringLiteral("checked=1")) ||
                    elapsed.elapsed() > 10000) {
                    timeout = elapsed.elapsed() > 10000;
                    poll.stop();
                    loop.quit();
                }
            });
            poll.start(20);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            const std::string probe = workspace.ContinuityProbe().toStdString();
            line("continuity-codes", started && !timeout && probe.find("checked=1") != std::string::npos &&
                                      probe.find("issues=1") != std::string::npos &&
                                      probe.find("failed=1") != std::string::npos,
                 "C1–C12 机器校验定位到具体 C1 离场不连续");
            line("continuity-report", probe.find("report=") != std::string::npos,
                 "连续性报告路径可追踪");
            content += "CONTINUITY-PROBE:\n" + probe + "\n";
            content += fails == 0 ? "[P06-S6] overall: PASS" : "[P06-S6] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S7"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s7-" + shine::util::RandomHex(8));
            std::error_code ec;
            fs::create_directories(root / "db", ec);
            std::int64_t scene_id = 0;
            std::int64_t chapter_id = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "桥接章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else {
                        chapter_id = *chapter;
                        auto scene = graph.UpsertScene({.chapter_id = chapter_id, .ord = 1,
                                                          .title = "桥接场景", .goal = "导出"});
                        if (!scene) {
                            line("fixture-scene", false, scene.error().message.c_str());
                        } else {
                            scene_id = *scene;
                            shine::novelcore::NovelVisual visual(db);
                            for (int ord = 1; ord <= 2; ++ord) {
                                shine::novelcore::ShotRow shot;
                                shot.scene_id = scene_id;
                                shot.ord = ord;
                                shot.action = ord == 1 ? "推近" : "拉远";
                                shot.prompt_text = "镜头动作：" + shot.action;
                                shot.negative_text = "watermark";
                                shot.timeline_json = R"({"duration_s":2.0,"beats":[]})";
                                if (!visual.UpsertShot(shot)) {
                                    line("fixture-shot", false, "桥接镜头创建失败");
                                }
                            }
                        }
                    }
                }
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            const bool first = opened && workspace.RunGenShotBridge();
            const std::filesystem::path export_path = root / "work" / "ch001" / "shots.json";
            const auto first_bytes = shine::util::ReadFileBytes(export_path);
            const bool second = opened && workspace.RunGenShotBridge();
            const auto second_bytes = shine::util::ReadFileBytes(export_path);
            const std::string probe = workspace.GenShotProbe().toStdString();
            line("to-gen-shot", first && second && probe.find("ok=1") != std::string::npos &&
                                    probe.find("shots=2") != std::string::npos,
                 "NarrativeShot 转换为可提交 VideoProject");
            line("export-shots-json", first_bytes && second_bytes &&
                                          first_bytes->find("\"shots\"") != std::string::npos,
                 "shots.json 导出并可读");
            line("deterministic", first_bytes && second_bytes && *first_bytes == *second_bytes,
                 "重复转换字节级稳定");
            content += "GENSHOT-PROBE:\n" + probe + "\n";
            content += fails == 0 ? "[P06-S7] overall: PASS" : "[P06-S7] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
    if (const std::filesystem::path out = EnvironmentPath(L"SHINE_P06_S8"); !out.empty()) {
        QTimer::singleShot(600, &window, [out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const char* name, bool pass, const char* detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name);
            };
            const fs::path root = fs::temp_directory_path() /
                                  ("shinetv-p06s8-" + shine::util::RandomHex(8));
            std::error_code ec;
            fs::create_directories(root / "db", ec);
            std::int64_t scene_id = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message.c_str());
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message.c_str());
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    auto chapter = volume ? graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                                   .title = "落库章", .status = "done"})
                                          : std::unexpected(volume.error());
                    if (!chapter) {
                        line("fixture-chapter", false, chapter.error().message.c_str());
                    } else if (auto scene = graph.UpsertScene({.chapter_id = *chapter, .ord = 1,
                                                                     .title = "落库场景", .goal = "落库"})) {
                        scene_id = *scene;
                    } else {
                        line("fixture-scene", false, "场景创建失败");
                    }
                }
            }
            shine::app::StoryboardWorkspace workspace;
            QString error;
            const bool opened = scene_id > 0 && workspace.OpenBook(root / "db" / "novel.db", root, &error) &&
                                workspace.SelectScene(scene_id);
            workspace.SetLlm([](shine::agent::LlmRole, std::string_view, std::string_view)
                                  -> std::expected<std::string, shine::agent::AgentError> {
                return std::string{R"({"shots":[{"scene_ord":1,"ord":1,"duration":2.0,"start_state":{"characters":[],"props":[]},"end_state":{"characters":[],"props":[]},"timeline":[{"begin_s":0.0,"end_s":2.0}],"prompt_text":"角色推门进入","negative_text":"watermark","dialogue":[],"transition":"cut"}]})"};
            });
            const auto wait_persistence = [&workspace] {
                QEventLoop loop;
                QElapsedTimer elapsed;
                elapsed.start();
                QTimer poll;
                bool timeout = false;
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                    if (workspace.PersistenceProbe().contains(QStringLiteral("running=0")) ||
                        elapsed.elapsed() > 15000) {
                        timeout = elapsed.elapsed() > 15000;
                        poll.stop();
                        loop.quit();
                    }
                });
                poll.start(20);
                loop.exec(QEventLoop::ExcludeUserInputEvents);
                return !timeout;
            };
            const bool first_started = opened && workspace.RunStoryboardPersistence();
            const bool first_done = first_started && wait_persistence();
            const std::string first_probe = workspace.PersistenceProbe().toStdString();
            bool first_prompt_version = false;
            std::int64_t prompt_id = 0;
            int first_prompt_count = 0;
            {
                shine::db::sqlite::Database db;
                if (auto verify = db.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false}); verify.has_value()) {
                    shine::novelcore::NovelVisual visual(db);
                    if (auto list = visual.ListPromptArtifacts(1); list && !list->empty()) {
                        first_prompt_count = static_cast<int>(list->size());
                        prompt_id = list->front().id;
                        first_prompt_version = list->front().version == 1;
                    }
                }
            }
            line("v9-v10-store", first_done && first_probe.find("v9=1") != std::string::npos &&
                                    first_probe.find("v10=1") != std::string::npos &&
                                    first_prompt_count == 1,
                 "V9 shots 与 V10 prompt_artifacts 均已落库");
            bool changed = false;
            if (opened) {
                shine::db::sqlite::Database db;
                if (auto writable = db.Open({.path = root / "db" / "novel.db", .readOnly = false,
                                             .create = false}); writable.has_value()) {
                    changed = static_cast<bool>(db.Exec(
                        "INSERT INTO prompt_layers(owner_kind,owner_id,layer,text,version) "
                        "VALUES('global',0,'base','probe-layer',1)"));
                }
            }
            const bool second_started = changed && workspace.RunPromptPersistence();
            const bool second_done = second_started && wait_persistence();
            const std::string second_probe = workspace.PersistenceProbe().toStdString();
            bool version_incremented = false;
            int second_prompt_count = 0;
            {
                shine::db::sqlite::Database db;
                if (auto verify = db.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false}); verify.has_value()) {
                    shine::novelcore::NovelVisual visual(db);
                    if (auto list = visual.ListPromptArtifacts(1); list && !list->empty()) {
                        second_prompt_count = static_cast<int>(list->size());
                        version_incremented = list->front().version == 2;
                    }
                }
            }
            const std::string version_detail = "changed=" + std::to_string(changed) +
                                               "; second_started=" + std::to_string(second_started) +
                                               "; second_done=" + std::to_string(second_done) +
                                               "; count=" + std::to_string(second_prompt_count) +
                                               "; " + second_probe;
            line("version-invalidation", second_done && version_incremented && second_prompt_count == 2,
                 version_detail.c_str());
            bool bidirectional = false;
            {
                shine::db::sqlite::Database db;
                if (auto writable = db.Open({.path = root / "db" / "novel.db", .readOnly = false,
                                             .create = false}); writable.has_value() && prompt_id > 0) {
                    shine::novelcore::NovelVisual visual(db);
                    auto asset_id = visual.UpsertAsset({.name = "probe-asset"});
                    if (!asset_id) {
                        line("generation-ref-asset", false, asset_id.error().message.c_str());
                    }
                    shine::novelcore::VisualArtifactRow artifact;
                    artifact.layer = "shot";
                    artifact.rel_path = "visual/generated/probe.png";
                    artifact.prompt_artifact_id = prompt_id;
                    artifact.asset_id = asset_id.value_or(0);
                    auto artifact_id = visual.UpsertArtifact(artifact);
                    auto prompt = visual.GetPromptArtifact(prompt_id);
                    if (artifact_id && prompt) {
                        prompt->generation_ref = "va:" + std::to_string(*artifact_id);
                        const auto saved = visual.UpsertPromptArtifact(*prompt);
                        const auto back = visual.GetArtifact(*artifact_id);
                        bidirectional = saved.has_value() && back.has_value() &&
                                        back->prompt_artifact_id == prompt_id &&
                                        visual.GetPromptArtifact(prompt_id)->generation_ref ==
                                            "va:" + std::to_string(*artifact_id);
                    }
                }
            }
            line("generation-ref", bidirectional, "prompt_artifacts.generation_ref 与 visual_artifacts.prompt_artifact_id 双向可查");
            content += "PERSISTENCE-PROBE:\n" + first_probe + "\n";
            content += fs::is_regular_file(root / "work" / "ch001" / "storyboard.json")
                           ? "storyboard.json=present\n"
                           : "storyboard.json=missing\n";
            content += fails == 0 ? "[P06-S8] overall: PASS" : "[P06-S8] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }
}

} // namespace shine::app::checks
