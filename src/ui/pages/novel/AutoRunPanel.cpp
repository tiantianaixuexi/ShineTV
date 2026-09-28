#include "ui/pages/novel/AutoRunPanel.h"

#include "core/Async.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "llm/OpenAIConfig.h"
#include "novel/NovelPipeline.h"
#include "util/Encoding.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QPointer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace shine::app {
 
namespace {

[[nodiscard]] QString Text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString RunModeText(novelcore::RunMode mode) {
    switch (mode) {
    case novelcore::RunMode::Manual: return QStringLiteral("手动");
    case novelcore::RunMode::Semi: return QStringLiteral("半自动");
    case novelcore::RunMode::Auto: return QStringLiteral("全自动");
    }
    return QStringLiteral("手动");
}

[[nodiscard]] QString PathText(const std::filesystem::path& path) {
    return QString::fromStdString(util::PathToUtf8(path));
}

[[nodiscard]] int ClampInt(double value, int lo, int hi) {
    if (!std::isfinite(value)) {
        return lo;
    }
    return std::clamp(static_cast<int>(std::lround(value)), lo, hi);
}

} // namespace

AutoRunPanel::AutoRunPanel(QWidget* parent) : QWidget(parent) {
    BuildUi();
    Refresh();
}

AutoRunPanel::~AutoRunPanel() {
    if (state_) {
        state_->cancel.store(true, std::memory_order_release);
    }
    CloseBook();
}

