// P04-S5 章节流水线（T1–T17）—— 全链跑完 / 产物落盘 + _manifest.json / 断点续跑。
// 驱动走 `agent::NovelDirector::GenerateChapter`（真实管线，LLM 可 mock 注入）；阶段产物与
// 断点判定的单一权威是 `novelcore::NovelStageLedger`（P1 哈希一致跳过 / P2 不一致重跑 /
// P4 正文复用 / P5 缺失重跑）。节点图用 kit `data::StageFlow`，颜色零内联（check-layers rule 3）。
#include "ui/pages/novel/ChapterFlowView.h"
#include "ui/kit/theme/CssColor.h"

#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Flow.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "novel/NovelDirector.h"
#include "novel/NovelGraph.h"
#include "novel/NovelRunLoop.h"
#include "novel/NovelStageLedger.h"
#include "novel/NovelTypes.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Time.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using shine::agent::Phase;

struct StageDef {
    const char* code;
    const char* stage;
    const char* name;
    bool merged;
};

// T1–T17 阶段目录（当前阶段定义见 src/pipeline/StageMachine.h；T2–T4/T6–T9 合并执行 → 产物并入
// 09_chapter_plan.json；具体状态机契约见 docs/10-modules/novel.md）。
constexpr StageDef kStageDefs[] = {
    {"T1", "CHAPTER_INIT", "章节初始化", false},     {"T2", "CHAPTER_GOAL", "本章目标", true},
    {"T3", "OUTLINE", "大纲", true},                 {"T4", "EVENT_PLAN", "事件计划", true},
    {"T5", "CONTEXT_ASSEMBLY", "上下文装配", false}, {"T6", "SCENE_PLAN", "场景计划", true},
    {"T7", "CHARACTER_PLAN", "人物计划", true},      {"T8", "ITEM_PLAN", "道具计划", true},
    {"T9", "FORESHADOW_PLAN", "伏笔计划", true},     {"T10", "SCENE_EVENT_ORDER", "场景事件序", false},
    {"T11", "NOVEL_WRITE", "正文写作", false},       {"T12", "CHAPTER_REVIEW", "章节评审", false},
    {"T13", "CHAPTER_REPAIR", "修复（条件）", false}, {"T14", "STATE_EXTRACT", "状态提取", false},
    {"T15", "STATE_VALIDATE", "状态校验", false},    {"T16", "COMMIT", "状态提交", false},
    {"T17", "NEXT_CHAPTER", "下一章备忘", false},
};

// 会落盘、进断点账的阶段（FindResumeIndex 的实际执行顺序；T13 条件产物不进）
constexpr std::string_view kArtifactStages[] = {
    "CONTEXT_ASSEMBLY", "SCENE_EVENT_ORDER", "CHAPTER_REVIEW", "STATE_EXTRACT", "STATE_VALIDATE",
};

QString Pad3(qint64 n) {
    return QStringLiteral("%1").arg(n, 3, 10, QLatin1Char('0'));
}



[[nodiscard]] shine::data::StageFlow::NodeState NodeStateOf(const QString& s) {
    using NS = shine::data::StageFlow::NodeState;
    if (s == QLatin1String("running")) {
        return NS::Running;
    }
    if (s == QLatin1String("done")) {
        return NS::Done;
    }
    if (s == QLatin1String("failed")) {
        return NS::Failed;
    }
    if (s == QLatin1String("skipped")) {
        return NS::Skipped;
    }
    return NS::Todo;
}

// Phase → T 节点（UI.md §2.3 的顺序）
[[nodiscard]] std::vector<QString> CodesOfPhase(Phase p) {
    switch (p) {
        case Phase::Analyze:
            return {QStringLiteral("T1")};
        case Phase::Retrieve:
            return {QStringLiteral("T5")};
        case Phase::Plan:
            return {QStringLiteral("T2"), QStringLiteral("T3"),  QStringLiteral("T4"),
                    QStringLiteral("T6"), QStringLiteral("T7"),  QStringLiteral("T8"),
                    QStringLiteral("T9"), QStringLiteral("T10")};
        case Phase::Write:
            return {QStringLiteral("T11")};
        case Phase::Review:
            return {QStringLiteral("T12")};
        case Phase::Revision:
            return {QStringLiteral("T13")};
        case Phase::Extract:
            return {QStringLiteral("T14"), QStringLiteral("T15")};
        default:
            return {};
    }
}

} // namespace

namespace shine::app {

ChapterFlowView::ChapterFlowView(QWidget* parent) : QWidget(parent) {
    for (const StageDef& d : kStageDefs) {
        StageRow row;
        row.code = QString::fromLatin1(d.code);
        row.stage = QString::fromLatin1(d.stage);
        row.name = QString::fromUtf8(d.name);
        row.merged = d.merged;
        state_.insert(row.code, QStringLiteral("todo"));
        stages_.push_back(std::move(row));
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);
    outer->addWidget(BuildHead());
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(BuildStageArea());
    split->addWidget(BuildArtifactArea());
    split->setSizes({780, 380});
    outer->addWidget(split, 1);
}

ChapterFlowView::~ChapterFlowView() {
    CloseDb();
}

QWidget* ChapterFlowView::BuildHead() {
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[2]);

