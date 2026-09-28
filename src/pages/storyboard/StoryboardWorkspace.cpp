#include "pages/storyboard/StoryboardWorkspace.h"
#include "pages/storyboard/StoryboardTimeline.h"
#include "pages/storyboard/ShotTableView.h"
#include "pages/storyboard/ShotDetailView.h"
#include "pages/storyboard/ContinuityView.h"
#include "pages/storyboard/GenShotBridgeView.h"

#include "pages/novel/WorldBoardShared.h"
#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "widget/data/Table.h"
#include "widget/data/Flow.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Feedback.h"
#include "widget/controls/Controls.h"
#include "widget/controls/WidgetCommon.h"
#include "novel/NovelVisual.h"
#include "novel/NovelGraph.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "novel/NovelContinuity.h"
#include "novel/NovelVisualStages.h"
#include "novel/NovelStoryboard.h"
#include "novel/NovelPromptGen.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItem>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

#include <QTreeView>

#include <algorithm>
#include <array>
#include <unordered_map>
#include <numeric>

namespace shine::app {

StoryboardWorkspace::StoryboardWorkspace(QWidget* parent) : QWidget(parent) {
    BuildUi();
    CloseBook();
}

StoryboardWorkspace::~StoryboardWorkspace() = default;

void StoryboardWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[2],
                              theme::space::kSteps[3], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[1]);

    status_ = new QLabel(QStringLiteral("分镜 · 未打开书库"), this);
    widgets::SetKind(status_, "statetitle");
    widgets::SetSemibold(status_, true);
    outer->addWidget(status_);

    auto* stage_bar = new QWidget(this);
    auto* stage_layout = new QHBoxLayout(stage_bar);
    stage_layout->setContentsMargins(0, 0, 0, 0);
    run_stages_ = new widgets::Button(QStringLiteral("运行 V1–V8"),
                                      widgets::Button::Variant::Primary,
                                      widgets::Button::Size::Sm, stage_bar);
    auto* persist = new widgets::Button(QStringLiteral("V9/V10 落库"),
                                       widgets::Button::Variant::Secondary,
                                       widgets::Button::Size::Sm, stage_bar);
    persist->setToolTip(QStringLiteral("生成叙事分镜并写入 shots / prompt_artifacts"));
    stage_layout->addWidget(persist);
    auto* prompt_only = new widgets::Button(QStringLiteral("V10 重生成"),
                                            widgets::Button::Variant::Ghost,
                                            widgets::Button::Size::Sm, stage_bar);
    prompt_only->setToolTip(QStringLiteral("按当前输入状态重新生成并递增 Prompt 版本"));
    stage_layout->addWidget(prompt_only);
    persistence_status_label_ = new QLabel(QStringLiteral("V9/V10 未运行"), stage_bar);
    persistence_status_label_->setWordWrap(true);
    stage_layout->addWidget(persistence_status_label_, 1);
    run_stages_->setToolTip(QStringLiteral("按章节运行 V1–V7 阶段并执行 V8 连续性校验"));
    stage_layout->addWidget(run_stages_);
    stage_flow_ = new data::StageFlow(stage_bar);
    stage_flow_->setMinimumHeight(92);
    stage_layout->addWidget(stage_flow_, 1);
    outer->addWidget(stage_bar);
    artifact_view_ = new QPlainTextEdit(this);
    artifact_view_->setReadOnly(true);
    artifact_view_->hide();
    outer->addWidget(artifact_view_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    tree_ = new data::DataTree(splitter);
    tree_->SetColumns({QStringLiteral("章节 / 场景"), QStringLiteral("目标"), QStringLiteral("状态")});
    splitter->addWidget(tree_);

    detail_ = new QWidget(splitter);
    auto* detail_layout = new QVBoxLayout(detail_);
    detail_layout->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                                      theme::space::kSteps[2], theme::space::kSteps[2]);
    auto* empty = new widgets::EmptyState(
        QStringLiteral("▤"), QStringLiteral("还没有已提交场景"),
        QStringLiteral("先在小说工作区提交 Scene；分镜只消费已提交章节，不读取草稿场景。"),
        QString{}, detail_);
    detail_layout->addWidget(empty);
    detail_layout->addStretch();
    splitter->addWidget(detail_);
    splitter->setSizes({420, 900});
    outer->addWidget(splitter, 1);
    timeline_ = new StoryboardTimeline(this);
    timeline_->SetOnReorder([this](const std::vector<novelcore::RowId>& ids) {
        (void)ReorderTimeline(ids);
    });
    shot_table_ = new ShotTableView(this);
    shot_table_->SetOnUpdate([this](const ShotTableView::ShotEdit& edit) {
        (void)UpdateShot(edit.id, edit.action.toStdString(), edit.mood.toStdString(),
                         edit.duration_note.toStdString());
    });
    shot_table_->SetOnBatchMood([this](const std::vector<novelcore::RowId>& ids, const QString& mood) {
        (void)ApplyMoodBatch(ids, mood);
    });
    outer->addWidget(shot_table_);
    outer->addWidget(timeline_);
    shot_detail_ = new ShotDetailView(this);
    shot_detail_->SetTimelineHandler([this](std::string json) {
        (void)SaveSelectedTimeline(json);
    });
    outer->addWidget(shot_detail_);
    continuity_ = new ContinuityView(this);
    outer->addWidget(continuity_);
    gen_shot_ = new GenShotBridgeView(this);
    outer->addWidget(gen_shot_);

    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        SelectScene(index.data(Qt::UserRole).toLongLong());
    });
    connect(persist, &widgets::Button::clicked, this, &StoryboardWorkspace::RunStoryboardPersistence);
    connect(prompt_only, &widgets::Button::clicked, this, &StoryboardWorkspace::RunPromptPersistence);
    connect(run_stages_, &widgets::Button::clicked, this, &StoryboardWorkspace::RunStagePipeline);
    UpdateStageGraph();
}