void AutoRunPanel::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    auto* head = new QWidget(this);
    auto* head_layout = new QHBoxLayout(head);
    head_layout->setContentsMargins(0, 0, 0, 0);
    head_layout->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("无人值守运行"), head);
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    status_ = new QLabel(QStringLiteral("未启动"), head);
    widgets::SetTextColor(status_, theme::Current().textSecondary);
    head_layout->addWidget(title);
    head_layout->addWidget(status_);
    head_layout->addStretch(1);
    outer->addWidget(head);

    auto* control_card = new widgets::SectionCard(QStringLiteral("运行参数"), this);
    control_card->SetSubtitle(QStringLiteral("模式 / 预算护栏"));
    control_card->SetCollapsible(true);
    control_card->SetContentMinWidth(760);
    auto* control_layout = control_card->BodyLayout();
    auto* mode_row = new QHBoxLayout;
    mode_row->setSpacing(theme::space::kSteps[1]);
    mode_row->addWidget(new QLabel(QStringLiteral("模式"), control_card));
    mode_control_ = new widgets::Segmented(
        {QStringLiteral("手动"), QStringLiteral("半自动"), QStringLiteral("全自动")}, control_card);
    mode_control_->SetOnChanged([this](int index) {
        run_mode_ = index == 2 ? novelcore::RunMode::Auto
                               : (index == 1 ? novelcore::RunMode::Semi : novelcore::RunMode::Manual);
        Refresh();
    });
    mode_row->addWidget(mode_control_);
    mode_row->addStretch(1);
    control_layout->addLayout(mode_row);

    auto* budget_row = new QHBoxLayout;
    budget_row->setSpacing(theme::space::kSteps[1]);
    const auto add_number = [&](const QString& label, widgets::NumberInput*& out, double value,
                                double lo, double hi) {
        budget_row->addWidget(new QLabel(label, control_card));
        out = new widgets::NumberInput(false, lo, hi, control_card);
        out->SetValue(value);
        out->SetOnChanged([this](double) { RefreshLimits(); });
        budget_row->addWidget(out);
    };
    add_number(QStringLiteral("本次章数"), max_chapters_input_, 0, 0, 100000);
    if (max_chapters_input_ != nullptr) {
        max_chapters_input_->setToolTip(QStringLiteral("本次最多运行章数；0 = 不限"));
    }
    add_number(QStringLiteral("单章调用"), llm_calls_input_, limits_.max_llm_calls_per_chapter, 1,
               100000);
    add_number(QStringLiteral("高档调用"), high_tier_input_, limits_.max_high_tier_calls_per_chapter,
               0, 100000);
    add_number(QStringLiteral("镜头"), images_input_, limits_.max_images_per_chapter, 0, 1000);
    add_number(QStringLiteral("检查点间隔"), checkpoint_input_, checkpoint_every_, 0, 10000);
    add_number(QStringLiteral("全书调用上限"), total_calls_input_, 0, 0, 1000000000);
    control_layout->addLayout(budget_row);

    auto* action_row = new QHBoxLayout;
    start_btn_ = new widgets::Button(QStringLiteral("▶ 启动运行"), widgets::Button::Variant::Primary,
                                     widgets::Button::Size::Sm, control_card);
    start_btn_->setToolTip(QStringLiteral("走 NovelRunLoop；manual/semi/auto 均使用同一停止条件与报告账"));
    stop_btn_ = new widgets::Button(QStringLiteral("■ 停止"), widgets::Button::Variant::Danger,
                                    widgets::Button::Size::Sm, control_card);
    stop_btn_->setEnabled(false);
    start_btn_->setToolTip(QStringLiteral("启动前会先显示 auto 六条前置；不满足时不会绕过门禁"));
    connect(start_btn_, &widgets::Button::clicked, this, [this] { Start(); });
    connect(stop_btn_, &widgets::Button::clicked, this, [this] { Stop(); });
    action_row->addWidget(start_btn_);
    action_row->addWidget(stop_btn_);
    action_row->addStretch(1);
    control_layout->addLayout(action_row);
    outer->addWidget(control_card);

    auto* gate_card = new widgets::SectionCard(QStringLiteral("auto 启动前置"), this);
    gate_card->SetSubtitle(QStringLiteral("逐条显示"));
    gate_card->SetCollapsible(true);
    gate_card->SetContentMinWidth(680);
    auto* gate_layout = gate_card->BodyLayout();
    precondition_ = new widgets::ElidedLabel(QString{}, gate_card);
    widgets::SetKind(precondition_, "fieldhelp");
    precondition_->setMinimumHeight(150);
    gate_layout->addWidget(precondition_);
    outer->addWidget(gate_card);

    auto* progress_card = new widgets::SectionCard(QStringLiteral("进度"), this);
    progress_card->SetCollapsible(true);
    progress_card->SetContentMinWidth(560);
    auto* progress_layout = progress_card->BodyLayout();
    progress_text_ = new widgets::ElidedLabel(QStringLiteral("第 0 / 0 章 · 已用 0 次调用"), progress_card);
    widgets::SetKind(progress_text_, "fieldhelp");
    progress_ = new widgets::ProgressBar(progress_card);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->SetInlineText(QStringLiteral("未启动"));
    progress_layout->addWidget(progress_text_);
    progress_layout->addWidget(progress_);
    outer->addWidget(progress_card);

    auto* stop_card = new widgets::SectionCard(QStringLiteral("停止条件 S1–S12"), this);
    stop_card->SetCollapsible(true);
    stop_card->SetContentMinWidth(680);
    auto* stop_layout = stop_card->BodyLayout();
    stop_text_ = new widgets::ElidedLabel(QString{}, stop_card);
    widgets::SetKind(stop_text_, "fieldhelp");
    stop_layout->addWidget(stop_text_);
    outer->addWidget(stop_card);

    auto* log_card = new widgets::SectionCard(QStringLiteral("运行日志 / 报告"), this);
    log_card->SetContentMinWidth(680);
    auto* log_layout = log_card->BodyLayout();
    timeline_ = new QPlainTextEdit(log_card);
    timeline_->setReadOnly(true);
    timeline_->setMinimumHeight(260);
    timeline_->setPlaceholderText(QStringLiteral("启动后逐事件显示；停止时报告会写 stop_report.md"));
    log_layout->addWidget(timeline_, 1);
    outer->addWidget(log_card, 1);
}