    chapter_sel_ = new widgets::Select(true, false, head);
    chapter_sel_->setMinimumWidth(220);
    chapter_sel_->SetPlaceholder(QStringLiteral("选章节（书 → 卷 → 章）"));
    chapter_sel_->SetOnChanged([this] {
        const std::vector<QString> got = chapter_sel_->Checked();
        if (got.empty()) {
            return;
        }
        for (std::size_t i = 0; i < chapter_labels_.size(); ++i) {
            if (chapter_labels_[i] == got.front() && i < chapter_ids_.size()) {
                SelectChapter(chapter_ids_[i]);
                return;
            }
        }
    });

    run_btn_ = new widgets::Button(QStringLiteral("▶ 跑全链（T1–T17）"),
                                   widgets::Button::Variant::Primary, widgets::Button::Size::Sm,
                                   head);
    run_btn_->setToolTip(QStringLiteral("T1…T17 无人工干预跑完（T12 FAIL → 回 T13 修复 ≤3 轮）"));
    connect(run_btn_, &widgets::Button::clicked, this, [this] { GenerateCurrent(false); });

    resume_btn_ = new widgets::Button(QStringLiteral("↻ 断点续跑"),
                                      widgets::Button::Variant::Secondary,
                                      widgets::Button::Size::Sm, head);
    resume_btn_->setToolTip(QStringLiteral("哈希一致的阶段产物直接跳过，从断点接着跑（P1/P4/P5）"));
    connect(resume_btn_, &widgets::Button::clicked, this, [this] { GenerateCurrent(true); });

    gen_btn_ = new widgets::Button(QStringLiteral("▶ 生成本章"), widgets::Button::Variant::Ghost,
                                   widgets::Button::Size::Sm, head);
    gen_btn_->setToolTip(QStringLiteral("生成当前章 = 跑一次全链 T1–T17"));
    connect(gen_btn_, &widgets::Button::clicked, this, [this] { GenerateCurrent(false); });

    state_label_ = new QLabel(
        QStringLiteral("还没跑 —— 选章节后点「▶ 跑全链」（T1–T17 无人工干预）"), head);
    state_label_->setWordWrap(true);
    widgets::SetTextColor(state_label_, theme::Current().textSecondary);

    hl->addWidget(chapter_sel_);
    hl->addWidget(run_btn_);
    hl->addWidget(resume_btn_);
    hl->addWidget(gen_btn_);
    hl->addWidget(state_label_, 1);
    return head;
}

QWidget* ChapterFlowView::BuildStageArea() {
    auto* area = new QWidget(this);
    auto* vl = new QVBoxLayout(area);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(theme::space::kSteps[1]);

    // —— StageFlow 节点图（17 节点横排；T13 是 T12 的修复支线）——
    flow_scroll_ = new QScrollArea(area);
    flow_scroll_->setFrameShape(QFrame::NoFrame);
    flow_scroll_->setWidgetResizable(false);
    flow_ = new data::StageFlow(flow_scroll_);
    {
        std::vector<data::StageFlow::Node> nodes;
        for (const StageRow& s : stages_) {
            data::StageFlow::Node n;
            n.id = s.code;
            n.title = s.code + QStringLiteral(" ") + s.name;
            n.state = data::StageFlow::NodeState::Todo;
            if (s.code == QLatin1String("T13")) {
                n.branchOf = QStringLiteral("T12"); // UI.md §2.3：T12 FAIL → 回 T13 修复
            }
            nodes.push_back(std::move(n));
        }
        std::vector<data::StageFlow::Link> links;
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            if (nodes[i].branchOf.isEmpty()) {
                links.push_back({nodes[i - 1].id, nodes[i].id});
            }
        }
        links.push_back({QStringLiteral("T12"), QStringLiteral("T13")});
        links.push_back({QStringLiteral("T13"), QStringLiteral("T14")});
        flow_->SetGraph(std::move(nodes), std::move(links));
    }
    flow_->SetOnPick([this](const QString& id) { ShowStage(id); });
    flow_scroll_->setWidget(flow_);
    vl->addWidget(flow_scroll_, 2);

    // —— 逐阶段账（与探针同源：状态 / 产物 / 耗时）——
    stage_table_ = new data::DataTable(QStringLiteral("p04.chapterflow.stages"), area);
    stage_table_->SetColumns({{QStringLiteral("code"), QStringLiteral("阶段")},
                              {QStringLiteral("stage"), QStringLiteral("代码")},
                              {QStringLiteral("file"), QStringLiteral("产物")},
                              {QStringLiteral("state"), QStringLiteral("状态")},
                              {QStringLiteral("ms"), QStringLiteral("耗时 ms")}});
    stage_table_->SetActionColumn(QStringLiteral("看产物"), [this](int row) {
        if (row >= 0 && row < static_cast<int>(stages_.size())) {
            ShowStage(stages_[static_cast<std::size_t>(row)].code);
        }
    });
    RebuildStageTable();
    vl->addWidget(stage_table_, 3);
    return area;
}