std::int64_t StoryboardWorkspace::SelectedChapterId() const {
    for (const ChapterScenes& chapter : chapters_) {
        for (const auto& scene : chapter.scenes) {
            if (scene.id == selected_scene_) {
                return chapter.chapter.id;
            }
        }
    }
    return 0;
}

void StoryboardWorkspace::UpdateStageGraph() {
    if (stage_flow_ == nullptr) {
        return;
    }
    std::vector<data::StageFlow::Node> nodes;
    std::vector<data::StageFlow::Link> links;
    for (int i = 0; i < 8; ++i) {
        const QString code = QStringLiteral("V%1").arg(i + 1);
        data::StageFlow::Node node;
        node.id = code;
        node.title = i == 7 ? QStringLiteral("连续性") : code;
        node.state = data::StageFlow::NodeState::Todo;
        if (i < static_cast<int>(stage_states_.size())) {
            const QString& status = stage_states_[static_cast<std::size_t>(i)].status;
            if (status == QStringLiteral("done") || status == QStringLiteral("reused")) {
                node.state = data::StageFlow::NodeState::Done;
            } else if (status == QStringLiteral("failed")) {
                node.state = data::StageFlow::NodeState::Failed;
            } else if (status == QStringLiteral("running")) {
                node.state = data::StageFlow::NodeState::Running;
            }
        }
        nodes.push_back(std::move(node));
        if (i > 0) {
            links.push_back({QStringLiteral("V%1").arg(i), QStringLiteral("V%1").arg(i + 1)});
        }
    }
    stage_flow_->SetGraph(std::move(nodes), std::move(links));
}

void StoryboardWorkspace::ShowLatestArtifact() {
    if (artifact_view_ == nullptr) {
        return;
    }
    for (auto it = stage_states_.rbegin(); it != stage_states_.rend(); ++it) {
        if (it->artifact.isEmpty()) {
            continue;
        }
        const auto bytes = util::ReadFileBytes(util::PathFromUtf8(it->artifact.toStdString()));
        if (!bytes) {
            continue;
        }
        artifact_view_->setPlainText(QString::fromStdString(*bytes));
        artifact_view_->show();
        return;
    }
    artifact_view_->setPlainText(QStringLiteral("当前没有阶段产物。"));
    artifact_view_->show();
}