bool AutoRunPanel::OpenBook(const std::filesystem::path& dbPath,
                            const std::filesystem::path& projectDir, QString* err) {
    if (running_) {
        if (err != nullptr) {
            *err = QStringLiteral("运行中不能切换书库");
        }
        return false;
    }
    if (dbPath.empty() || projectDir.empty()) {
        if (err != nullptr) {
            *err = QStringLiteral("书库或书根为空");
        }
        return false;
    }
    if (db_ && db_->isOpen() && db_path_ == dbPath && project_dir_ == projectDir) {
        return true;
    }
    CloseBook();
    auto candidate = std::make_shared<db::sqlite::Database>();
    if (auto result = candidate->Open({.path = dbPath}); !result) {
        if (err != nullptr) {
            *err = QString::fromStdString(result.error().message);
        }
        return false;
    }
    db_ = std::move(candidate);
    db_path_ = dbPath;
    project_dir_ = projectDir;
    Refresh();
    return true;
}

void AutoRunPanel::CloseBook() noexcept {
    if (running_) {
        return;
    }
    db_.reset();
    db_path_.clear();
    project_dir_.clear();
    last_result_.clear();
    last_outcome_ = novelcore::RunOutcome{};
    Refresh();
}

void AutoRunPanel::SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream) {
    if (running_) {
        return;
    }
    call_ = std::move(call);
    stream_ = std::move(stream);
    Refresh();
}

void AutoRunPanel::SetMode(novelcore::RunMode mode) {
    run_mode_ = mode;
    if (mode_control_ != nullptr) {
        const int index = mode == novelcore::RunMode::Manual
                              ? 0
                              : (mode == novelcore::RunMode::Semi ? 1 : 2);
        mode_control_->SetCurrent(index);
    }
    Refresh();
}

void AutoRunPanel::SetStartChapter(int ord) {
    start_ord_ = std::max(0, ord);
    Refresh();
}

void AutoRunPanel::SetMaxChapters(int count) {
    max_chapters_ = std::max(0, count);
    if (max_chapters_input_ != nullptr) {
        max_chapters_input_->SetValue(max_chapters_);
    }
    Refresh();
}

void AutoRunPanel::SetCheckpointEvery(int count) {
    checkpoint_every_ = std::max(0, count);
    if (checkpoint_input_ != nullptr) {
        checkpoint_input_->SetValue(checkpoint_every_);
    }
    Refresh();
}

void AutoRunPanel::SetRunLimits(const novelcore::RunLimits& limits) {
    limits_ = limits;
    if (llm_calls_input_ != nullptr) {
        llm_calls_input_->SetValue(limits_.max_llm_calls_per_chapter);
    }
    if (high_tier_input_ != nullptr) {
        high_tier_input_->SetValue(limits_.max_high_tier_calls_per_chapter);
    }
    if (images_input_ != nullptr) {
        images_input_->SetValue(limits_.max_images_per_chapter);
    }
    RefreshLimits();
}

void AutoRunPanel::SetMaxTotalLlmCalls(std::int64_t calls) {
    max_total_calls_ = std::max<std::int64_t>(0, calls);
    if (total_calls_input_ != nullptr) {
        total_calls_input_->SetValue(static_cast<double>(max_total_calls_));
    }
    Refresh();
}

void AutoRunPanel::RefreshLimits() {
    if (llm_calls_input_ != nullptr) {
        limits_.max_llm_calls_per_chapter = ClampInt(llm_calls_input_->Value(), 1, 100000);
    }
    if (high_tier_input_ != nullptr) {
        limits_.max_high_tier_calls_per_chapter =
            ClampInt(high_tier_input_->Value(), 0, 100000);
    }
    if (images_input_ != nullptr) {
        limits_.max_images_per_chapter = ClampInt(images_input_->Value(), 0, 1000);
    }
    if (max_chapters_input_ != nullptr) {
        max_chapters_ = ClampInt(max_chapters_input_->Value(), 0, 100000);
    }
    if (checkpoint_input_ != nullptr) {
        checkpoint_every_ = ClampInt(checkpoint_input_->Value(), 0, 10000);
    }
    if (total_calls_input_ != nullptr) {
        const double value = total_calls_input_->Value();
        max_total_calls_ = std::isfinite(value) && value > 0
                               ? static_cast<std::int64_t>(std::llround(value))
                               : 0;
    }
}