QWidget* ChapterFlowView::BuildArtifactArea() {
    auto* area = new QWidget(this);
    auto* vl = new QVBoxLayout(area);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(theme::space::kSteps[1]);

    artifact_sel_ = new widgets::Select(true, false, area);
    artifact_sel_->SetPlaceholder(QStringLiteral("选阶段看产物（T1–T17）"));
    artifact_sel_->SetOnChanged([this] {
        const std::vector<QString> got = artifact_sel_->Checked();
        if (!got.empty()) {
            ShowStage(got.front().section(QLatin1Char(' '), 0, 0));
        }
    });
    {
        std::vector<widgets::Select::Item> items;
        for (const StageRow& s : stages_) {
            widgets::Select::Item it;
            it.text = s.code + QStringLiteral(" ") + s.name;
            it.checked = false;
            items.push_back(std::move(it));
        }
        artifact_sel_->SetItems(std::move(items));
    }

    artifact_stack_ = new QStackedWidget(area);
    artifact_empty_ = new widgets::EmptyState(
        QStringLiteral("📄"), QStringLiteral("还没看产物"),
        QStringLiteral("点左边流水线的阶段节点，或在上面的下拉选阶段 —— 产物 JSON 原文显示在这里。"),
        QStringLiteral("看 T10 场景事件序"), artifact_stack_);
    artifact_empty_->SetOnAction([this] { ShowStage(QStringLiteral("T10")); });
    artifact_tree_ = new data::JsonTree(artifact_stack_);
    artifact_stack_->addWidget(artifact_empty_);
    artifact_stack_->addWidget(artifact_tree_);
    artifact_stack_->setCurrentWidget(artifact_empty_);

    artifact_note_ = new QLabel(QStringLiteral("选中阶段后这里显示产物出处、状态与耗时。"), area);
    artifact_note_->setWordWrap(true);
    widgets::SetTextColor(artifact_note_, theme::Current().textSecondary);

    vl->addWidget(artifact_sel_);
    vl->addWidget(artifact_stack_, 1);
    vl->addWidget(artifact_note_);
    return area;
}

// ————————————————————————————————————————————— 数据接入

void ChapterFlowView::LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel) {
    CloseDb();
    ref_store_ = std::make_unique<project::ProjectRef>(ref);
    last_novel_ = lastNovel;
    book_title_.clear();
    open_error_.clear();

    // 一部一库：lastNovel = 系列书名；空 = 默认书（isDefault）
    const std::vector<project::BookRef> books = project::ListBooks(ref);
    const project::BookRef* pick = nullptr;
    for (const project::BookRef& b : books) {
        if (!lastNovel.empty() && b.title == lastNovel) {
            pick = &b;
            break;
        }
    }
    if (pick == nullptr) {
        for (const project::BookRef& b : books) {
            if (b.isDefault) {
                pick = &b;
                break;
            }
        }
    }
    if (pick == nullptr && !books.empty()) {
        pick = &books.front();
    }
    if (pick == nullptr) {
        open_error_ = QStringLiteral("这个工程还没有书库 —— 出路：先在[设定]新建一本书再跑流水线。");
        Ui([this] {
            state_label_->setText(open_error_);
            widgets::SetTextColor(state_label_, theme::Current().statusWarn);
        });
        return;
    }
    book_root_ = pick->rootDir;
    book_work_ = pick->workDir;
    book_db_path_ = pick->dbPath;
    book_title_ = QString::fromStdString(pick->title);

    db_ = std::make_unique<db::sqlite::Database>();
    if (auto r = db_->Open({.path = book_db_path_}); !r) {
        open_error_ = QStringLiteral("书库打不开（%1）—— 出路：确认 %2 可写后重试。")
                          .arg(QString::fromStdString(r.error().message),
                               QString::fromStdString(util::PathToUtf8(book_db_path_)));
        db_.reset();
        Ui([this] {
            state_label_->setText(open_error_);
            widgets::SetTextColor(state_label_, theme::Current().statusDanger);
        });
        return;
    }

    RebuildChapters();
    if (!chapter_ids_.empty()) {
        SelectChapter(chapter_ids_.front());
    } else {
        RefreshFlowStates();
        Ui([this] {
            state_label_->setText(
                QStringLiteral("《%1》还没有章节 —— 出路：先跑[初始化]建骨架，或手建第一章。")
                    .arg(book_title_));
            widgets::SetTextColor(state_label_, theme::Current().textSecondary);
        });
    }
}

void ChapterFlowView::CloseDb() noexcept {
    db_.reset();
    ref_store_.reset();
}

void ChapterFlowView::SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream) {
    call_ = std::move(call);
    stream_ = std::move(stream);
}

void ChapterFlowView::SelectChapter(qint64 chapterId) {
    chapter_id_ = chapterId;
    chapter_ord_ = 0;
    chapter_title_.clear();
    has_run_ = false;
    last_run_ = RunSummary{};
    if (db_ && chapterId > 0) {
        novelcore::NovelGraph g(*db_);
        if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapterId)); ch) {
            chapter_ord_ = ch->ord;
            chapter_title_ = QString::fromStdString(ch->title);
        }
    }
    RefreshFlowStates();
    Ui([this] {
        state_label_->setText(
            QStringLiteral("第 %1 章《%2》—— 点「▶ 跑全链（T1–T17）」开跑。产物在 %3。")
                .arg(chapter_ord_)
                .arg(chapter_title_, QStringLiteral("work/ch%1/").arg(Pad3(chapter_ord_))));
        widgets::SetTextColor(state_label_, theme::Current().textSecondary);
    });
}

// ————————————————————————————————————————————— 布局辅助

void ChapterFlowView::RebuildChapters() {
    chapter_ids_.clear();
    chapter_labels_.clear();
    std::vector<widgets::Select::Item> items;
    if (db_) {
        novelcore::NovelGraph g(*db_);
        if (auto list = g.ListChapters(2000); list) {
            for (const novelcore::ChapterRow& c : *list) {
                chapter_ids_.push_back(static_cast<qint64>(c.id));
                chapter_labels_.push_back(
                    QStringLiteral("第 %1 章 · %2").arg(c.ord).arg(QString::fromStdString(c.title)));
                widgets::Select::Item it;
                it.text = chapter_labels_.back();
                it.checked = static_cast<qint64>(c.id) == chapter_id_;
                items.push_back(std::move(it));
            }
        }
    }
    chapter_sel_->SetItems(std::move(items));
}

