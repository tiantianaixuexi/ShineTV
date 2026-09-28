#include "ui/verify/checks/P05Checks.h"
#include "ui/app/AppEnvironment.h"
#include "ui/pages/assets/AssetWorkspace.h"
#include "ui/pages/assets/AssetDetailView.h"
#include "ui/verify/gallery/GalleryWorkspace.h"
#include "ui/pages/shell/MainWindow.h"
#include "db/sqlite/SqliteDb.h"
#include "media/ExifOrientation.h"
#include "media/Gallery.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelAssetPipeline.h"
#include "novel/NovelImageStore.h"
#include "novel/NovelVisual.h"
#include "visual/ReferenceLibrary.h"
#include "core/Settings.h"
#include "project/Project.h"
#include "util/File.h"
#include "util/Encoding.h"
#include "util/Random.h"

#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QColor>
#include <QImageReader>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::app::checks {

void RegisterP05Checks(MainWindow& window) {
    // P05-S1 判定：SHINE_P05_S1=<文件> AssetWorkspace（实体树 + 资产卡 + 空态）
    if (const std::filesystem::path s5Out = EnvironmentPath(L"SHINE_P05_S1"); !s5Out.empty()) {
        QTimer::singleShot(600, &window, [&window, s5Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s1-" + shine::util::RandomHex(8));
            shine::project::ProjectFile pf;
            pf.id = "prj_p05s10000000ff";
            pf.name = "视觉资产样例";
            pf.templateId = "novel";
            pf.createdAt = shine::project::Iso8601UtcNow();
            (void)shine::project::SaveProjectFile(root, pf);
            std::error_code ec;
            fs::create_directories(root / "db", ec);
shine::util::EnsureDir(root / "assets" / "refs");
            shine::novelcore::RowId personId = 0;
            {
                shine::db::sqlite::Database db;
                if (auto opened = db.Open({.path = root / "db" / "novel.db"}); !opened) {
                    line("fixture-db", false, opened.error().message);
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message);
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto person = graph.UpsertEntity({.kind = "person", .name = "林默",
                                                        .summary = "主角"});
                    auto location = graph.UpsertEntity({.kind = "location", .name = "黑森林",
                                                         .summary = "场景"});
                    auto item = graph.UpsertEntity({.kind = "item", .name = "铜钥匙",
                                                     .summary = "道具"});
                    auto faction = graph.UpsertEntity({.kind = "faction", .name = "守夜人",
                                                        .summary = "势力"});
                    if (person && location && item && faction) personId = *person;
                }
            }

            shine::app::AssetWorkspace empty;
            QString err;
            const bool open = personId > 0 && empty.OpenBook(root / "db" / "novel.db", root, &err);
            const std::string emptyProbe = open ? empty.AssetProbe().toStdString() : "open=false";
            line("empty-state", open && (contains(emptyProbe, "empty=1") ||
                                         contains(emptyProbe, "empty-state") ||
                                         contains(emptyProbe, "生成设定集")),
                 "没有视觉资产时显示空态与「生成设定集」下一步");

            {
                shine::db::sqlite::Database db;
                if (auto opened = db.Open({.path = root / "db" / "novel.db"}); opened) {
                    shine::novelcore::NovelVisual visual(db);
                    shine::novelcore::VisualAssetRow row;
                    row.entity_id = personId;
                    row.kind = "character";
                    row.name = "林默 · 角色设定集";
                    row.status = "SHEET_READY";
                    row.sheet_rel_path = "assets/refs/lin-mu.png";
                    (void)visual.UpsertAsset(row);
                }
            }
            shine::app::AssetWorkspace assets;
            const bool reopened = personId > 0 && assets.OpenBook(root / "db" / "novel.db", root, &err);
            if (reopened) assets.SelectEntity(personId);
            const std::string assetProbe = reopened ? assets.AssetProbe().toStdString() : "open=false";
            line("entity-tree", reopened && contains(assetProbe, "entities=4") &&
                                 contains(assetProbe, "selected=林默#1"),
                 "实体树按人物/地点/物品/势力加载，林默可选");
            line("asset-grid", reopened && contains(assetProbe, "assets=") &&
                                contains(assetProbe, "SHEET_READY") && contains(assetProbe, "林默"),
                 "资产卡展示实体绑定、状态与设定集名称");

            window.SwitchWorkspace(2);
            line("workspace-navigation", window.AssetPage() != nullptr,
                 "活动栏「视觉资产」入口创建并切换到 AssetWorkspace 文档页");

            content += "ASSET-PROBE(empty):\n" + emptyProbe + "\n";
            content += "ASSET-PROBE(full):\n" + assetProbe + "\n";
            content += fails == 0 ? "[P05-S1] overall: PASS" : "[P05-S1] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s5Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S2 判定：四层详情 + 父子派生链 + 缺层行动 + 中文图像路径。
    if (const std::filesystem::path s2Out = EnvironmentPath(L"SHINE_P05_S2"); !s2Out.empty()) {
        QTimer::singleShot(600, &window, [&window, s2Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s2-" + shine::util::RandomHex(8));
            std::error_code ec;
            fs::create_directories(root / "db", ec);
shine::util::EnsureDir(root / "assets" / "中文参考");
            shine::novelcore::RowId personId = 0;
            shine::novelcore::RowId assetId = 0;
            shine::novelcore::RowId frontId = 0;
            shine::novelcore::RowId turnaroundId = 0;
            shine::novelcore::RowId baseBodyId = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    line("fixture-db", false, opened.error().message);
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    line("fixture-schema", false, schema.error().message);
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto person = graph.UpsertEntity({.kind = "person", .name = "林默",
                                                        .summary = "主角"});
                    if (!person) {
                        line("fixture-entity", false, person.error().message);
                    } else {
                        personId = *person;
                        shine::novelcore::NovelVisual visual(db);
                        shine::novelcore::VisualAssetRow asset;
                        asset.entity_id = personId;
                        asset.kind = "character";
                        asset.name = "林默 · 角色设定集";
                        asset.base_desc = "黑色短发，灰色风衣";
                        asset.sheet_rel_path = "assets/中文参考/正脸.png";
                        asset.status = "SHEET_READY";
                        auto saved = visual.UpsertAsset(asset);
                        if (!saved) {
                            line("fixture-asset", false, saved.error().message);
                        } else {
                            assetId = *saved;
                            const auto write_image = [&](const char* name) {
                                QImage image(64, 64, QImage::Format_RGB32);
                                image.fill(Qt::lightGray);
                                const fs::path path = root / "assets" / "中文参考" / name;
                                return image.save(
                                    QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
                            };
                            const auto write_artifact = [&](std::string layer, shine::novelcore::RowId parent,
                                                            const char* file) {
                                shine::novelcore::VisualArtifactRow artifact;
                                artifact.asset_id = assetId;
                                artifact.layer = std::move(layer);
                                artifact.rel_path = std::string{"assets/中文参考/"} + file;
                                artifact.parent_artifact_id = parent;
                                artifact.status = "DONE";
                                return visual.UpsertArtifact(artifact);
                            };
                            auto front = write_artifact("front", 0, "正脸.png");
                            auto turnaround = front
                                                  ? write_artifact("turnaround", *front, "四视图.png")
                                                  : std::unexpected(front.error());
                            auto base_body = turnaround
                                                 ? write_artifact("base_body", *turnaround, "基础身体.png")
                                                 : std::unexpected(turnaround.error());
                            if (!front || !turnaround || !base_body || !write_image("正脸.png") ||
                                !write_image("四视图.png") || !write_image("基础身体.png")) {
                                line("fixture-artifacts", false,
                                     "三层形象产物或中文图像写入失败");
                            } else {
                                frontId = *front;
                                turnaroundId = *turnaround;
                                baseBodyId = *base_body;
                                line("fixture-artifacts", true, "front → turnaround → base_body 已落库");
                            }
                        }
                    }
                }
            }

            shine::app::AssetWorkspace workspace;
            QString openError;
            const bool opened = personId > 0 && assetId > 0 && frontId > 0 && turnaroundId > 0 &&
                                baseBodyId > 0 &&
                                workspace.OpenBook(root / "db" / "novel.db", root, &openError) &&
                                workspace.SelectEntity(personId);
            const std::string detail = opened ? workspace.DetailProbe().toStdString() : "open=false";
            line("sheet-grid", opened && contains(detail, "layers=4") && contains(detail, "ready=3") &&
                                    contains(detail, "decoded=3"),
                 "正脸/四视图/基础身体/服装四层固定呈现，三层图像真实解码");
            line("derivation-chain", opened && contains(detail, "links=3") &&
                                       contains(detail, "chain=front>turnaround>base_body>wardrobe") &&
                                       contains(detail, "front=DONE>turnaround=DONE>base_body=DONE>wardrobe=PENDING"),
                 "父子派生链和每层状态可视");
            line("missing-layer-action", opened && contains(detail, "missing=1") &&
                                          contains(detail, "actions=1"),
                 "缺层显示占位与生成/重试行动");
            line("chinese-path", opened && contains(detail, "decoded=3"),
                 "中文目录和文件名经 UTF-8 路径链正确解码");

            content += "DETAIL-PROBE:\n" + detail + "\n";
            content += fails == 0 ? "[P05-S2] overall: PASS" : "[P05-S2] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s2Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S3 判定：GENERATING → CHECKING → READY 即时推进；FAILED 可视；降级账落盘。
    if (const std::filesystem::path s3Out = EnvironmentPath(L"SHINE_P05_S3"); !s3Out.empty()) {
        QTimer::singleShot(600, &window, [s3Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s3-" + shine::util::RandomHex(8));
            std::error_code ec;
shine::util::EnsureDir(root / "db");
            const shine::AppSettings saved_settings = shine::Settings();
            shine::Settings().imageBackend = "mock";
            shine::Settings().imageOutputRelDir = "visual/gen";
            shine::Settings().imageModel = "mock-model";
            shine::novelcore::RowId runEntity = 0;
            shine::novelcore::RowId degradedEntity = 0;
            shine::novelcore::RowId failedEntity = 0;
            shine::novelcore::RowId runAsset = 0;
            shine::novelcore::RowId degradedAsset = 0;
            bool fixture_ok = true;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    fixture_ok = false;
                    line("fixture-db", false, opened.error().message);
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    fixture_ok = false;
                    line("fixture-schema", false, schema.error().message);
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto run = graph.UpsertEntity({.kind = "person", .name = "状态机主角"});
                    auto degraded = graph.UpsertEntity({.kind = "person", .name = "降级角色"});
                    auto failed = graph.UpsertEntity({.kind = "person", .name = "失败角色"});
                    if (!run || !degraded || !failed) {
                        fixture_ok = false;
                        line("fixture-entity", false, "实体创建失败");
                    } else {
                        runEntity = *run;
                        degradedEntity = *degraded;
                        failedEntity = *failed;
                        shine::novelcore::NovelVisual visual(db);
                        auto run_saved = visual.UpsertAsset({.entity_id = runEntity,
                                                             .kind = "character",
                                                             .name = "状态机主角",
                                                             .base_desc = "black hair, gray coat"});
                        auto deg_saved = visual.UpsertAsset({.entity_id = degradedEntity,
                                                             .kind = "character",
                                                             .name = "降级角色",
                                                             .base_desc = "long hair, blue coat"});
                        auto fail_saved = visual.UpsertAsset({.entity_id = failedEntity,
                                                              .kind = "character",
                                                              .name = "失败角色",
                                                              .status = "FAILED",
                                                              .note = "fixture failure"});
                        if (!run_saved || !deg_saved || !fail_saved) {
                            fixture_ok = false;
                            line("fixture-assets", false, "资产创建失败");
                        } else {
                            runAsset = *run_saved;
                            degradedAsset = *deg_saved;
                            shine::novelcore::AssetPipelineOptions force;
                            force.suspendTimeoutMs = 0;
                            force.width = 64;
                            force.height = 64;
                            force.steps = 1;
                            auto degraded_run = shine::novelcore::RunAssetLayer(
                                db, degradedAsset, shine::novelcore::AssetLayer::Wardrobe, force);
                            if (!degraded_run ||
                                degraded_run->outcome !=
                                    shine::novelcore::PipelineOutcome::Degraded) {
                                fixture_ok = false;
                                line("fixture-degrade", false, "降级夹具未生成");
                            }
                        }
                    }
                }
            }
            line("fixture", fixture_ok, "PENDING / FAILED / 强制 no_reference 三类资产已准备");

            shine::app::AssetWorkspace workspace;
            QString open_error;
            const bool opened = fixture_ok && runEntity > 0 && degradedEntity > 0 && failedEntity > 0 &&
                                workspace.OpenBook(root / "db" / "novel.db", root, &open_error) &&
                                workspace.SelectEntity(runEntity);
            const bool started = opened && workspace.StartAssetPipeline();
            const std::string immediate = workspace.StateProbe().toStdString();
            line("state-machine-immediate", started && contains(immediate, "runtime=GENERATING") &&
                                             contains(immediate, "active=1"),
                 "提交后卡片立即进入 GENERATING，不等待 worker 完成");

            QEventLoop loop;
            QElapsedTimer elapsed;
            elapsed.start();
            QTimer poll;
            bool timed_out = false;
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                const std::string state = workspace.StateProbe().toStdString();
                if (contains(state, "active=0") || elapsed.elapsed() > 10000) {
                    timed_out = elapsed.elapsed() > 10000;
                    poll.stop();
                    loop.quit();
                }
            });
            poll.start(20);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            shine::Settings() = saved_settings;

            const std::string final_state = workspace.StateProbe().toStdString();
            const std::string final_card = workspace.AssetProbe().toStdString();
            line("state-machine-run", !timed_out && contains(final_state, "runtime=READY") &&
                                          contains(final_state, "history=GENERATING>CHECKING>READY") &&
                                          contains(final_state, "active=0") &&
                                          contains(final_card, "status=READY"),
                 "worker 完成后经过 CHECKING 收敛到 READY，卡片即时刷新");

            int done_layers = 0;
            bool files_ok = true;
            {
                shine::db::sqlite::Database verify;
                if (auto db = verify.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false});
                    db) {
                    shine::novelcore::NovelVisual visual(verify);
                    if (auto artifacts = visual.ListArtifacts(runAsset)) {
                        for (const auto& artifact : *artifacts) {
                            if (artifact.status == "DONE") {
                                ++done_layers;
                            }
                            const fs::path path = root / shine::util::PathFromUtf8(artifact.rel_path);
                            if (artifact.status == "DONE" && !fs::is_regular_file(path, ec)) {
                                files_ok = false;
                            }
                        }
                    }
                }
            }
            line("state-machine-artifacts", done_layers == 4 && files_ok,
                 "四层 DONE 产物均真实落盘");

            const bool selected_degraded = workspace.SelectEntity(degradedEntity);
            const std::string degraded_card = workspace.AssetProbe().toStdString();
            line("degraded-badge", selected_degraded && contains(degraded_card, "degraded=1"),
                 "visual_artifacts.degraded=1 在资产卡上可见");
            const auto ledger = shine::util::ReadFileBytes(root / "assets" / "degradations.jsonl");
            line("degradation-ledger", ledger && contains(*ledger, "\"kind\":\"no_reference\"") &&
                                        contains(*ledger, "\"task\":\"asset#"),
                 "no_reference 降级追加到 assets/degradations.jsonl");

            const bool selected_failed = workspace.SelectEntity(failedEntity);
            const std::string failed_card = workspace.AssetProbe().toStdString();
            line("failed-state", selected_failed && contains(failed_card, "status=FAILED"),
                 "FAILED 状态在卡片上可辨识");

            content += "STATE-PROBE(immediate):\n" + immediate + "\n";
            content += "STATE-PROBE(final):\n" + final_state + "\n";
            content += "ASSET-PROBE(degraded):\n" + degraded_card + "\n";
            content += "ASSET-PROBE(failed):\n" + failed_card + "\n";
            content += fails == 0 ? "[P05-S3] overall: PASS" : "[P05-S3] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s3Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S4 判定：缺依赖按 C 挂起；超期按 B 降级；严格模式拒绝降级。
    if (const std::filesystem::path s4Out = EnvironmentPath(L"SHINE_P05_S4"); !s4Out.empty()) {
        QTimer::singleShot(600, &window, [s4Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s4-" + shine::util::RandomHex(8));
            std::error_code ec;
shine::util::EnsureDir(root / "db");
            const shine::AppSettings saved_settings = shine::Settings();
            shine::Settings().imageBackend = "mock";
            shine::Settings().imageOutputRelDir = "visual/gen";
            shine::Settings().imageModel = "mock-model";
            shine::novelcore::RowId waitingEntity = 0;
            shine::novelcore::RowId degradedEntity = 0;
            shine::novelcore::RowId strictEntity = 0;
            bool fixture_ok = true;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    fixture_ok = false;
                    line("fixture-db", false, opened.error().message);
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    fixture_ok = false;
                    line("fixture-schema", false, schema.error().message);
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto waiting = graph.UpsertEntity({.kind = "person", .name = "等待角色"});
                    auto degraded = graph.UpsertEntity({.kind = "person", .name = "超时角色"});
                    auto strict = graph.UpsertEntity({.kind = "person", .name = "严格角色"});
                    if (!waiting || !degraded || !strict) {
                        fixture_ok = false;
                        line("fixture-entity", false, "实体创建失败");
                    } else {
                        waitingEntity = *waiting;
                        degradedEntity = *degraded;
                        strictEntity = *strict;
                        shine::novelcore::NovelVisual visual(db);
                        auto a = visual.UpsertAsset({.entity_id = waitingEntity,
                                                     .kind = "character",
                                                     .name = "等待角色"});
                        auto b = visual.UpsertAsset({.entity_id = degradedEntity,
                                                     .kind = "character",
                                                     .name = "超时角色"});
                        auto c = visual.UpsertAsset({.entity_id = strictEntity,
                                                     .kind = "character",
                                                     .name = "严格角色"});
                        fixture_ok = a && b && c;
                        if (!fixture_ok) {
                            line("fixture-assets", false, "资产创建失败");
                        }
                    }
                }
            }
            line("fixture", fixture_ok, "三套缺依赖资产已准备");

            shine::app::AssetWorkspace workspace;
            QString open_error;
            const bool opened = fixture_ok && workspace.OpenBook(root / "db" / "novel.db", root,
                                                                  &open_error);
            const auto wait_idle = [&workspace] {
                QEventLoop loop;
                QElapsedTimer elapsed;
                elapsed.start();
                QTimer poll;
                bool timeout = false;
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                    if (workspace.StateProbe().contains(QStringLiteral("active=0")) ||
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

            workspace.SelectEntity(waitingEntity);
            const std::string default_policy = workspace.PolicyProbe().toStdString();
            const bool wait_started = opened && workspace.StartAssetLayer(
                                                          shine::novelcore::AssetLayer::Wardrobe);
            const bool wait_done = wait_started && wait_idle();
            const std::string wait_state = workspace.StateProbe().toStdString();
            const std::string wait_policy = workspace.PolicyProbe().toStdString();
            line("policy-default", contains(default_policy, "policy=C>B") &&
                                      contains(default_policy, "allowDegrade=1") &&
                                      contains(default_policy, "timeoutMs=1800000"),
                 "默认策略为 C 挂起 + 30 分钟后 B 降级");
            line("dependency-suspend", wait_done && contains(wait_state, "runtime=PENDING") &&
                                           contains(wait_state, "allowDegrade=1") &&
                                           contains(wait_policy, "runtime=PENDING"),
                 "缺 base_body 时策略 C 挂起，未生成也未降级");

            workspace.SelectEntity(degradedEntity);
            workspace.SetAssetPolicy({.suspendTimeoutMs = 0, .allowDegrade = true});
            const bool degrade_started = workspace.StartAssetLayer(
                shine::novelcore::AssetLayer::Wardrobe);
            const bool degrade_done = degrade_started && wait_idle();
            const std::string degrade_state = workspace.StateProbe().toStdString();
            const std::string degrade_policy = workspace.PolicyProbe().toStdString();
            const std::string degrade_card = workspace.AssetProbe().toStdString();
            line("timeout-degrade", degrade_done &&
                                        contains(degrade_state, "runtime=WARDROBE_READY") &&
                                        contains(degrade_state, "degraded=1") &&
                                        contains(degrade_policy, "allowDegrade=1") &&
                                        contains(degrade_policy, "timeoutMs=0") &&
                                        contains(degrade_card, "degraded=1"),
                 "挂起超期后策略 B 生成，卡片与策略面板均标降级");

            workspace.SelectEntity(strictEntity);
            workspace.SetAssetPolicy({.suspendTimeoutMs = 0, .allowDegrade = false});
            const bool strict_started = workspace.StartAssetLayer(
                shine::novelcore::AssetLayer::Wardrobe);
            const bool strict_done = strict_started && wait_idle();
            const std::string strict_state = workspace.StateProbe().toStdString();
            const std::string strict_policy = workspace.PolicyProbe().toStdString();
            const std::string strict_card = workspace.AssetProbe().toStdString();
            line("strict-mode", strict_done && contains(strict_state, "runtime=PENDING") &&
                                   contains(strict_state, "allowDegrade=0") &&
                                   contains(strict_policy, "strict=1") &&
                                   contains(strict_card, "degraded=0"),
                 "严格模式即使超时为 0 也只挂起，拒绝降级");

            int suspend_audits = 0;
            int degrade_audits = 0;
            {
                shine::db::sqlite::Database verify;
                if (auto db = verify.Open({.path = root / "db" / "novel.db", .readOnly = true,
                                           .create = false});
                    db) {
                    if (auto count = verify.Prepare(
                            "SELECT COUNT(*) FROM audit_logs WHERE action='asset_suspend'");
                        count) {
                        if (auto row = count->Step();
                            row && *row == shine::db::sqlite::StepResult::Row) {
                            suspend_audits = count->ColumnInt(0);
                        }
                    }
                    if (auto count = verify.Prepare(
                            "SELECT COUNT(*) FROM audit_logs WHERE action='asset_degrade'");
                        count) {
                        if (auto row = count->Step();
                            row && *row == shine::db::sqlite::StepResult::Row) {
                            degrade_audits = count->ColumnInt(0);
                        }
                    }
                }
            }
            line("audit-policy", suspend_audits >= 2 && degrade_audits == 1,
                 "C 挂起与 B 降级均写 audit_logs");
            const auto ledger = shine::util::ReadFileBytes(root / "assets" / "degradations.jsonl");
            line("degradation-ledger", ledger && contains(*ledger, "\"kind\":\"no_reference\""),
                 "唯一允许的降级写入 assets/degradations.jsonl");
            shine::Settings() = saved_settings;

            content += "POLICY-PROBE(default):\n" + default_policy + "\n";
            content += "STATE-PROBE(wait):\n" + wait_state + "\n";
            content += "POLICY-PROBE(degrade):\n" + degrade_policy + "\n";
            content += "STATE-PROBE(degrade):\n" + degrade_state + "\n";
            content += "STATE-PROBE(strict):\n" + strict_state + "\n";
            content += fails == 0 ? "[P05-S4] overall: PASS" : "[P05-S4] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s4Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S5 判定：按章外观 / 表情基线 + 同角色跨镜头并排与差异高亮。
    if (const std::filesystem::path s5Out = EnvironmentPath(L"SHINE_P05_S5"); !s5Out.empty()) {
        QTimer::singleShot(600, &window, [s5Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s5-" + shine::util::RandomHex(8));
            std::error_code ec;
shine::util::EnsureDir(root / "db");
            const shine::AppSettings saved_settings = shine::Settings();
            shine::Settings().imageBackend = "mock";
            shine::Settings().imageOutputRelDir = "visual/gen";
            shine::Settings().imageModel = "mock-model";
            shine::novelcore::RowId entityId = 0;
            shine::novelcore::RowId assetId = 0;
            bool fixture_ok = true;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    fixture_ok = false;
                    line("fixture-db", false, opened.error().message);
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    fixture_ok = false;
                    line("fixture-schema", false, schema.error().message);
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto person = graph.UpsertEntity({.kind = "person", .name = "苏黎",
                                                        .summary = "一致性夹具"});
                    if (!person) {
                        fixture_ok = false;
                        line("fixture-entity", false, person.error().message);
                    } else {
                        entityId = *person;
                        shine::novelcore::NovelVisual visual(db);
                        auto asset = visual.UpsertAsset({.entity_id = entityId,
                                                          .kind = "character",
                                                          .name = "苏黎 · 一致性基准",
                                                          .base_desc = "black hair, gray coat",
                                                          .status = "READY"});
                        auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                        if (!asset || !volume) {
                            fixture_ok = false;
                            line("fixture-asset", false, "资产或卷创建失败");
                        } else {
                            assetId = *asset;
                            std::array<shine::novelcore::RowId, 3> chapters{};
                            for (std::size_t i = 0; i < chapters.size(); ++i) {
                                auto chapter = graph.UpsertChapter({.volume_id = *volume,
                                                                     .ord = static_cast<int>(i + 1),
                                                                     .title = QStringLiteral("第 %1 章")
                                                                                   .arg(i + 1)
                                                                                   .toStdString(),
                                                                     .status = "done"});
                                if (!chapter) {
                                    fixture_ok = false;
                                    break;
                                }
                                chapters[i] = *chapter;
                            }

                            std::array<shine::novelcore::RowId, 2> shots{};
                            for (std::size_t i = 0; i < shots.size() && fixture_ok; ++i) {
                                auto scene = graph.UpsertScene({.chapter_id = chapters[i], .ord = 1,
                                                                .title = "场景"});
                                if (!scene) {
                                    fixture_ok = false;
                                    break;
                                }
                                shine::novelcore::ShotRow shot;
                                shot.scene_id = *scene;
                                shot.ord = 1;
                                shot.character_ids_json = "[" + std::to_string(entityId) + "]";
                                shot.action = "站立";
                                auto saved = visual.UpsertShot(shot);
                                if (!saved) {
                                    fixture_ok = false;
                                    break;
                                }
                                shots[i] = *saved;
                            }

                            struct VisualStateSpec {
                                const char* key;
                                const char* label;
                                const char* appearance;
                                const char* colors;
                            };

                            const std::array<VisualStateSpec, 3> state_specs{{
                                {"baseline", "初始", "短发", "灰风衣"},
                                {"wound", "左颊伤", "短发", "灰风衣、深色血痕"},
                                {"rain", "雨夜", "湿发", "黑大衣"},
                            }};
                            for (std::size_t i = 0; i < state_specs.size() && fixture_ok; ++i) {
                                shine::novelcore::VisualStateRow state;
                                state.asset_id = assetId;
                                state.stage_key = state_specs[i].key;
                                state.stage_label = state_specs[i].label;
                                state.ord = static_cast<int>(i + 1);
                                state.from_chapter = chapters[i];
                                state.appearance = state_specs[i].appearance;
                                state.materials_colors = state_specs[i].colors;
                                fixture_ok = visual.UpsertState(state).has_value();
                            }
                            const std::array<std::string, 3> emotions{
                                "{\"fear\":10,\"trust\":70}",
                                "{\"fear\":45,\"trust\":55}",
                                "{\"fear\":80,\"trust\":30}",
                            };
                            for (std::size_t i = 0; i < emotions.size() && fixture_ok; ++i) {
                                shine::novelcore::CharacterStatusRow status;
                                status.entity_id = entityId;
                                status.chapter_id = chapters[i];
                                status.mind_state = state_specs[i].label;
                                status.emotion_json = emotions[i];
                                fixture_ok = graph.UpsertCharacterStatus(status).has_value();
                            }

                            const auto make_image = [&](shine::novelcore::RowId shot_id,
                                                        const QColor& color) {
                                shine::novelcore::ImageJobInput input;
                                input.prompt = "Su Li character shot";
                                input.negative = "lowres";
                                input.shot_id = shot_id;
                                input.width = 64;
                                input.height = 64;
                                input.steps = 1;
                                auto job = shine::novelcore::RunImageJob(db, input);
                                if (!job) {
                                    return false;
                                }
                                QImage image(64, 64, QImage::Format_RGB32);
                                image.fill(color);
                                const fs::path path = root / shine::util::PathFromUtf8(job->rel_path);
                                return image.save(
                                    QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
                            };
                            if (fixture_ok) {
                                fixture_ok = make_image(shots[0], Qt::white) &&
                                             make_image(shots[1], Qt::black);
                            }
                        }
                    }
                }
            }
            shine::Settings() = saved_settings;
            line("fixture", fixture_ok, "3 个外观阶段、3 条情绪、2 张同角色镜头图已准备");

            shine::app::AssetWorkspace workspace;
            QString open_error;
            const bool opened = fixture_ok && workspace.OpenBook(root / "db" / "novel.db", root,
                                                                  &open_error) &&
                                workspace.SelectEntity(entityId);
            const std::string probe = opened ? workspace.ConsistencyProbe().toStdString()
                                             : "open=false";
            line("appearance-baseline", opened && contains(probe, "states=3"),
                 "按章 visual_states 外观基线完整加载");
            line("emotion-baseline", opened && contains(probe, "emotions=3"),
                 "emotion_json 情绪基线完整加载");
            line("cross-shot-compare", opened && contains(probe, "shots=2") &&
                                          contains(probe, "images=2") &&
                                          contains(probe, "compared=1"),
                 "同角色两镜进入 CompareView");
            line("difference-highlight", opened && contains(probe, "diff=100.0") &&
                                            contains(probe, "severity=significant"),
                 "像素差异量化并标为显著差异");

            content += "CONSISTENCY-PROBE:\n" + probe + "\n";
            content += fails == 0 ? "[P05-S5] overall: PASS" : "[P05-S5] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s5Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S6 判定：项目参考图导入 / 标记 / 绑定 / EXIF 方向 / 中文路径 / 拖拽入口。
    if (const std::filesystem::path s6Out = EnvironmentPath(L"SHINE_P05_S6"); !s6Out.empty()) {
        QTimer::singleShot(600, &window, [s6Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s6-" + shine::util::RandomHex(8));
            std::error_code ec;
            fs::create_directories(root / "db", ec);
shine::util::EnsureDir(root / "中文素材");
            const fs::path source = root / "中文素材" / "中文原图.jpg";
            QImage image(40, 20, QImage::Format_RGB32);
            image.fill(Qt::lightGray);
            bool source_ok = image.save(
                QString::fromStdString(shine::util::PathToUtf8(source)), "JPEG", 95);
            std::int64_t entityId = 0;
            std::int64_t assetId = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    source_ok = false;
                    line("fixture-db", false, opened.error().message);
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    source_ok = false;
                    line("fixture-schema", false, schema.error().message);
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto person = graph.UpsertEntity({.kind = "person", .name = "参考图角色"});
                    if (!person) {
                        source_ok = false;
                    } else {
                        entityId = *person;
                        shine::novelcore::NovelVisual visual(db);
                        auto asset = visual.UpsertAsset({.entity_id = entityId,
                                                          .kind = "character",
                                                          .name = "参考图角色",
                                                          .status = "READY"});
                        if (!asset) {
                            source_ok = false;
                        } else {
                            assetId = *asset;
                        }
                    }
                }
            }

            if (source_ok) {
                auto jpeg = shine::util::ReadFileBytes(source);
                if (!jpeg || jpeg->size() < 4 ||
                    static_cast<unsigned char>((*jpeg)[0]) != 0xFF ||
                    static_cast<unsigned char>((*jpeg)[1]) != 0xD8) {
                    source_ok = false;
                } else {
                    constexpr std::array<unsigned char, 36> exif_app1{
                        0xFF, 0xE1, 0x00, 0x20, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00,
                        0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00,
                        0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                    std::vector<unsigned char> bytes(jpeg->begin(), jpeg->end());
                    bytes.insert(bytes.begin() + 2, exif_app1.begin(), exif_app1.end());
                    source_ok = shine::util::WriteFileBytes(source, std::string_view{
                        reinterpret_cast<const char*>(bytes.data()), bytes.size()});
                }
            }
            const auto source_exif = source_ok ? shine::gallery::ReadExif(source)
                                               : shine::gallery::ExifMeta{};
            line("fixture", source_ok && entityId > 0 && assetId > 0 &&
                                source_exif.orientation == shine::gallery::Orientation::Rotate90,
                 "中文 JPEG + EXIF Orientation=6 夹具已准备");

            shine::app::AssetWorkspace workspace;
            QString open_error;
            const bool opened = source_ok && workspace.OpenBook(root / "db" / "novel.db", root,
                                                                  &open_error) &&
                                workspace.SelectEntity(entityId);
            const std::size_t submitted = opened ? workspace.ImportReferences({source}) : 0;
            QEventLoop loop;
            QElapsedTimer elapsed;
            elapsed.start();
            QTimer poll;
            bool timed_out = false;
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                if (contains(workspace.RefProbe().toStdString(), "busy=0") ||
                    elapsed.elapsed() > 10000) {
                    timed_out = elapsed.elapsed() > 10000;
                    poll.stop();
                    loop.quit();
                }
            });
            poll.start(20);
            loop.exec(QEventLoop::ExcludeUserInputEvents);

            shine::visual::ReferenceLibrary library(root);
            auto loaded = library.Load();
            const shine::visual::ReferenceImage* record =
                loaded && !library.Images().empty() ? &library.Images().front() : nullptr;
            bool decoded_size_ok = false;
            if (record != nullptr) {
                QImageReader reader(QString::fromStdString(
                    shine::util::PathToUtf8(library.Resolve(record->rel_path))));
                reader.setAutoTransform(true);
                const QImage decoded = reader.read();
                decoded_size_ok = decoded.width() == 20 && decoded.height() == 40;
            }
            if (record != nullptr) {
                (void)library.SetMarkers(record->id, {"正脸", "灰风衣"});
                (void)library.Bind(record->id, entityId, assetId);
            }
            workspace.RefreshReferences();
            const std::string probe = workspace.RefProbe().toStdString();
            const auto manifest = shine::util::ReadFileBytes(root / "assets" / "refs" / "refs.json");
            line("drag-import", submitted == 1 && contains(probe, "refs=1") &&
                                  contains(probe, "drops=1") && !timed_out,
                 "拖拽入口开启，worker 导入一张项目参考图");
            line("chinese-path", record != nullptr && record->original_name == "中文原图.jpg" &&
                                     contains(probe, "中文原图.jpg") && manifest.has_value(),
                 "中文目录/文件名与 refs.json 全链路无乱码");
            line("exif-correction", record != nullptr &&
                                        record->display_width == 20 && record->display_height == 40 &&
                                        decoded_size_ok,
                 "EXIF 6 按 90° 校正，宽高由 40×20 摆正为 20×40");
            line("marker-binding", contains(probe, "markers=正脸、灰风衣") &&
                                        contains(probe, "entity=1") && manifest.has_value(),
                 "标记和实体/资产绑定持久化并回读");

            content += "REF-PROBE:\n" + probe + "\n";
            content += fails == 0 ? "[P05-S6] overall: PASS" : "[P05-S6] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s6Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S7 判定：三来源扫描 / 虚拟网格 / worker 解码 / 查看器缩放 / 工作流输入。
    if (const std::filesystem::path s7Dir = EnvironmentPath(L"SHINE_P05_S7"); !s7Dir.empty()) {
        const std::filesystem::path s7Out = EnvironmentPath(L"SHINE_P05_S7_OUT");
        QTimer::singleShot(600, &window, [s7Out = s7Out.empty() ? s7Dir / "p05_s7_probe.txt" : s7Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };
            const auto contains = [](const std::string& hay, const std::string& needle) {
                return hay.find(needle) != std::string::npos;
            };
            std::error_code ec;

            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s7-" + shine::util::RandomHex(8));
            const fs::path local = root / "本地图库";
            const fs::path output = root / "comfy-output";
            const fs::path input = root / "comfy-input";
            fs::create_directories(local, ec);
            fs::create_directories(output, ec);
shine::util::EnsureDir(input);
            const auto write_image = [](const fs::path& path, const QColor& color) {
                QImage image(96, 64, QImage::Format_RGB32);
                image.fill(color);
                return image.save(QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
            };
            const bool fixture = write_image(local / "本地图.png", Qt::white) &&
                                 write_image(output / "output.png", Qt::lightGray) &&
                                 write_image(input / "input.png", Qt::darkGray);
            const shine::AppSettings saved = shine::Settings();
            shine::Settings().galleryLocalDir = shine::util::PathToUtf8(local);
            shine::Settings().comfyOutputDir = shine::util::PathToUtf8(output);
            shine::Settings().comfyInputDir = shine::util::PathToUtf8(input);
            shine::gallery::SetLastGraphDropPath(std::string{});
            line("fixture", fixture, "本地 / Comfy 输出 / Comfy 输入三来源各有图片");

            shine::app::GalleryWorkspace workspace;
            workspace.resize(1100, 760);
            workspace.show();
            QApplication::processEvents();
            const auto wait_scan = [&](shine::gallery::SourceKind source) {
                workspace.SelectSource(source);
                QEventLoop loop;
                QElapsedTimer elapsed;
                elapsed.start();
                QTimer poll;
                bool timeout = false;
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                    if ((!shine::gallery::State().scanning &&
                         shine::gallery::State().source == source) ||
                        elapsed.elapsed() > 10000) {
                        timeout = elapsed.elapsed() > 10000;
                        poll.stop();
                        loop.quit();
                    }
                });
                poll.start(20);
                loop.exec(QEventLoop::ExcludeUserInputEvents);
                workspace.Sync();
                QApplication::processEvents();
                return !timeout;
            };

            const bool local_ok = wait_scan(shine::gallery::SourceKind::Local);
            const std::size_t local_count = shine::gallery::Model().Count();
            const bool output_ok = wait_scan(shine::gallery::SourceKind::ComfyOutput);
            const std::size_t output_count = shine::gallery::Model().Count();
            const bool input_ok = wait_scan(shine::gallery::SourceKind::ComfyInput);
            const std::size_t input_count = shine::gallery::Model().Count();
            line("three-sources", local_ok && output_ok && input_ok && local_count == 1 &&
                                    output_count == 1 && input_count == 1,
                 "三来源逐一异步扫描并更新同一图库模型");

            const bool selected = workspace.SelectFirst();
            const bool workflow = selected && workspace.SetSelectedAsWorkflowInput();
            QEventLoop decode_loop;
            QElapsedTimer decode_elapsed;
            decode_elapsed.start();
            QTimer decode_poll;
            bool decode_timeout = false;
            QObject::connect(&decode_poll, &QTimer::timeout, &decode_loop, [&] {
                QApplication::processEvents();
                if (workspace.HasDecodedVisible() || decode_elapsed.elapsed() > 10000) {
                    decode_timeout = decode_elapsed.elapsed() > 10000;
                    decode_poll.stop();
                    decode_loop.quit();
                }
            });
            decode_poll.start(20);
            decode_loop.exec(QEventLoop::ExcludeUserInputEvents);
            const std::string probe = workspace.GalleryProbe().toStdString();
            line("virtual-decode", !decode_timeout && workspace.HasDecodedVisible() &&
                                      contains(probe, "virtual=1"),
                 "ThumbGrid 只请求可视行，首行在 worker 解码完成");
            line("viewer-range", contains(probe, "viewerMin=0.1") && contains(probe, "viewerMax=16.0"),
                 "ImageViewer 缩放边界保持 0.1–16×");
            line("workflow-input", workflow && contains(probe, "workflow=C:") &&
                                      contains(probe, "input.png"),
                 "右键同路径动作可把选中图片设为工作流输入");

            shine::Settings() = saved;
            content += "GALLERY-PROBE:\n" + probe + "\n";
            content += fails == 0 ? "[P05-S7] overall: PASS" : "[P05-S7] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s7Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

    // P05-S8 判定：同一图片被资产、分镜、参考图绑定引用；列表可跳转。
    if (const std::filesystem::path s8Dir = EnvironmentPath(L"SHINE_P05_S8"); !s8Dir.empty()) {
        const std::filesystem::path s8Out = EnvironmentPath(L"SHINE_P05_S8_OUT");
        QTimer::singleShot(600, &window, [s8Out = s8Out.empty() ? s8Dir / "p05_s8_probe.txt" : s8Out] {
            namespace fs = std::filesystem;
            std::string content;
            int fails = 0;
            const auto line = [&content, &fails](const std::string& name, bool pass,
                                                 const std::string& detail) {
                content += std::string("[") + (pass ? "PASS" : "FAIL") + "] " + name + " — " + detail +
                           "\n";
                if (!pass) ++fails;
                std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", name.c_str());
            };

            std::error_code ec;
            const fs::path root =
                fs::temp_directory_path() / ("shinetv-p05s8-" + shine::util::RandomHex(8));
            const fs::path shared_dir = root / "assets" / "shared";
            const fs::path shared = shared_dir / "角色参考.png";
            fs::create_directories(root / "db", ec);
shine::util::EnsureDir(shared_dir);
            QImage shared_image(128, 96, QImage::Format_RGB32);
            shared_image.fill(Qt::lightGray);
            bool fixture = shared_image.save(
                QString::fromStdString(shine::util::PathToUtf8(shared)), "PNG");

            std::int64_t entityId = 0;
            std::int64_t assetId = 0;
            std::int64_t shotId = 0;
            {
                shine::db::sqlite::Database db;
                auto opened = db.Open({.path = root / "db" / "novel.db"});
                if (!opened) {
                    fixture = false;
                } else if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
                    fixture = false;
                } else {
                    shine::novelcore::NovelGraph graph(db);
                    auto entity = graph.UpsertEntity({.kind = "person", .name = "引用角色"});
                    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
                    if (!entity || !volume) {
                        fixture = false;
                    } else {
                        entityId = *entity;
                        auto chapter = graph.UpsertChapter({.volume_id = *volume, .ord = 1,
                                                             .title = "第一章", .status = "done"});
                        if (!chapter) {
                            fixture = false;
                        } else {
                            auto scene = graph.UpsertScene({.chapter_id = *chapter, .ord = 1,
                                                            .title = "引用场景"});
                            if (!scene) {
                                fixture = false;
                            } else {
                                shine::novelcore::NovelVisual visual(db);
                                auto asset = visual.UpsertAsset({.entity_id = entityId,
                                                                  .kind = "character",
                                                                  .name = "引用角色",
                                                                  .sheet_rel_path =
                                                                      "assets/shared/角色参考.png",
                                                                  .status = "READY"});
                                if (!asset) {
                                    fixture = false;
                                } else {
                                    assetId = *asset;
                                    shine::novelcore::VisualArtifactRow artifact;
                                    artifact.asset_id = assetId;
                                    artifact.layer = "front";
                                    artifact.rel_path = "assets/shared/角色参考.png";
                                    artifact.status = "DONE";
                                    fixture = visual.UpsertArtifact(artifact).has_value();
                                    shine::novelcore::ShotRow shot;
                                    shot.scene_id = *scene;
                                    shot.ord = 1;
                                    shot.character_ids_json = "[" + std::to_string(entityId) + "]";
                                    shot.action = "走向镜头";
                                    shot.expression = "警觉";
                                    auto shot_saved = visual.UpsertShot(shot);
                                    if (!shot_saved) {
                                        fixture = false;
                                    } else {
                                        shotId = *shot_saved;
                                        const shine::AppSettings saved_image = shine::Settings();
                                        shine::Settings().imageBackend = "mock";
                                        shine::Settings().imageOutputRelDir = "visual/gen";
                                        shine::novelcore::ImageJobInput input;
                                        input.prompt = "Su Li reference shot";
                                        input.negative = "lowres";
                                        input.shot_id = shotId;
                                        input.width = 64;
                                        input.height = 64;
                                        auto image = shine::novelcore::RunImageJob(db, input);
                                        shine::Settings() = saved_image;
                                        if (!image) {
                                            fixture = false;
                                        } else if (auto update = db.Prepare(
                                                       "UPDATE generated_images SET rel_path=?1 WHERE id=?2")) {
                                            (void)update->BindText(1, "assets/shared/角色参考.png");
                                            (void)update->BindInt(2, image->id);
                                            fixture = update->Step().has_value();
                                            fs::remove(root / shine::util::PathFromUtf8(image->rel_path), ec);
                                        } else {
                                            fixture = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            shine::visual::ReferenceLibrary references(root);
            if (fixture) {
                fixture = references.Import(shared, entityId, assetId, {"镜头", "基准"})
                              .has_value();
            }
            line("fixture", fixture, "资产层 + 分镜任务 + 项目参考图绑定共享同一文件");

            const shine::AppSettings saved = shine::Settings();
            shine::Settings().galleryLocalDir = shine::util::PathToUtf8(shared_dir);
            shine::app::AssetWorkspace workspace;
            QString open_error;
            const bool opened = fixture && workspace.OpenBook(root / "db" / "novel.db", root,
                                                                  &open_error) &&
                                workspace.SelectEntity(entityId);
            workspace.SelectGlobalGallerySource(shine::gallery::SourceKind::Local);
            QEventLoop scan_loop;
            QElapsedTimer scan_elapsed;
            scan_elapsed.start();
            QTimer scan_poll;
            bool scan_timeout = false;
            QObject::connect(&scan_poll, &QTimer::timeout, &scan_loop, [&] {
                if ((!shine::gallery::State().scanning &&
                     shine::gallery::State().source == shine::gallery::SourceKind::Local) ||
                    scan_elapsed.elapsed() > 10000) {
                    scan_timeout = scan_elapsed.elapsed() > 10000;
                    scan_poll.stop();
                    scan_loop.quit();
                }
            });
            scan_poll.start(20);
            scan_loop.exec(QEventLoop::ExcludeUserInputEvents);

            const bool selected = opened && !scan_timeout && workspace.SelectFirstGlobalGallery();
            const QStringList labels = workspace.ReferenceUsageLabels();
            const QString joined = labels.join(QStringLiteral(" | "));
            int asset_index = -1;
            int shot_index = -1;
            for (int i = 0; i < labels.size(); ++i) {
                if (labels[i].startsWith(QStringLiteral("资产 #"))) {
                    asset_index = i;
                } else if (labels[i].startsWith(QStringLiteral("第 "))) {
                    shot_index = i;
                }
            }
            const bool asset_jump = asset_index >= 0 && workspace.ActivateReferenceUsage(asset_index);
            const bool shot_jump = shot_index >= 0 && workspace.ActivateReferenceUsage(shot_index);
            line("usage-list", selected && labels.size() == 3 && joined.contains(QStringLiteral("项目参考图")),
                 QStringLiteral("被引用列表：%1").arg(joined).toStdString());
            line("usage-jump", asset_jump && shot_jump,
                 "资产引用切到实体详情，分镜引用打开分镜抽屉");

            shine::Settings() = saved;
            content += "REFERENCE-USAGES:\n" + joined.toStdString() + "\n";
            content += fails == 0 ? "[P05-S8] overall: PASS" : "[P05-S8] overall: FAIL";
            content += '\n';
            (void)shine::util::WriteFileBytes(s8Out, content);
            std::fflush(nullptr);
            std::_Exit(fails == 0 ? 0 : 1);
        });
    }

}

} // namespace shine::app::checks