bool AutoRunPanel::LlmReady() const {
    return static_cast<bool>(call_) || !llm::ResolveApiKey().empty();
}

QString AutoRunPanel::PreconditionText() const {
    if (!db_ || !db_->isOpen()) {
        return QStringLiteral("前置 1–6：未打开书库\n");
    }
    novelcore::AutoPreconditionInput input =
        novelcore::ProbeAutoPrecondition(*db_, LlmReady(),
                                         call_ ? true : novel::CrossReviewEffective());
    const novelcore::BookBudgetEstimate estimate = novelcore::EstimateBookBudget(
        *db_, limits_.max_llm_calls_per_chapter, max_total_calls_, max_chapters_);
    input.book_budget_ok = !estimate.Over();
    input.budget_detail = estimate.Describe();
    const auto mark = [](bool pass) { return pass ? QStringLiteral("✔") : QStringLiteral("✘"); };
    QString out;
    out += QStringLiteral("前置 1 %1 最近一章 G1–G5 已验证\n").arg(mark(input.gates_verified_on_last_chapter));
    out += QStringLiteral("前置 2 %1 K01–K29 校验器全量可用\n").arg(mark(input.verifiers_complete));
    out += QStringLiteral("前置 3 %1 LLM 可用（%2）\n")
               .arg(mark(input.llm_ok), input.llm_ok ? QStringLiteral("已配置") : QStringLiteral("未配置 API Key"));
    out += QStringLiteral("前置 4 %1 Comfy 连通（本轮未请求出图）\n").arg(mark(input.comfy_ok));
    out += QStringLiteral("前置 5 %1 评审模型 ≠ 写作模型\n").arg(mark(input.cross_review_ok));
    out += QStringLiteral("前置 6 %1 全书预算：%2\n")
               .arg(mark(input.book_budget_ok), Text(estimate.Describe()));
    return out;
}

void AutoRunPanel::Refresh() {
    const bool hasBook = db_ && db_->isOpen() && !project_dir_.empty();
    if (start_btn_ != nullptr) {
        start_btn_->setEnabled(hasBook && !running_);
        start_btn_->SetLoading(running_);
    }
    if (stop_btn_ != nullptr) {
        stop_btn_->setEnabled(running_);
    }
    if (precondition_ != nullptr) {
        precondition_->SetFullText(PreconditionText());
        bool allOk = false;
        if (hasBook) {
            const QString text = precondition_->text();
            allOk = !text.contains(QStringLiteral("✘"));
        }
        widgets::SetTextColor(precondition_, hasBook && (run_mode_ != novelcore::RunMode::Auto || allOk)
                                   ? theme::Current().textSecondary
                                   : theme::Current().statusDanger);
    }
    if (stop_text_ != nullptr) {
        QString text;
        for (int i = 1; i <= 12; ++i) {
            const auto code = static_cast<novelcore::StopCode>(i);
            text += QStringLiteral("%1 %2\n")
                        .arg(Text(novelcore::StopCodeName(code)),
                             Text(novelcore::StopCodeCondition(code)));
        }
        stop_text_->SetFullText(text);
    }
    if (progress_ != nullptr && !running_) {
        progress_->SetIndeterminate(false);
    }
    if (status_ != nullptr && !running_ && !last_result_.isEmpty()) {
        status_->setText(last_result_);
    }
    if (progress_text_ != nullptr && last_outcome_.chapters_attempted > 0) {
        progress_text_->SetFullText(QStringLiteral("已完成 %1 章 · 尝试 %2 章 · LLM %3 次")
                                     .arg(last_outcome_.chapters_done)
                                     .arg(last_outcome_.chapters_attempted)
                                     .arg(last_outcome_.llm_calls_total));
    }
}