void ChapterFlowView::RebuildStageTable() {
    std::vector<std::vector<QString>> rows;
    for (const StageRow& s : stages_) {
        QString fileText;
        if (s.merged) {
            fileText = QStringLiteral("merged:09_chapter_plan.json");
        } else {
            const std::string_view f = novelcore::StageFileName(s.stage.toStdString());
            fileText = f.empty() ? QStringLiteral("-")
                                 : QString::fromLatin1(f.data(), static_cast<int>(f.size()));
            if (s.code == QLatin1String("T11")) {
                fileText = QStringLiteral("chapters.body");
            }
        }
        rows.push_back({s.code, s.stage, fileText, StateNameOf(s.code),
                        QString::number(stage_ms_.value(s.code, 0))});
    }
    stage_table_->SetRows(std::move(rows));
}

void ChapterFlowView::RefreshFlowStates() {
    if (!db_ || chapter_id_ <= 0 || chapter_ord_ <= 0 || book_root_.empty()) {
        for (const StageRow& s : stages_) {
            SetNodeState(s.code, QStringLiteral("todo"), true);
        }
        RebuildStageTable();
        return;
    }
    const auto dir = novelcore::ChapterWorkDir(book_root_, chapter_ord_);
    std::error_code ec;
    const bool hasManifest = std::filesystem::exists(ManifestPath(), ec);
    const auto hasFile = [&dir](std::string_view stage) {
        const std::string_view f = novelcore::StageFileName(stage);
        if (f.empty()) {
            return false;
        }
        std::error_code e2;
        return std::filesystem::exists(dir / std::string(f), e2);
    };
    const bool ctx = hasFile("CONTEXT_ASSEMBLY");
    const bool plan = hasFile("SCENE_EVENT_ORDER");
    const bool review = hasFile("CHAPTER_REVIEW");
    const bool repair = hasFile("CHAPTER_REPAIR");
    const bool extract = hasFile("STATE_EXTRACT");
    const bool validate = hasFile("STATE_VALIDATE");
    const bool hasSnap = std::filesystem::exists(SnapshotPath(), ec);

    QString body;
    {
        novelcore::NovelGraph g(*db_);
        if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_)); ch) {
            body = QString::fromStdString(ch->body);
        }
    }
    const bool hasBody = !body.trimmed().isEmpty();
    const bool commitNoDiff = last_run_.commit_note.find("没有给出 StateDiff") != std::string::npos;

    for (const StageRow& s : stages_) {
        QString st;
        if (s.code == QLatin1String("T1")) {
            st = QStringLiteral("done"); // 章节已初始化（行就在库里）
        } else if (s.merged) {
            st = plan ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T5")) {
            st = ctx ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T10")) {
            st = plan ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T11")) {
            st = hasBody ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T12")) {
            st = review ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T13")) {
            if (repair) {
                st = QStringLiteral("done");
            } else if (review) {
                st = QStringLiteral("skipped"); // 条件产物：评审一次过 = 不触发
            } else {
                st = QStringLiteral("todo");
            }
        } else if (s.code == QLatin1String("T14")) {
            st = extract ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T15")) {
            st = validate ? QStringLiteral("done") : QStringLiteral("todo");
        } else if (s.code == QLatin1String("T16")) {
            if (hasSnap || (has_run_ && (last_run_.committed || last_run_.skipped))) {
                st = QStringLiteral("done");
            } else if (commitNoDiff) {
                st = QStringLiteral("skipped"); // 没有 StateDiff 可提交
            } else {
                st = QStringLiteral("todo");
            }
        } else if (s.code == QLatin1String("T17")) {
            if (hasManifest && (!has_run_ || last_run_.ok)) {
                st = QStringLiteral("done");
            } else if (has_run_ && !last_run_.ok) {
                st = QStringLiteral("failed");
            } else {
                st = QStringLiteral("todo");
            }
        }
        SetNodeState(s.code, st, true);
    }
    RebuildStageTable();
}

void ChapterFlowView::ShowStage(const QString& stageCode) {
    const StageRow* row = nullptr;
    for (const StageRow& s : stages_) {
        if (s.code == stageCode) {
            row = &s;
            break;
        }
    }
    if (row == nullptr) {
        return;
    }
    artifact_sel_->blockSignals(true);
    {
        std::vector<widgets::Select::Item> items;
        for (const StageRow& s : stages_) {
            widgets::Select::Item it;
            it.text = s.code + QStringLiteral(" ") + s.name;
            it.checked = s.code == stageCode;
            items.push_back(std::move(it));
        }
        artifact_sel_->SetItems(std::move(items));
    }
    artifact_sel_->blockSignals(false);

    QString fileText;
    if (row->merged) {
        fileText = QStringLiteral("merged:09_chapter_plan.json");
    } else {
        const std::string_view f = novelcore::StageFileName(row->stage.toStdString());
        fileText = f.empty() ? QStringLiteral("-")
                             : QString::fromLatin1(f.data(), static_cast<int>(f.size()));
        if (row->code == QLatin1String("T11")) {
            fileText = QStringLiteral("chapters.body");
        } else if (row->code == QLatin1String("T16")) {
            fileText = QStringLiteral("snapshots/ch%1.json").arg(Pad3(chapter_id_));
        }
    }
    artifact_note_->setText(
        QStringLiteral("%1 · 产物：%2 · 状态：%3 · 耗时：%4 ms")
            .arg(row->code + QStringLiteral(" ") + row->stage + QStringLiteral(" ") + row->name,
                 fileText, StateNameOf(stageCode))
            .arg(stage_ms_.value(stageCode, 0)));
    widgets::SetTextColor(artifact_note_, theme::Current().textSecondary);
    artifact_tree_->SetJson(ArtifactTextFor(stageCode));
    artifact_stack_->setCurrentWidget(artifact_tree_);
}