bool StoryboardWorkspace::RunStagePipeline() {
    const std::int64_t chapterId = SelectedChapterId();
    if (db_ == nullptr || chapterId <= 0 || stage_running_) {
        return false;
    }
    if (!llm_) {
        error_ = QStringLiteral("未配置 LLM；请先在设置或模型面板配置生成回调。");
        return false;
    }
    stage_running_ = true;
    run_stages_->SetLoading(true);
    run_stages_->setEnabled(false);
    const auto dbPath = dbPath_;
    const auto projectDir = projectDir_;
    const auto call = llm_;
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, dbPath, projectDir, chapterId, call] {
        std::vector<StageState> states;
        for (int i = 0; i < 7; ++i) {
            states.push_back({QStringLiteral("V%1").arg(i + 1), QStringLiteral("running"),
                              QStringLiteral("运行中"), QString{}});
        }
        states.push_back({QStringLiteral("V8"), QStringLiteral("running"), QStringLiteral("等待 V1–V7"),
                          QString{}});
        db::sqlite::Database workerDb;
        if (auto opened = workerDb.Open({.path = dbPath, .readOnly = false, .create = false}); !opened) {
            for (auto& state : states) {
                state.status = QStringLiteral("failed");
                state.detail = QString::fromStdString(opened.error().message);
            }
        } else {
            auto stages = agent::RunAllVisualStages(workerDb, call, chapterId,
                                                    util::PathToUtf8(projectDir));
            const std::array<const char*, 7> names{
                "v01_scene_breakdown.json", "v02_director_intent.json", "v03_performance.json",
                "v04_spatial.json", "v05_camera.json", "v06_timeline.json", "v07_audio.json"};
            const int chapterOrd = [&] {
                novelcore::NovelGraph graph(workerDb);
                auto chapter = graph.GetChapter(chapterId);
                return chapter ? chapter->ord : 0;
            }();
            for (int i = 0; i < 7; ++i) {
                states[static_cast<std::size_t>(i)].status =
                    stages ? QStringLiteral("done") : QStringLiteral("failed");
                states[static_cast<std::size_t>(i)].detail =
                    stages ? QString::fromStdString(stages->detail)
                           : QString::fromStdString(stages.error().message);
                std::string chapterDir = "ch";
                if (chapterOrd < 100) chapterDir += "0";
                if (chapterOrd < 10) chapterDir += "0";
                chapterDir += std::to_string(chapterOrd);
                states[static_cast<std::size_t>(i)].artifact =
                    QString::fromStdString(util::PathToUtf8(
                        projectDir / "work" / chapterDir / names[static_cast<std::size_t>(i)]));
            }
            auto continuity = novelcore::RunContinuityChecks(workerDb, chapterId,
                                                               util::PathToUtf8(projectDir));
            states[7].status = QStringLiteral("done");
            states[7].detail = QString::fromStdString(continuity.Describe());
            states[7].artifact = QString::fromStdString(continuity.report_path);
        }
        async::PostToUi([guard, states = std::move(states)]() mutable {
            if (!guard) {
                return;
            }
            guard->stage_states_ = std::move(states);
            guard->stage_running_ = false;
            guard->run_stages_->SetLoading(false);
            guard->run_stages_->setEnabled(true);
            guard->UpdateStageGraph();
            guard->ShowLatestArtifact();
        });
    });
    return true;
}