novelcore::RunRequest AutoRunPanel::MakeRequest() const {
    novelcore::RunRequest req;
    req.project_dir = project_dir_;
    req.from_ord = start_ord_;
    req.max_chapters = max_chapters_;
    req.mode = run_mode_;
    req.limits = limits_;
    req.checkpoint_every = checkpoint_every_;
    req.resume = true;
    req.max_total_llm_calls = max_total_calls_;
    if (call_ || !llm::ResolveApiKey().empty()) {
        req.llm_ready = LlmReady();
        req.cross_review_ok = call_ ? true : novel::CrossReviewEffective();
    }
    return req;
}

void AutoRunPanel::AppendLog(const QString& text) {
    if (timeline_ == nullptr) {
        return;
    }
    timeline_->moveCursor(QTextCursor::End);
    timeline_->insertPlainText(QStringLiteral("[%1] %2\n")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                        text));
}

void AutoRunPanel::SetRunning(bool running) {
    running_ = running;
    if (start_btn_ != nullptr) {
        start_btn_->SetLoading(running);
        start_btn_->setEnabled(!running && db_ && db_->isOpen());
    }
    if (stop_btn_ != nullptr) {
        stop_btn_->setEnabled(running);
    }
    if (progress_ != nullptr) {
        progress_->SetIndeterminate(running);
    }
    if (status_ != nullptr) {
        status_->setText(running ? QStringLiteral("运行中") : status_->text());
        widgets::SetTextColor(status_, running ? theme::Current().statusBusy : theme::Current().textSecondary);
    }
}

void AutoRunPanel::ProgressToUi(const std::shared_ptr<RunState>& state,
                                const novelcore::RunProgress& progress) {
    if (state != state_ || progress_ == nullptr) {
        return;
    }
    const int total = std::max(1, progress.chapters_total);
    const int value = std::clamp((progress.chapters_done * 100) / total, 0, 100);
    progress_->setValue(value);
    progress_->SetInlineText(QStringLiteral("第 %1 / %2 章")
                                 .arg(progress.chapters_done)
                                 .arg(progress.chapters_total));
    if (progress_text_ != nullptr) {
        progress_text_->SetFullText(QStringLiteral("第 %1 章 · %2 · %3")
                                     .arg(progress.chapter_ord)
                                     .arg(Text(progress.phase), progress.note.empty()
                                                  ? QStringLiteral("处理中")
                                                  : QString::fromStdString(progress.note)));
    }
    if (!progress.phase.empty() && progress.phase != "START") {
        AppendLog(QStringLiteral("ch%1 · %2 · %3")
                      .arg(progress.chapter_ord)
                      .arg(Text(progress.phase), progress.note.empty()
                                                   ? QString()
                                                   : QString::fromStdString(progress.note)));
    }
}

void AutoRunPanel::Start() {
    if (running_ || !db_ || !db_->isOpen() || project_dir_.empty()) {
        return;
    }
    RefreshLimits();
    novelcore::RunRequest req = MakeRequest();
    auto state = std::make_shared<RunState>();
    state_ = state;
    ++run_id_;
    QPointer<AutoRunPanel> guard(this);
    auto db = db_;
    auto call = call_;
    auto stream = stream_;
    req.cancel = [state] { return state->cancel.load(std::memory_order_acquire); };
    req.on_progress = [guard, state](const novelcore::RunProgress& progress) {
        async::PostToUi([guard, state, progress] {
            if (guard) {
                guard->ProgressToUi(state, progress);
            }
        });
    };
    AppendLog(QStringLiteral("启动：%1 · from=%2 max=%3 checkpoint=%4")
                  .arg(RunModeText(run_mode_))
                  .arg(start_ord_)
                  .arg(max_chapters_)
                  .arg(checkpoint_every_));
    SetRunning(true);
    async::RunOnWorker([db, req = std::move(req), state, call, stream, guard]() mutable {
        novelcore::RunOutcome outcome;
        if (call) {
            novelcore::NovelRunLoop loop(*db, call, stream);
            outcome = loop.Run(req);
        } else {
            outcome = novel::RunOnce(*db, req, &state->cancel, req.on_progress);
        }
        async::PostToUi([guard, state, outcome = std::move(outcome)]() mutable {
            if (guard) {
                guard->Finish(state, std::move(outcome));
            }
        });
    });
}