QString ChapterFlowView::ArtifactTextFor(const QString& stageCode) const {
    const StageRow* row = nullptr;
    for (const StageRow& s : stages_) {
        if (s.code == stageCode) {
            row = &s;
            break;
        }
    }
    if (row == nullptr) {
        return QStringLiteral("{\"error\":\"没有这个阶段：%1\"}").arg(stageCode);
    }
    if (chapter_ord_ <= 0 || book_root_.empty()) {
        return QStringLiteral("{\"error\":\"还没选章节 —— 先在上方选一章再看产物\"}");
    }
    const auto dir = novelcore::ChapterWorkDir(book_root_, chapter_ord_);

    // T1：章节上下文（合成账 —— 03 §2.7 文件名表里没有它的文件）
    if (row->code == QLatin1String("T1")) {
        QString title;
        int ord = 0;
        qint64 pov = 0;
        if (db_) {
            novelcore::NovelGraph g(*db_);
            if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_)); ch) {
                title = QString::fromStdString(ch->title);
                ord = ch->ord;
                pov = static_cast<qint64>(ch->pov_entity_id);
            }
        }
        return QStringLiteral("{\"stage\":\"CHAPTER_INIT\",\"note\":\"合成账：章节上下文初始化"
                              "（03 §2.7 无对应文件）\",\"chapter_id\":%1,\"ord\":%2,\"title\":\"%3\","
                              "\"pov_entity_id\":%4}")
            .arg(chapter_id_)
            .arg(ord)
            .arg(title)
            .arg(pov);
    }

    // T2–T4/T6–T9：合并执行 → 产物并入 09_chapter_plan.json（03 §2.2）
    if (row->merged) {
        std::string plan = last_run_.plan_json;
        if (plan.empty()) {
            if (auto p = novelcore::ReadStageArtifact(book_root_, chapter_ord_, "SCENE_EVENT_ORDER");
                p) {
                plan = p->payload;
            }
        }
        QString out = QStringLiteral("{\"merged_into\":\"09_chapter_plan.json\",\"stage\":\"");
        out += row->stage;
        out += QStringLiteral("\",\"note\":\"03 §2.2 合并执行：本阶段产物并入 09_chapter_plan.json\","
                              "\"chapter_plan\":");
        out += plan.empty() ? QStringLiteral("null") : QString::fromStdString(plan);
        out += QStringLiteral("}");
        return out;
    }

    // T11：正文产物就是 chapters.body（P4：NOVEL_WRITE 落盘前不写库）
    if (row->code == QLatin1String("T11")) {
        QString body;
        if (db_) {
            novelcore::NovelGraph g(*db_);
            if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_)); ch) {
                body = QString::fromStdString(ch->body);
            }
        }
        if (body.isEmpty()) {
            body = QString::fromStdString(last_run_.body);
        }
        QString out = QStringLiteral("{\"source\":\"chapters.body\",\"chapter_id\":");
        out += QString::number(chapter_id_);
        out += QStringLiteral(",\"words\":");
        out += QString::number(body.size());
        out += QStringLiteral(",\"body\":");
        out += QString::fromStdString(util::json::JsonQuote(body.toStdString()));
        out += QStringLiteral("}");
        return out;
    }

    // T16：提交产物 = 章快照
    if (row->code == QLatin1String("T16")) {
        if (auto bytes = util::ReadFileBytes(SnapshotPath()); bytes) {
            return QString::fromStdString(*bytes);
        }
        QString out = QStringLiteral("{\"stage\":\"COMMIT\",\"note\":\"还没有快照 —— T16 状态提交"
                                     "（G1–G5 全过）后落 snapshots/ch%1.json\",\"committed\":")
                          .arg(chapter_id_);
        out += last_run_.committed ? QStringLiteral("true}") : QStringLiteral("false}");
        return out;
    }

    // T17：下一章备忘（合成账）
    if (row->code == QLatin1String("T17")) {
        QString hook;
        if (!last_run_.plan_json.empty()) {
            const auto doc = util::json::ParseDoc(last_run_.plan_json);
            if (doc) {
                hook = QString::fromStdString(
                    util::json::GetStrCopy(doc.root(), "ending_hook"));
            }
        }
        QString out = QStringLiteral("{\"stage\":\"NEXT_CHAPTER\",\"note\":\"下一章备忘"
                                     "（供 T5 上下文装配取用）\",\"ending_hook\":");
        out += QString::fromStdString(util::json::JsonQuote(hook.toStdString()));
        out += QStringLiteral("}");
        return out;
    }

    // 其余：work/chNNN/<StageFileCatalog 文件> 原文（P1 包装格式）
    const std::string_view f = novelcore::StageFileName(row->stage.toStdString());
    if (f.empty()) {
        return QStringLiteral("{\"error\":\"%1 不在 StageFileCatalog 里\"}").arg(row->stage);
    }
    if (auto bytes = util::ReadFileBytes(dir / std::string(f)); bytes) {
        return QString::fromStdString(*bytes);
    }
    return QStringLiteral("{\"empty\":\"%1 还没落盘 —— 跑一次全链（T1–T17）就会出现在 %2\"}")
        .arg(QString::fromLatin1(f.data(), static_cast<int>(f.size())),
             QStringLiteral("work/ch%1/").arg(Pad3(chapter_ord_)));
}