bool StoryboardWorkspace::RunStoryboardPersistence() {
    const std::int64_t chapterId = SelectedChapterId();
    if (db_ == nullptr || chapterId <= 0 || persistence_running_ || !llm_) {
        return false;
    }
    persistence_running_ = true;
    persistence_status_ = QStringLiteral("V9/V10 落库运行中…");
    const auto dbPath = dbPath_;
    const auto projectDir = projectDir_;
    const auto call = llm_;
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, dbPath, projectDir, chapterId, call] {
        bool v9_ok = false;
        bool v10_ok = false;
        int shots = 0;
        int prompts = 0;
        QString status;
        db::sqlite::Database workerDb;
        if (auto opened = workerDb.Open({.path = dbPath, .readOnly = false, .create = false}); !opened) {
            status = QString::fromStdString(opened.error().message);
        } else {
            auto storyboard = agent::GenerateStoryboard(
                workerDb, call, agent::StoryboardRequest{.chapter_id = chapterId,
                                                          .project_dir = projectDir});
            if (!storyboard) {
                status = QString::fromStdString(storyboard.error().message);
            } else {
                v9_ok = storyboard->ok;
                shots = storyboard->shots_written;
                if (!v9_ok) {
                    status = QString::fromStdString(storyboard->error);
                } else {
                    const auto prompt = novelcore::GeneratePromptArtifacts(
                        workerDb, chapterId, util::PathToUtf8(projectDir));
                    v10_ok = prompt.ok;
                    if (!v10_ok) {
                        status = QString::fromStdString(prompt.error);
                    } else {
                        status = QStringLiteral("V9 已写 shots；V10 已写 prompt_artifacts（%1 镜，%2 条账）")
                                     .arg(shots)
                                     .arg(prompt.artifacts_written);
                    }
                    novelcore::NovelVisual visual(workerDb);
                    if (auto list = visual.ListPromptArtifacts(chapterId); list) {
                        prompts = static_cast<int>(list->size());
                    }
                }
            }
        }
        async::PostToUi([guard, v9_ok, v10_ok, shots, prompts, status] {
            if (!guard) return;
            guard->persistence_running_ = false;
            guard->persistence_ran_ = true;
            guard->persistence_v9_ok_ = v9_ok;
            guard->persistence_v10_ok_ = v10_ok;
            guard->persistence_shots_ = shots;
            guard->persistence_prompts_ = prompts;
            guard->persistence_status_ = status;
            if (guard->persistence_status_label_ != nullptr) guard->persistence_status_label_->setText(status);
            guard->LoadTimeline();
        });
    });
    return true;
}

bool StoryboardWorkspace::RunPromptPersistence() {
    const std::int64_t chapterId = SelectedChapterId();
    if (db_ == nullptr || chapterId <= 0 || persistence_running_) {
        return false;
    }
    persistence_running_ = true;
    persistence_status_ = QStringLiteral("V10 重生成运行中…");
    const auto dbPath = dbPath_;
    const auto projectDir = projectDir_;
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, dbPath, projectDir, chapterId] {
        bool ok = false;
        int prompts = 0;
        QString status;
        db::sqlite::Database workerDb;
        if (auto opened = workerDb.Open({.path = dbPath, .readOnly = false, .create = false}); !opened) {
            status = QString::fromStdString(opened.error().message);
        } else {
            const auto result = novelcore::GeneratePromptArtifacts(
                workerDb, chapterId, util::PathToUtf8(projectDir));
            ok = result.ok;
            status = ok ? QStringLiteral("V10 完成：写入 %1 条 prompt_artifacts")
                              .arg(result.artifacts_written)
                        : QString::fromStdString(result.error);
            novelcore::NovelVisual visual(workerDb);
            if (auto list = visual.ListPromptArtifacts(chapterId); list) {
                prompts = static_cast<int>(list->size());
            }
        }
        async::PostToUi([guard, ok, prompts, status] {
            if (!guard) return;
            guard->persistence_running_ = false;
            guard->persistence_ran_ = true;
            guard->persistence_v10_ok_ = ok;
            guard->persistence_prompts_ = prompts;
            guard->persistence_status_ = status;
            if (guard->persistence_status_label_ != nullptr) guard->persistence_status_label_->setText(status);
        });
    });
    return true;
}