void AutoRunPanel::Stop() {
    if (!running_ || !state_) {
        return;
    }
    state_->cancel.store(true, std::memory_order_release);
    if (status_ != nullptr) {
        status_->setText(QStringLiteral("停止请求已发送，正在收束当前章…"));
    }
    AppendLog(QStringLiteral("用户请求停止：保存当前进度，可从断点续跑"));
}

void AutoRunPanel::Finish(const std::shared_ptr<RunState>& state, novelcore::RunOutcome outcome) {
    if (state != state_ || !running_) {
        return;
    }
    running_ = false;
    last_outcome_ = outcome;
    QString result;
    if (!outcome.refuse_reason.empty()) {
        result = QStringLiteral("未启动：%1").arg(QString::fromStdString(outcome.refuse_reason));
    } else if (outcome.stop && outcome.stop->code != novelcore::StopCode::None) {
        result = QStringLiteral("停止 %1：%2（%3）")
                     .arg(Text(novelcore::StopCodeName(outcome.stop->code)),
                          Text(outcome.stop->condition), QString::fromStdString(outcome.stop->detail));
    } else {
        result = QStringLiteral("运行结束：完成 %1 章").arg(outcome.chapters_done);
    }
    last_result_ = result;
    if (status_ != nullptr) {
        status_->setText(result);
        widgets::SetTextColor(status_, outcome.refuse_reason.empty() ? theme::Current().statusOk
                                                         : theme::Current().statusWarn);
    }
    if (progress_ != nullptr) {
        progress_->SetIndeterminate(false);
        progress_->setValue(outcome.refuse_reason.empty() ? 100 : 0);
        progress_->SetInlineText(result);
    }
    if (progress_text_ != nullptr) {
        progress_text_->SetFullText(QStringLiteral("完成 %1 章 · 尝试 %2 章 · LLM %3 次")
                                     .arg(outcome.chapters_done)
                                     .arg(outcome.chapters_attempted)
                                     .arg(outcome.llm_calls_total));
    }
    AppendLog(result);
    if (!outcome.stop_report_path.empty()) {
        AppendLog(QStringLiteral("stop_report.md：%1")
                      .arg(QString::fromStdString(outcome.stop_report_path)));
    }
    if (progress_ != nullptr) {
        progress_->SetState(outcome.refuse_reason.empty() ? "ok" : "error");
    }
    Refresh();
}

QString AutoRunPanel::ProbeText() const {
    QString out;
    out += QStringLiteral("mode=%1 start=%2 max_chapters=%3 checkpoint=%4 total_calls=%5\n")
               .arg(Text(novelcore::RunModeName(run_mode_)))
               .arg(start_ord_)
               .arg(max_chapters_)
               .arg(checkpoint_every_)
               .arg(max_total_calls_);
    out += QStringLiteral("limits|llm=%1|high_tier=%2|images=%3\n")
               .arg(limits_.max_llm_calls_per_chapter)
               .arg(limits_.max_high_tier_calls_per_chapter)
               .arg(limits_.max_images_per_chapter);
    out += PreconditionText();
    out += QStringLiteral("stop-conditions=12\n");
    for (int i = 1; i <= 12; ++i) {
        const auto code = static_cast<novelcore::StopCode>(i);
        out += QStringLiteral("stop %1|%2\n")
                   .arg(Text(novelcore::StopCodeName(code)), Text(novelcore::StopCodeCondition(code)));
    }
    out += QStringLiteral("running=%1\n").arg(running_ ? 1 : 0);
    if (!last_result_.isEmpty()) {
        out += QStringLiteral("last=%1\n").arg(last_result_);
    }
    return out;
}