QString ChapterFlowView::StateNameOf(const QString& s) const {
    if (s == QLatin1String("running")) {
        return QStringLiteral("跑中");
    }
    if (s == QLatin1String("done")) {
        return QStringLiteral("完成");
    }
    if (s == QLatin1String("failed")) {
        return QStringLiteral("失败");
    }
    if (s == QLatin1String("skipped")) {
        return QStringLiteral("跳过（未触发）");
    }
    return QStringLiteral("待跑");
}

void ChapterFlowView::SetNodeState(const QString& code, const QString& s, bool live) {
    if (state_.value(code) == s) {
        return;
    }
    state_.insert(code, s);
    flow_->SetNodeState(code, NodeStateOf(s));
    if (live) {
        std::lock_guard<std::mutex> lock(live_mutex_);
        live_log_.push_back(code + QLatin1Char(':') + s);
    }
}

void ChapterFlowView::SetRunning(const QString& step, bool on) {
    run_btn_->SetLoading(on);
    run_btn_->setEnabled(!on);
    resume_btn_->setEnabled(!on);
    gen_btn_->setEnabled(!on);
    chapter_sel_->setEnabled(!on);
    state_label_->setText(on ? step
                             : (step.isEmpty() ? QStringLiteral("跑完。产物在 work/ch%1/。")
                                                     .arg(Pad3(chapter_ord_))
                                               : step));
    widgets::SetTextColor(state_label_, on ? theme::Current().statusBusy : theme::Current().textSecondary);
}

[[nodiscard]] std::filesystem::path ChapterFlowView::ManifestPath() const {
    if (book_root_.empty() || chapter_ord_ <= 0) {
        return {};
    }
    return novelcore::ChapterWorkDir(book_root_, chapter_ord_) / "_manifest.json";
}

[[nodiscard]] std::filesystem::path ChapterFlowView::SnapshotPath() const {
    if (book_root_.empty() || chapter_id_ <= 0) {
        return {};
    }
    return book_root_ / "snapshots" /
           (QStringLiteral("ch%1.json").arg(Pad3(chapter_id_)).toStdString());
}

[[nodiscard]] bool ChapterFlowView::HasArtifact(const QString& stageCode) const {
    for (const StageRow& s : stages_) {
        if (s.code != stageCode) {
            continue;
        }
        if (s.merged) {
            return static_cast<bool>(
                novelcore::ReadStageArtifact(book_root_, chapter_ord_, "SCENE_EVENT_ORDER"));
        }
        if (s.code == QLatin1String("T11")) {
            return false; // 证据在 chapters.body（RefreshFlowStates 直读）
        }
        return static_cast<bool>(
            novelcore::ReadStageArtifact(book_root_, chapter_ord_, s.stage.toStdString()));
    }
    return false;
}

bool ChapterFlowView::WriteManifest(const std::string& status) {
    if (book_root_.empty() || chapter_id_ <= 0 || chapter_ord_ <= 0 || !db_) {
        return false;
    }
    // 阶段账按 T 阶段代码记（贴 `03` §2.2 的语义）；指纹与运行循环同源（StateFingerprint）
    std::vector<std::string> stages;
    for (const StageRow& s : stages_) {
        stages.push_back(s.code.toStdString() + " " + s.stage.toStdString());
    }
    novelcore::NovelRunLoop loop(*db_, call_, stream_);
    const std::string fingerprint = loop.StateFingerprint();
    const std::string json = novelcore::ManifestJson(
        static_cast<novelcore::RowId>(chapter_id_), chapter_ord_, fingerprint, stages, status,
        novelcore::ScanChapterStages(book_root_, static_cast<novelcore::RowId>(chapter_id_),
                                     chapter_ord_));
    const auto path = ManifestPath();
    return util::WriteFileEnsuredDir(path, json);
}

void ChapterFlowView::Ui(const std::function<void()>& fn) {
    if (!fn) {
        return;
    }
    if (QThread::currentThread() == this->thread()) {
        fn();
    } else {
        async::PostToUi(fn);
    }
}

// ————————————————————————————————————————————— 全链跑完（GenerateChapter 驱动）

bool ChapterFlowView::RunCurrent(bool resume, QString* report) {
    return RunChapter(chapter_id_, resume, report);
}