QString StoryboardWorkspace::PersistenceProbe() const {
    return QStringLiteral("ran=%1; running=%2; v9=%3; v10=%4; shots=%5; prompts=%6; status=%7")
        .arg(persistence_ran_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(persistence_running_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(persistence_v9_ok_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(persistence_v10_ok_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(persistence_shots_)
        .arg(persistence_prompts_)
        .arg(persistence_status_);
}

QString StoryboardWorkspace::StageProbe() const {
    QString codes;
    for (const auto& state : stage_states_) {
        if (!codes.isEmpty()) {
            codes += QLatin1Char('>');
        }
        codes += state.code + QLatin1Char('=') + state.status;
    }
    return QStringLiteral("running=%1; states=%2; flow=%3")
        .arg(stage_running_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(static_cast<int>(stage_states_.size()))
        .arg(codes.isEmpty() ? QStringLiteral("none") : codes);
}

bool StoryboardWorkspace::OpenBook(const std::filesystem::path& dbPath,
                                    const std::filesystem::path& projectDir, QString* error) {
    const auto nextDb = dbPath;
    const auto nextDir = projectDir;
    CloseBook();
    dbPath_ = nextDb;
    projectDir_ = nextDir;
    db_ = std::make_unique<shine::db::sqlite::Database>();
    if (auto opened = db_->Open({.path = dbPath_, .readOnly = true, .create = false}); !opened) {
        error_ = QStringLiteral("打不开小说库：%1")
                     .arg(QString::fromStdString(opened.error().message));
        db_.reset();
        if (error != nullptr) {
            *error = error_;
        }
        RebuildTree();
        UpdateSelection();
        return false;
    }
    if (!ReloadScenes()) {
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    RebuildTree();
    UpdateSelection();
    return true;
}

void StoryboardWorkspace::CloseBook() noexcept {
    db_.reset();
    dbPath_.clear();
    projectDir_.clear();
    chapters_.clear();
    selected_scene_ = 0;
    error_.clear();
    selected_chapter_ = 0;
    current_shots_.clear();
    if (timeline_ != nullptr) {
        timeline_->SetScene({}, {});
    }
    if (continuity_ != nullptr) continuity_->SetContext({}, {}, 0);
    if (gen_shot_ != nullptr) gen_shot_->SetContext({}, {}, 0);
    stage_states_.clear();
    artifact_view_->clear();
    artifact_view_->hide();
    stage_running_ = false;
    UpdateStageGraph();
    RebuildTree();
    UpdateSelection();
}

bool StoryboardWorkspace::ReloadScenes() {
    chapters_.clear();
    if (db_ == nullptr) {
        error_ = QStringLiteral("尚未打开书库。");
        return false;
    }
    shine::novelcore::NovelGraph graph(*db_);
    auto chapters = graph.ListChapters(2000);
    if (!chapters) {
        error_ = QStringLiteral("读取章节失败：%1")
                     .arg(QString::fromStdString(chapters.error().message));
        return false;
    }
    for (const auto& chapter : *chapters) {
        if (chapter.status != "done") {
            continue;
        }
        auto scenes = graph.ListScenes(chapter.id);
        if (!scenes) {
            error_ = QStringLiteral("读取场景失败：%1")
                         .arg(QString::fromStdString(scenes.error().message));
            return false;
        }
        if (scenes->empty()) {
            continue;
        }
        chapters_.push_back({chapter, *scenes});
    }
    error_.clear();
    status_->setText(QStringLiteral("分镜 · 已提交场景 %1 个")
                         .arg(static_cast<int>(std::accumulate(
                             chapters_.begin(), chapters_.end(), 0,
                             [](int sum, const ChapterScenes& chapter) {
                                 return sum + static_cast<int>(chapter.scenes.size());
                             }))));
    return true;
}

void StoryboardWorkspace::RebuildTree() {
    if (tree_ == nullptr) {
        return;
    }
    if (auto* model = qobject_cast<QStandardItemModel*>(tree_->model()); model != nullptr &&
        model->rowCount() > 0) {
        model->removeRows(0, model->rowCount());
    }
    for (const ChapterScenes& chapter : chapters_) {
        QStandardItem* chapterItem = tree_->AddTop({
            QStringLiteral("第 %1 章 · %2")
                .arg(chapter.chapter.ord)
                .arg(QString::fromStdString(chapter.chapter.title)),
            QStringLiteral("%1 场").arg(chapter.scenes.size()),
            QStringLiteral("已提交")}, false);
        if (chapterItem == nullptr) {
            continue;
        }
        for (const auto& scene : chapter.scenes) {
            auto* sceneItem = new QStandardItem(
                QStringLiteral("场景 %1 · %2")
                    .arg(scene.ord)
                    .arg(QString::fromStdString(scene.title)));
            sceneItem->setData(QVariant::fromValue<qlonglong>(scene.id), Qt::UserRole);
            sceneItem->setEditable(false);
            chapterItem->appendRow(sceneItem);
        }
        tree_->expand(chapterItem->index());
    }
}

bool StoryboardWorkspace::SelectScene(std::int64_t id) {
    for (const ChapterScenes& chapter : chapters_) {
        for (const auto& scene : chapter.scenes) {
            if (scene.id == id) {
                selected_scene_ = id;
                UpdateSelection();
                return true;
            }
        }
    }
    if (id == 0) {
        selected_scene_ = 0;
        UpdateSelection();
        return true;
    }
    return false;
}

void StoryboardWorkspace::UpdateSelection() {
    if (detail_ == nullptr) {
        return;
    }
    LoadTimeline();
    auto* layout = qobject_cast<QVBoxLayout*>(detail_->layout());
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    if (selected_scene_ == 0) {
        layout->addWidget(new widgets::EmptyState(
            QStringLiteral("▤"), QStringLiteral("还没有已提交场景"),
            QStringLiteral("先在小说工作区提交 Scene；分镜只消费已提交章节，不读取草稿场景。"),
            QString{}, detail_));
        layout->addStretch();
        return;
    }
    for (const ChapterScenes& chapter : chapters_) {
        for (const auto& scene : chapter.scenes) {
            if (scene.id != selected_scene_) {
                continue;
            }
            auto* title = new QLabel(QStringLiteral("第 %1 章 · 场景 %2 · %3")
                                         .arg(chapter.chapter.ord)
                                         .arg(scene.ord)
                                         .arg(QString::fromStdString(scene.title)),
                                     detail_);
            widgets::SetKind(title, "statetitle");
            widgets::SetSemibold(title, true);
            layout->addWidget(title);
            auto* summary = new QLabel(
                QStringLiteral("目标：%1\n冲突：%2\n情绪：%3\n环境：%4")
                    .arg(QString::fromStdString(scene.goal),
                         QString::fromStdString(scene.conflict),
                         QString::fromStdString(scene.emotion),
                         QString::fromStdString(scene.hook)),
                detail_);
            summary->setWordWrap(true);
            widgets::SetKind(summary, "statedetail");
            layout->addWidget(summary);
            layout->addStretch();
            return;
        }
    }
}

void StoryboardWorkspace::LoadTimeline() {
    current_shots_.clear();
    selected_chapter_ = SelectedChapterId();
    if (timeline_ != nullptr) {
        timeline_->SetScene({}, {});
    }
    if (shot_table_ != nullptr) {
        shot_table_->SetShots({});
    }
    if (shot_detail_ != nullptr) {
        shot_detail_->SetShot({});
    }
    if (continuity_ != nullptr) continuity_->SetContext({}, {}, 0);
    if (gen_shot_ != nullptr) gen_shot_->SetContext({}, {}, 0);
    if (db_ == nullptr || selected_scene_ == 0 || selected_chapter_ == 0) {
        return;
    }
    novelcore::NovelVisual visual(*db_);
    auto shots = visual.ListShotsByChapter(selected_chapter_);
    if (!shots) {
        return;
    }
    current_shots_ = *shots;
    novelcore::SceneRow scene;
    for (const ChapterScenes& chapter : chapters_) {
        if (chapter.chapter.id != selected_chapter_) {
            continue;
        }
        for (const auto& candidate : chapter.scenes) {
            if (candidate.id == selected_scene_) {
                scene = candidate;
            }
        }
    }
    if (timeline_ != nullptr) {
        timeline_->SetScene(scene, current_shots_);
    }
    if (shot_table_ != nullptr) {
        shot_table_->SetShots(current_shots_);
    }
    if (shot_detail_ != nullptr && !current_shots_.empty()) {
        shot_detail_->SetShot(current_shots_.front());
    }
    if (continuity_ != nullptr) {
        continuity_->SetContext(dbPath_, projectDir_, selected_chapter_);
    }
    if (gen_shot_ != nullptr) {
        gen_shot_->SetContext(dbPath_, projectDir_, selected_chapter_);
    }
}

bool StoryboardWorkspace::ReorderTimeline(const std::vector<novelcore::RowId>& orderedIds) {
    if (db_ == nullptr || orderedIds.empty() || orderedIds.size() != current_shots_.size() ||
        timeline_busy_) {
        return false;
    }
    std::unordered_map<novelcore::RowId, novelcore::ShotRow> by_id;
    for (const auto& shot : current_shots_) by_id.emplace(shot.id, shot);
    for (novelcore::RowId id : orderedIds) {
        if (!by_id.contains(id)) return false;
    }
    timeline_busy_ = true;
    const auto db_path = dbPath_;
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, db_path, orderedIds, by_id] {
        shine::db::sqlite::Database worker_db;
        QString error;
        if (auto opened = worker_db.Open({.path = db_path, .readOnly = false, .create = false}); !opened) {
            error = QString::fromStdString(opened.error().message);
        } else if (auto begin = worker_db.Begin(); !begin) {
            error = QString::fromStdString(begin.error().message);
        } else {
            bool ok = true;
            shine::novelcore::NovelVisual visual(worker_db);
            for (std::size_t i = 0; i < orderedIds.size(); ++i) {
                auto shot = by_id.at(orderedIds[i]);
                shot.ord = static_cast<int>(i + 1);
                if (!visual.UpsertShot(shot)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                auto commit = worker_db.Commit();
                if (!commit) error = QString::fromStdString(commit.error().message);
            } else {
                (void)worker_db.Rollback();
                error = QStringLiteral("镜头顺序写库失败");
            }
        }
        async::PostToUi([guard, error] {
            if (!guard) return;
            guard->timeline_busy_ = false;
            if (!error.isEmpty()) guard->error_ = error;
            guard->LoadTimeline();
        });
    });
    return true;
}

bool StoryboardWorkspace::UpdateShot(std::int64_t id, std::string action, std::string mood,
                                     std::string duration_note) {
    if (db_ == nullptr || timeline_busy_) return false;
    const auto found = std::find_if(current_shots_.begin(), current_shots_.end(),
                                   [id](const auto& shot) { return shot.id == id; });
    if (found == current_shots_.end()) return false;
    auto updated = *found;
    updated.action = std::move(action);
    updated.mood = std::move(mood);
    updated.duration_note = std::move(duration_note);
    timeline_busy_ = true;
    const auto db_path = dbPath_;
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, db_path, updated] {
        shine::db::sqlite::Database worker_db;
        QString error;
        if (auto opened = worker_db.Open({.path = db_path, .readOnly = false, .create = false}); !opened) {
            error = QString::fromStdString(opened.error().message);
        } else {
            shine::novelcore::NovelVisual visual(worker_db);
            if (!visual.UpsertShot(updated)) error = QStringLiteral("镜头写库失败");
        }
        async::PostToUi([guard, error] {
            if (!guard) return;
            guard->timeline_busy_ = false;
            if (!error.isEmpty()) guard->error_ = error;
            guard->LoadTimeline();
        });
    });
    return true;
}

bool StoryboardWorkspace::ApplyMoodBatch(const std::vector<novelcore::RowId>& ids,
                                        const QString& mood) {
    if (db_ == nullptr || timeline_busy_ || ids.empty() || mood.isEmpty()) return false;
    timeline_busy_ = true;
    const auto db_path = dbPath_;
    const std::string mood_text = mood.toStdString();
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, db_path, ids, mood_text] {
        shine::db::sqlite::Database worker_db;
        QString error;
        if (auto opened = worker_db.Open({.path = db_path, .readOnly = false, .create = false}); !opened) {
            error = QString::fromStdString(opened.error().message);
        } else {
            shine::novelcore::NovelVisual visual(worker_db);
            for (const auto id : ids) {
                auto shot = visual.GetShot(id);
                if (!shot) continue;
                shot->mood = mood_text;
                if (!visual.UpsertShot(*shot)) {
                    error = QStringLiteral("批量情绪写库失败");
                    break;
                }
            }
        }
        async::PostToUi([guard, error] {
            if (!guard) return;
            guard->timeline_busy_ = false;
            if (!error.isEmpty()) guard->error_ = error;
            guard->LoadTimeline();
        });
    });
    return true;
}

bool StoryboardWorkspace::SaveSelectedTimeline(const std::string& timeline_json) {
    if (db_ == nullptr || current_shots_.empty() || timeline_busy_) return false;
    auto shot = current_shots_.front();
    shot.timeline_json = timeline_json;
    timeline_busy_ = true;
    const auto db_path = dbPath_;
    QPointer<StoryboardWorkspace> guard(this);
    async::RunOnWorker([guard, db_path, shot] {
        shine::db::sqlite::Database worker_db;
        QString error;
        if (auto opened = worker_db.Open({.path = db_path, .readOnly = false, .create = false}); !opened) {
            error = QString::fromStdString(opened.error().message);
        } else {
            shine::novelcore::NovelVisual visual(worker_db);
            if (!visual.UpsertShot(shot)) error = QStringLiteral("Beat 时间轴写库失败");
        }
        async::PostToUi([guard, error] {
            if (!guard) return;
            guard->timeline_busy_ = false;
            if (!error.isEmpty()) guard->error_ = error;
            guard->LoadTimeline();
        });
    });
    return true;
}

QString StoryboardWorkspace::ShotDetailProbe() const {
    return shot_detail_ == nullptr ? QStringLiteral("detail=unavailable")
                                   : shot_detail_->DetailProbe();
}


bool StoryboardWorkspace::RunContinuityCheck() {
    return continuity_ != nullptr && continuity_->RunCheck();
}
QString StoryboardWorkspace::ContinuityProbe() const {
    return continuity_ == nullptr ? QStringLiteral("continuity=unavailable")
                                  : continuity_->ContinuityProbe();
}

QString StoryboardWorkspace::ShotTableProbe() const {
    return shot_table_ == nullptr ? QStringLiteral("table=unavailable") : shot_table_->TableProbe();
}


bool StoryboardWorkspace::RunGenShotBridge() {
    return gen_shot_ != nullptr && gen_shot_->RunBridge();
}

QString StoryboardWorkspace::GenShotProbe() const {
    return gen_shot_ == nullptr ? QStringLiteral("gen_shot=unavailable") : gen_shot_->BridgeProbe();
}

QString StoryboardWorkspace::TimelineProbe() const {
    return QStringLiteral("busy=%1; scene=%2; chapter=%3; shots=%4; order=%5")
        .arg(timeline_busy_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(selected_scene_)
        .arg(selected_chapter_)
        .arg(static_cast<int>(current_shots_.size()))
        .arg([this] {
            QStringList ids;
            for (const auto& shot : current_shots_) ids.push_back(QString::number(shot.id));
            return ids.join(QLatin1Char('>'));
        }());
}

QString StoryboardWorkspace::SceneProbe() const {
    int scene_count = 0;
    for (const ChapterScenes& chapter : chapters_) {
        scene_count += static_cast<int>(chapter.scenes.size());
    }
    return QStringLiteral("open=%1; committed_chapters=%2; scenes=%3; selected=%4; error=%5")
        .arg(db_ != nullptr ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(static_cast<int>(chapters_.size()))
        .arg(scene_count)
        .arg(selected_scene_)
        .arg(error_.isEmpty() ? QStringLiteral("none") : error_);
}

} // namespace shine::app