QString AutoRunPanel::RunProbe() {
    if (!db_ || !db_->isOpen() || project_dir_.empty()) {
        return QStringLiteral("probe-error=book-not-open\n");
    }
    const auto run_mock = [this](novelcore::RunMode mode, int max_chapters, int checkpoint,
                                 bool resume, bool fail_k02) {
        novelcore::RunRequest req = MakeRequest();
        req.mode = mode;
        req.from_ord = 0;
        req.max_chapters = max_chapters;
        req.checkpoint_every = checkpoint;
        req.resume = resume;
        req.llm_ready = true;
        req.cross_review_ok = true;
        req.max_total_llm_calls = 1000000;
        novelcore::NovelRunLoop loop(*db_, nullptr);
        int runner_calls = 0;
        loop.SetChapterRunner([&runner_calls, fail_k02](
                                  novelcore::RowId, const novelcore::RunLimits&, novelcore::RunMode,
                                  const std::function<void(const agent::GenerateChapterProgress&)>&)
                                  -> std::expected<novelcore::ChapterRunInfo, agent::AgentError> {
            ++runner_calls;
            novelcore::ChapterRunInfo info;
            info.ok = true;
            info.state_committed = true;
            info.review_passed = true;
            info.llm_calls = 3;
            info.high_tier_calls = 1;
            info.stages = {"PLAN", "WRITE", "REVIEW", "COMMIT"};
            if (fail_k02) {
                info.failed_check_ids = {"K02", "K02"};
            }
            return info;
        });
        novelcore::RunOutcome outcome = loop.Run(req);
        return std::pair<novelcore::RunOutcome, int>{std::move(outcome), runner_calls};
    };

    QString out = ProbeText();
    const auto manual = run_mock(novelcore::RunMode::Manual, 1, 0, false, false);
    out += QStringLiteral("manual|started=%1|done=%2|attempts=%3|calls=%4\n")
               .arg(manual.first.started ? 1 : 0)
               .arg(manual.first.chapters_done)
               .arg(manual.first.chapters_attempted)
               .arg(manual.second);
    const auto semi = run_mock(novelcore::RunMode::Semi, 1, 1, false, false);
    out += QStringLiteral("semi|started=%1|done=%2|checkpoints=%3\n")
               .arg(semi.first.started ? 1 : 0)
               .arg(semi.first.chapters_done)
               .arg(semi.first.checkpoints.size());
    const auto automatic = run_mock(novelcore::RunMode::Auto, 1, 0, false, false);
    out += QStringLiteral("auto|started=%1|done=%2|refuse=%3\n")
               .arg(automatic.first.started ? 1 : 0)
               .arg(automatic.first.chapters_done)
               .arg(QString::fromStdString(automatic.first.refuse_reason));
    const auto stopped = run_mock(novelcore::RunMode::Manual, 1, 0, false, true);
    out += QStringLiteral("s1-stop|code=%1|detail=%2\n")
               .arg(stopped.first.stop ? Text(novelcore::StopCodeName(stopped.first.stop->code))
                                       : QStringLiteral("none"))
               .arg(stopped.first.stop ? QString::fromStdString(stopped.first.stop->detail)
                                       : QString());
    if (!stopped.first.stop_report_path.empty()) {
        out += QStringLiteral("stop-report=%1\n")
                   .arg(QString::fromStdString(stopped.first.stop_report_path));
    }
    if (!stopped.first.chapters_attempted) {
        out += QStringLiteral("probe-error=no-run\n");
    }
    last_outcome_ = stopped.first;
    last_result_ = QStringLiteral("S10 探针完成");
    return out;
}

} // namespace shine::app