bool ChapterFlowView::RunChapter(qint64 chapterId, bool resume, QString* report) {
    QString rep;
    const auto fail = [this, report, &rep](const QString& msg) {
        rep += msg + QLatin1Char('\n');
        if (report != nullptr) {
            *report = rep;
        }
        Ui([this, msg] {
            state_label_->setText(msg);
            widgets::SetTextColor(state_label_, theme::Current().statusDanger);
            SetRunning(msg, false);
        });
        return false;
    };
    if (!db_) {
        return fail(open_error_.isEmpty() ? QStringLiteral("跑不成：书库没打开。出路：先选书或新建一本书。")
                                          : QStringLiteral("跑不成：%1").arg(open_error_));
    }
    if (!call_ && !stream_) {
        return fail(QStringLiteral("跑不成：还没接 LLM。出路：在 S8「模型与 Prompt」配置写作模型，"
                                   "或用自动化探针注入 mock 后再跑。"));
    }

    // 换章只切数据上下文（worker 线程不碰控件；UI 刷新统一走 Ui()）
    chapter_id_ = chapterId;
    chapter_ord_ = 0;
    chapter_title_.clear();
    has_run_ = false;
    last_run_ = RunSummary{};
    {
        novelcore::NovelGraph g(*db_);
        auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_));
        if (!ch) {
            return fail(QStringLiteral("跑不成：章节读不到（%1）。出路：确认书库完整后重试。")
                            .arg(QString::fromStdString(ch.error().message)));
        }
        chapter_ord_ = ch->ord;
        chapter_title_ = QString::fromStdString(ch->title);
    }
    Ui([this, resume] {
        active_stages_.clear();
        SetRunning(resume ? QStringLiteral("跑中：断点续跑 T1–T17（哈希一致的产物跳过）…")
                          : QStringLiteral("跑中：T1–T17 全链（无人工干预）…"),
                   true);
    });

    // 跑中状态实时推进：Phase → T 节点（UI.md §2.3 的顺序）；跨线程时回主线程碰控件
    auto starts = std::make_shared<QHash<QString, qint64>>();
    auto applyPhase = [this, starts](Phase p, const QString& note) {
        const std::vector<QString> target = CodesOfPhase(p);
        const qint64 now = util::MonotonicMillis();
        for (const QString& code : active_stages_) {
            if (std::find(target.begin(), target.end(), code) == target.end()) {
                stage_ms_.insert(code, now - starts->value(code, now));
                SetNodeState(code, QStringLiteral("done"), true);
            }
        }
        for (const QString& code : target) {
            if (!starts->contains(code)) {
                starts->insert(code, now);
            }
            SetNodeState(code, QStringLiteral("running"), true);
        }
        active_stages_ = target;
        if (!note.isEmpty()) {
            state_label_->setText(note);
        }
    };

    const auto onProgress = [this, applyPhase](const agent::GenerateChapterProgress& p) {
        const QString note = QString::fromStdString(p.note);
        Ui([applyPhase, phase = p.phase, note] { applyPhase(phase, note); });
    };

    agent::NovelDirector dir(*db_, call_, stream_);
    agent::GenerateChapterRequest req;
    req.chapter_id = chapter_id_;
    req.resume = resume;
    req.project_dir = util::PathToUtf8(book_root_);
    req.snapshot_dir = util::PathToUtf8(book_root_ / "snapshots");
    req.canon_mode = "manual"; // 提交默认写 PROPOSED（`07` §2.5）；auto 面板在 P04-S10

    const std::int64_t chainStart = util::MonotonicMillis();
    auto res = dir.GenerateChapter(req, onProgress);
    const qint64 chainMs = static_cast<qint64>(util::ElapsedMillis(chainStart));

    RunSummary sum;
    if (!res) {
        sum.error = res.error().code + ": " + res.error().message;
    } else {
        sum.ok = true;
        sum.body = res->body;
        sum.plan_json = res->plan_json;
        sum.critic_json = res->critic_json;
        sum.revisions = res->revisions;
        sum.llm_calls = res->llm_calls;
        sum.committed = res->state_committed;
        sum.skipped = res->state_skipped;
        sum.commit_note = res->commit_note;
    }
    // manifest status 口径：committed→done / 幂等跳过→skipped / 跑完未回写→generated / 失败→failed
    const std::string status = !res            ? "failed"
                               : sum.skipped   ? "skipped"
                               : sum.committed ? "done"
                                               : "generated";
    const bool manifestOk = WriteManifest(status);
    has_run_ = true;
    last_run_ = sum;

    rep += res ? QStringLiteral("全链 T1–T17 跑完：正文 %1 字 · 修订 %2 轮 · LLM %3 次 · 状态%4\n")
                     .arg(QString::number(sum.body.size()))
                     .arg(sum.revisions)
                     .arg(sum.llm_calls)
                     .arg(sum.skipped ? QStringLiteral("已提交过（幂等跳过）")
                                      : (sum.committed ? QStringLiteral("已回写")
                                                       : QStringLiteral("未回写（%1）")
                                                             .arg(QString::fromStdString(
                                                                 sum.commit_note))))
               : QStringLiteral("生成失败：%1\n").arg(QString::fromStdString(sum.error));
    rep += QStringLiteral("产物目录 work/ch%1/ · _manifest.json %2 · 整链耗时 %3 ms\n")
               .arg(Pad3(chapter_ord_))
               .arg(manifestOk ? QStringLiteral("已落盘") : QStringLiteral("写入失败"))
               .arg(chainMs);

    Ui([this, ok = res.has_value(),
        err = res ? std::string{} : (res.error().code + ": " + res.error().message), manifestOk,
        chainMs] {
        RefreshFlowStates(); // 盘上证据收束（含 T16/T17 判定）
        if (!ok) {
            for (const QString& code : active_stages_) { // 失败停在哪个阶段 → 红
                SetNodeState(code, QStringLiteral("failed"), true);
            }
            active_stages_.clear();
        }
        const RunSummary& sum = last_run_;
        if (ok && manifestOk) {
            state_label_->setText(
                QStringLiteral("跑完 T1–T17：正文 %1 字 · 修订 %2 轮 · LLM %3 次 · 状态%4 · 耗时 %5 ms。"
                               "产物在 %6")
                    .arg(QString::number(sum.body.size()))
                    .arg(sum.revisions)
                    .arg(sum.llm_calls)
                    .arg(sum.skipped ? QStringLiteral("已提交过（幂等跳过）")
                                     : (sum.committed ? QStringLiteral("已回写")
                                                      : QStringLiteral("未回写（%1）")
                                                            .arg(QString::fromStdString(
                                                                sum.commit_note))))
                    .arg(chainMs)
                    .arg(QStringLiteral("work/ch%1/").arg(Pad3(chapter_ord_))));
            widgets::SetTextColor(state_label_, theme::Current().statusOk);
        } else if (!ok) {
            state_label_->setText(
                QStringLiteral("生成失败：%1 —— 出路：点「↻ 断点续跑」从上次阶段接着跑"
                               "（哈希一致的产物直接复用），或改提示词后重跑。")
                    .arg(QString::fromStdString(err)));
            widgets::SetTextColor(state_label_, theme::Current().statusDanger);
        } else {
            state_label_->setText(
                QStringLiteral("跑完 T1–T17，但 _manifest.json 写入失败（%1）。出路：确认磁盘可写后"
                               "点「↻ 断点续跑」补账。")
                    .arg(QStringLiteral("work/ch%1/").arg(Pad3(chapter_ord_))));
            widgets::SetTextColor(state_label_, theme::Current().statusWarn);
        }
    });

    if (report != nullptr) {
        *report = rep;
    }
    return res.has_value() && manifestOk;
}

void ChapterFlowView::GenerateCurrent(bool resume) {
    if (!run_btn_->isEnabled()) {
        return; // 跑中：防重入
    }
    if (chapter_id_ <= 0) {
        widgets::Toast::Show(QStringLiteral("还没选章节 —— 先在上方下拉选一章"),
                             widgets::Toast::Tone::Warning);
        return;
    }
    const qint64 id = chapter_id_;
    async::RunOnWorker([this, id, resume] {
        QString rep;
        const bool ok = RunChapter(id, resume, &rep);
        Ui([ok, rep] {
            widgets::Toast::Show(ok ? QStringLiteral("T1–T17 跑完 —— 产物已落 work/chNN/")
                                    : rep.trimmed(),
                                 ok ? widgets::Toast::Tone::Success : widgets::Toast::Tone::Error);
        });
    });
}

// ————————————————————————————————————————————— 探针

QString ChapterFlowView::StageProbe() const {
    QString out = QStringLiteral("stages=17 chapter=%1 ord=%2\n").arg(chapter_id_).arg(chapter_ord_);
    for (const StageRow& s : stages_) {
        QString fileText;
        if (s.merged) {
            fileText = QStringLiteral("merged:09_chapter_plan.json");
        } else {
            const std::string_view f = novelcore::StageFileName(s.stage.toStdString());
            fileText = f.empty() ? QStringLiteral("-")
                                 : QString::fromLatin1(f.data(), static_cast<int>(f.size()));
            if (s.code == QLatin1String("T11")) {
                fileText = QStringLiteral("chapters.body");
            } else if (s.code == QLatin1String("T16")) {
                fileText = QStringLiteral("snapshots/ch%1.json").arg(Pad3(chapter_id_));
            }
        }
        out += QStringLiteral("stage %1|%2|%3|%4|%5|%6\n")
                   .arg(s.code, s.stage, s.name, fileText, state_.value(s.code))
                   .arg(stage_ms_.value(s.code, 0));
    }
    return out;
}

QString ChapterFlowView::LiveProbe() const {
    std::lock_guard<std::mutex> lock(live_mutex_);
    QString out;
    for (std::size_t i = 0; i < live_log_.size(); ++i) {
        out += QStringLiteral("live %1|%2\n").arg(i + 1, 3, 10, QLatin1Char('0')).arg(live_log_[i]);
    }
    return out;
}

QString ChapterFlowView::ArtifactProbe(const QString& stageCode) const {
    return ArtifactTextFor(stageCode);
}

QString ChapterFlowView::ManifestProbe() const {
    const auto path = ManifestPath();
    if (path.empty()) {
        return QStringLiteral("{\"error\":\"还没选章节\"}");
    }
    if (auto text = util::ReadFileBytes(path); text) {
        return QString::fromStdString(*text);
    }
    return QStringLiteral("{\"error\":\"_manifest.json 还没落盘 —— 跑一次 T1–T17 即可\"}");
}

QString ChapterFlowView::ResumeProbe() const {
    // ① 产物账（P1/P2/P5，NovelStageLedger::FindResumeIndex）：从哪个产物阶段接着跑
    std::size_t artIndex = std::size(kArtifactStages);
    std::string artStage = "(全部一致)";
    if (db_ && chapter_id_ > 0 && chapter_ord_ > 0 && !book_root_.empty()) {
        const std::size_t idx =
            novelcore::FindResumeIndex(*db_, static_cast<novelcore::RowId>(chapter_id_),
                                       chapter_ord_, book_root_, kArtifactStages);
        artIndex = idx;
        artStage = idx < std::size(kArtifactStages) ? std::string{kArtifactStages[idx]}
                                                    : std::string{"(全部一致)"};
    }
    // ② 阶段级断点：T1–T17 里首个未完成的节点（T11 的正文证据在 chapters.body）
    QString node = QStringLiteral("T1");
    bool found = false;
    for (const StageRow& s : stages_) {
        const QString st = state_.value(s.code);
        if (st == QLatin1String("todo") || st == QLatin1String("failed")) {
            node = s.code;
            found = true;
            break;
        }
    }
    if (!found) {
        node = QStringLiteral("(全部完成)");
    }
    return QStringLiteral("resume-point=%1|artifact-index=%2/5|artifact-stage=%3")
        .arg(node)
        .arg(static_cast<qint64>(artIndex))
        .arg(QString::fromStdString(artStage));
}

} // namespace shine::app
