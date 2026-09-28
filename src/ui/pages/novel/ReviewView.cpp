// P04-S7 `ReviewView` 实现：rubric 8 维（`06` §2.4 阈值+总判定首次落地）+ K01–K29
// 机器校验清单 + 修复轮入口（≤3 轮，走同一 GenerateChapter 真实管线）。
// 颜色零内联：条形/刻度/文字一律 theme token 派生。
#include "ui/pages/novel/ReviewView.h"
#include "ui/kit/theme/CssColor.h"

#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "novel/NovelGraph.h"
#include "novel/NovelStageLedger.h"
#include "novel/NovelTypes.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Time.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

using shine::novelcore::CheckOutcome;



[[nodiscard]] QString OutcomeText(CheckOutcome o) {
    using namespace shine::novelcore;
    const std::string_view n = CheckOutcomeName(o);
    return QString::fromUtf8(n.data(), static_cast<int>(n.size()));
}

} // namespace

namespace shine::app {

// —— rubric 8 维（`06` §2.4；world=80 是本 rubric 的硬阈值）——
const std::vector<RubricDim>& RubricDims() {
    static const std::vector<RubricDim> dims = {
        {"plot", "剧情目标", 60},   {"character", "人物一致", 60},
        {"causality", "因果", 60},  {"world", "世界观", 80},
        {"timeline", "时间线", 70}, {"foreshadow", "伏笔", 60},
        {"pacing", "节奏", 50},     {"style", "文风", 50},
    };
    return dims;
}

std::vector<RubricScore> ParseRubric(std::string_view criticJson) {
    // 格式①：`{"rubric":{"plot":72,…},"passed":…,"issues":[…]}`（新契约）
    // 格式②：维度直接平铺在根（`{"plot":72,…}`）
    std::vector<RubricScore> out;
    const auto doc = util::json::ParseDoc(criticJson);
    if (!doc) {
        return out;
    }
    yyjson_val* root = doc.root();
    yyjson_val* obj = util::json::GetObj(root, "rubric");
    if (obj == nullptr) {
        obj = root;
    }
    for (const RubricDim& d : RubricDims()) {
        RubricScore s;
        s.key = d.key;
        s.name = QString::fromUtf8(d.name);
        s.threshold = d.threshold;
        s.score = util::json::GetInt(obj, d.key, 0); // 缺维按 0 = FAIL（不伪造通过）
        s.below = s.score < s.threshold;
        out.push_back(std::move(s));
    }
    return out;
}

bool RubricVerdictFail(const std::vector<RubricScore>& scores, std::string_view criticJson,
                       QString* why) {
    QStringList below;
    for (const RubricScore& s : scores) {
        if (s.below) {
            below << QStringLiteral("%1 %2/%3").arg(s.name).arg(s.score).arg(s.threshold);
        }
    }
    // high issue → FAIL（`06` §2.4「任一 high issue 即 FAIL」）
    bool hasHigh = false;
    const auto doc = util::json::ParseDoc(criticJson);
    if (doc) {
        if (yyjson_val* issues = util::json::GetArr(doc.root(), "issues"); issues != nullptr) {
            for (std::size_t i = 0; i < yyjson_arr_size(issues); ++i) {
                if (util::json::GetStr(yyjson_arr_get(issues, i), "severity") == "high") {
                    hasHigh = true;
                    break;
                }
            }
        }
    }
    const bool fail = !below.isEmpty() || hasHigh;
    if (why != nullptr) {
        QStringList all;
        if (!below.isEmpty()) {
            all << QStringLiteral("低于阈值：%1").arg(below.join(QStringLiteral("、")));
        }
        if (hasHigh) {
            all << QStringLiteral("存在 high issue");
        }
        *why = fail ? all.join(QStringLiteral("；"))
                    : QStringLiteral("8 维均达阈值且无 high issue（`06` §2.4）");
    }
    return fail;
}

namespace {

// —— rubric 条形 + 阈值刻度线（token 派生，零内联）——
class RubricBar : public QWidget {
  public:
    RubricBar(const RubricScore& s, QWidget* parent) : QWidget(parent), score_(s) {
        setMinimumHeight(30);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int h = height();
        const int barH = 10;
        const int y = (h - barH) / 2 + 3; // 给下方刻度留一行
        const int w = std::max(1, width() - 2);

        // 底槽（bg.elevated）
        p.fillRect(QRect(1, y, w, barH), shine::widgets::TokenQColor(theme::Current().bgElevated));
        // 得分条（低于阈值 = danger，否则 = accent primary）
        const auto c = score_.below
                           ? shine::widgets::TokenQColor(theme::Current().statusDanger)
                           : shine::widgets::TokenQColor(theme::Current().accentPrimary);
        const int fw = w * std::clamp(score_.score, 0, 100) / 100;
        p.fillRect(QRect(1, y, std::max(2, fw), barH), c);

        // 阈值刻度线（textSecondary，虚线感：短线三段）
        const int tx = 1 + w * std::clamp(score_.threshold, 0, 100) / 100;
        p.setPen(shine::widgets::TokenQColor(theme::Current().textSecondary));
        for (int dy = -3; dy <= 3; ++dy) {
            p.drawLine(tx, y + dy, tx, y + dy + (dy < 0 ? 1 : 0));
        }

        // 文案：`名称  分数/阈值`
        QFont f = font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
        p.setFont(f);
        p.setPen(shine::widgets::TokenQColor(score_.below ? theme::Current().statusDanger
                                                          : theme::Current().textPrimary));
        p.drawText(QRect(4, 0, w - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1  %2 / %3%4")
                       .arg(score_.name)
                       .arg(score_.score)
                       .arg(score_.threshold)
                       .arg(score_.below ? QStringLiteral("  ✘ 低于阈值")
                                          : QStringLiteral("  ✔")));
    }

  private:
    RubricScore score_;
};

} // namespace

ReviewView::ReviewView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);
    outer->addWidget(BuildHead());

    // —— rubric 区（8 维条形）——
    rubric_scroll_ = new QScrollArea(this);
    rubric_scroll_->setFrameShape(QFrame::NoFrame);
    rubric_scroll_->setWidgetResizable(true);
    rubric_stack_ = new QStackedWidget(rubric_scroll_);
    auto* empty = new widgets::EmptyState(
        QStringLiteral("📋"), QStringLiteral("还没评审"),
        QStringLiteral("先跑章节流水线 T12 生成评审产物，或点「进入修复轮」重评。"),
        QStringLiteral(""), rubric_stack_);
    rubric_rows_ = new QWidget(rubric_stack_);
    rubric_stack_->addWidget(empty);
    rubric_stack_->addWidget(rubric_rows_);
    rubric_stack_->setCurrentWidget(empty);
    rubric_scroll_->setWidget(rubric_stack_);
    outer->addWidget(new QLabel(QStringLiteral("语义 rubric（8 维 · `06` §2.4 阈值）"), this));
    outer->addWidget(rubric_scroll_, 2);

    // —— 机器校验区（K01–K29）——
    auto* ccap = new QLabel(QStringLiteral("机器校验 K01–K29（`06` §2.3 · low 级可记但放行）"), this);
    check_scroll_ = new QScrollArea(this);
    check_scroll_->setFrameShape(QFrame::NoFrame);
    check_scroll_->setWidgetResizable(true);
    check_table_ = new data::DataTable(QStringLiteral("p04.review.checks"), check_scroll_);
    check_table_->SetColumns({{QStringLiteral("id"), QStringLiteral("编号")},
                              {QStringLiteral("name"), QStringLiteral("检查")},
                              {QStringLiteral("outcome"), QStringLiteral("结论")},
                              {QStringLiteral("severity"), QStringLiteral("级别")},
                              {QStringLiteral("detail"), QStringLiteral("详情")}});
    check_scroll_->setWidget(check_table_);
    outer->addWidget(ccap);
    outer->addWidget(check_scroll_, 3);
}

ReviewView::~ReviewView() {
    CloseDb();
}

QWidget* ReviewView::BuildHead() {
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[1]);

    verdict_ = new QLabel(QStringLiteral("结论: —"), head);
    QFont vf = verdict_->font();
    vf.setBold(true);
    verdict_->setFont(vf);
    widgets::SetTextColor(verdict_, theme::Current().textSecondary);

    round_ = new QLabel(QStringLiteral("修复轮 0/3"), head);
    widgets::SetTextColor(round_, theme::Current().textSecondary);

    repair_btn_ = new widgets::Button(QStringLiteral("进入修复轮"), widgets::Button::Variant::Primary,
                                      widgets::Button::Size::Sm, head);
    repair_btn_->setToolTip(QStringLiteral("按 FAIL 项重评 + 最小修改（累计 ≤3 轮；T12 FAIL → T13）"));
    repair_btn_->setEnabled(false);
    connect(repair_btn_, &widgets::Button::clicked, this, [this] { Repair(); });

    ignore_btn_ = new widgets::Button(QStringLiteral("忽略并继续"),
                                      widgets::Button::Variant::Ghost,
                                      widgets::Button::Size::Sm, head);
    ignore_btn_->setToolTip(QStringLiteral("人工确认放行本次评审（只改 UI 态，不动产物与账）"));
    ignore_btn_->setEnabled(false);
    connect(ignore_btn_, &widgets::Button::clicked, this, [this] { IgnoreAndContinue(); });

    hint_ = new QLabel(QStringLiteral("选一章后自动读 `work/chNNN/10_review.json` 并跑 K01–K29。"),
                       head);
    hint_->setWordWrap(true);
    widgets::SetTextColor(hint_, theme::Current().textSecondary);

    hl->addWidget(verdict_);
    hl->addWidget(round_);
    hl->addWidget(repair_btn_);
    hl->addWidget(ignore_btn_);
    hl->addWidget(hint_, 1);
    return head;
}

void ReviewView::SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream) {
    call_ = std::move(call);
    stream_ = std::move(stream);
}

bool ReviewView::OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                          QString* err) {
    if (db_ && db_path_ == dbPath) {
        project_dir_ = projectDir; // 同库只更新工程根（可能是另一本书的 work）
        return true;
    }
    CloseDb();
    db_path_ = dbPath;
    db_ = std::make_unique<db::sqlite::Database>();
    if (auto r = db_->Open({.path = dbPath}); !r) {
        if (err != nullptr) {
            *err = QString::fromStdString(r.error().message);
        }
        db_.reset();
        db_path_.clear();
        return false;
    }
    project_dir_ = projectDir;
    return true;
}

void ReviewView::CloseDb() noexcept {
    db_.reset();
}

void ReviewView::SelectChapter(qint64 chapterId, int chapterOrd) {
    chapter_id_ = chapterId;
    chapter_ord_ = chapterOrd;
    ignored_ = false;
    injected_ = false;
    if (chapter_ord_ <= 0 || project_dir_.empty()) {
        if (chapter_ord_ <= 0 && db_ && chapter_id_ > 0) {
            novelcore::NovelGraph g(*db_);
            if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_)); ch) {
                chapter_ord_ = ch->ord;
            }
        }
    }
    Refresh();
}

void ReviewView::InjectReviewArtifact(std::string_view criticJson, int revisionsUsed) {
    injected_ = true;
    critic_json_ = std::string(criticJson);
    revisions_used_ = revisionsUsed;
    Refresh();
}

void ReviewView::Refresh() {
    // ① rubric 源：work/chNNN/10_review.json（P1 包装 → payload 即 critic JSON）
    //    injected_ = 探针直喂的评审产物（不重读盘，避免覆盖）
    if (!injected_) {
        critic_json_.clear();
        if (chapter_ord_ > 0 && !project_dir_.empty()) {
            if (auto art = novelcore::ReadStageArtifact(project_dir_, chapter_ord_, "CHAPTER_REVIEW")) {
                critic_json_ = art->payload;
            }
        }
    }
    // 修复轮数：11_repair_receipt.json 的 round（无 = 0 轮）
    revisions_used_ = 0;
    if (chapter_ord_ > 0 && !project_dir_.empty()) {
        if (auto art = novelcore::ReadStageArtifact(project_dir_, chapter_ord_, "CHAPTER_REPAIR")) {
            const auto doc = util::json::ParseDoc(art->payload);
            if (doc) {
                revisions_used_ = std::max(util::json::GetInt(doc.root(), "round", 0), 0);
            }
        }
    }
    scores_ = ParseRubric(critic_json_);

    QString why;
    const bool rubricFail = RubricVerdictFail(scores_, critic_json_, &why);
    RunMachineChecks();
    // 总判定：rubric FAIL **或** 机器校验有 high 失败 → FAIL（`06` §2.4 + `07` §2.3 G2）
    verdict_fail_ = rubricFail || machine_high_fail_ > 0;
    verdict_text_ = verdict_fail_ ? QStringLiteral("FAIL") : QStringLiteral("PASS");
    verdict_why_ = why;

    // —— 刷新 UI ——
    auto* rows = new QVBoxLayout(rubric_rows_);
    while (rows->count() > 0) {
        QLayoutItem* it = rows->takeAt(0);
        delete it->widget();
        delete it;
    }
    if (!critic_json_.empty()) {
        for (const RubricScore& s : scores_) {
            rows->addWidget(new RubricBar(s, rubric_rows_));
        }
        rubric_stack_->setCurrentWidget(rubric_rows_);
    } else {
        rubric_stack_->setCurrentIndex(0);
    }

    std::vector<std::vector<QString>> table;
    table.reserve(checks_.size());
    for (const CheckLine& c : checks_) {
        table.push_back({c.id, c.name, c.outcome, c.severity, c.detail});
    }
    check_table_->SetRows(std::move(table));

    verdict_->setText(QStringLiteral("结论: %1（rubric %2 · 机器 %3/%4）")
                          .arg(verdict_text_,
                               rubricFail ? QStringLiteral("FAIL") : QStringLiteral("PASS"))
                          .arg(ran_count_ - fail_count_)
                          .arg(ran_count_));
    widgets::SetTextColor(verdict_, verdict_fail_ ? theme::Current().statusDanger : theme::Current().statusOk);
    round_->setText(QStringLiteral("修复轮 %1/%2%3")
                        .arg(revisions_used_)
                        .arg(max_revisions_)
                        .arg(ignored_ ? QStringLiteral("（已人工确认放行）") : QString()));
    repair_btn_->setEnabled(verdict_fail_ && revisions_used_ < max_revisions_ && !running_ &&
                            static_cast<bool>(call_));
    ignore_btn_->setEnabled(verdict_fail_ && !ignored_ && !running_);
    if (verdict_fail_) {
        SetHint(QStringLiteral("%1 —— 修复轮 %2/%3 可用；出路：[进入修复轮] 或人工确认后[忽略并继续]")
                    .arg(why.isEmpty() ? QStringLiteral("评审 FAIL") : why)
                    .arg(revisions_used_)
                    .arg(max_revisions_),
                theme::Current().statusWarn);
    } else if (!critic_json_.empty()) {
        SetHint(QStringLiteral("rubric 8 维达阈值、无 high issue（`06` §2.4）"), theme::Current().statusOk);
    }
}

void ReviewView::RunMachineChecks() {
    checks_.clear();
    fail_count_ = 0;
    ran_count_ = 0;
    machine_high_fail_ = 0;
    if (!db_ || chapter_id_ <= 0) {
        return;
    }
    novelcore::CheckInputs in;
    in.chapter_id = static_cast<novelcore::RowId>(chapter_id_);
    in.chapter_ord = chapter_ord_;
    in.project_dir = project_dir_;
    in.snapshot_dir = project_dir_ / "snapshots";
    in.word_target = 3000;
    const novelcore::ValidationReport rep = novelcore::RunChapterChecks(*db_, in);
    for (const novelcore::CheckResult& r : rep.checks) {
        CheckLine c;
        c.id = QString::fromStdString(r.check_id);
        c.name = QString::fromStdString(r.name);
        c.outcome = OutcomeText(r.outcome);
        c.severity = QString::fromStdString(r.severity);
        c.detail = QString::fromStdString(r.detail);
        if (r.outcome != CheckOutcome::NotApplicable) {
            ++ran_count_;
        }
        if (!novelcore::CheckPassed(r)) {
            ++fail_count_;
            c.outcome = QStringLiteral("✘ ") + c.outcome;
            if (r.severity == "high") {
                ++machine_high_fail_;
            }
        }
        checks_.push_back(std::move(c));
    }
}

void ReviewView::ShowRepairRunning() {
    running_ = true;
    repair_btn_->setEnabled(false);
    ignore_btn_->setEnabled(false);
    repair_btn_->SetLoading(true);
    SetHint(QStringLiteral("修复轮跑中（T12 重评 → FAIL 则 T13 最小修改，累计 ≤3）…"),
            theme::Current().statusBusy);
}

void ReviewView::SetHint(const QString& text, std::uint32_t token) {
    hint_->setText(text);
    widgets::SetTextColor(hint_, token);
}

void ReviewView::IgnoreAndContinue() {
    ignored_ = true;
    verdict_text_ = QStringLiteral("PASS（人工确认）");
    verdict_->setText(QStringLiteral("结论: PASS（人工确认放行）"));
    widgets::SetTextColor(verdict_, theme::Current().statusWarn);
    round_->setText(QStringLiteral("修复轮 %1/%2（已人工确认放行）").arg(revisions_used_).arg(max_revisions_));
    SetHint(QStringLiteral("已人工确认放行（不动产物与账；`03` §2.7 的审计由门禁侧记）"),
            theme::Current().textSecondary);
}

void ReviewView::Repair() {
    if (running_ || !call_ || !db_ || chapter_id_ <= 0 || chapter_ord_ <= 0) {
        return;
    }
    if (revisions_used_ >= max_revisions_) {
        SetHint(QStringLiteral("修复轮已用满（%1/%2）—— 出路：手动改稿后重跑流水线，或人工确认放行。")
                    .arg(revisions_used_)
                    .arg(max_revisions_),
                theme::Current().statusWarn);
        return;
    }
    ShowRepairRunning();
    // 作废旧评审产物（人工要求重评）：P1 哈希账的输入没变，不清掉就会复用旧 verdict
    if (project_dir_.empty()) {
        running_ = false;
        return;
    }
    const auto reviewPath =
        novelcore::ChapterWorkDir(project_dir_, chapter_ord_) / "10_review.json";
    std::error_code ec;
    std::filesystem::remove(reviewPath, ec);

    // 剩余轮次预算 = 3 - 已用（累计口径；`06` §2.6「累计 ≤ max_revisions=3」）
    const int budget = max_revisions_ - revisions_used_;
    const qint64 cid = chapter_id_;
    const int ord = chapter_ord_;
    auto call = call_;
    auto stream = stream_;
    auto root = project_dir_;
    async::RunOnWorker([this, cid, ord, budget, call, stream, root] {
        agent::NovelDirector dir(*db_, call, stream);
        agent::GenerateChapterRequest req;
        req.chapter_id = cid;
        req.resume = true; // 复用 T1–T11 产物，只重跑 T12→T13
        req.project_dir = util::PathToUtf8(root);
        req.snapshot_dir = util::PathToUtf8(root / "snapshots");
        req.canon_mode = "manual";
        req.max_revisions = budget;
        const auto res = dir.GenerateChapter(req, nullptr);
        const bool ok = res.has_value();
        const int revs = ok ? res->revisions : 0;
        async::PostToUi([this, ok, revs] {
            running_ = false;
            repair_btn_->SetLoading(false);
            if (ok) {
                ++repairs_run_;
                SetHint(QStringLiteral("修复轮跑完（本页第 %1 次发起 · 本次 %2 轮）—— 重新对账 rubric 与 K01–K29")
                            .arg(repairs_run_)
                            .arg(revs),
                        theme::Current().statusOk);
            } else {
                SetHint(QStringLiteral("修复轮失败 —— 出路：检查 LLM 通路后重试，或人工确认放行。"),
                        theme::Current().statusDanger);
            }
            Refresh();
        });
    });
}

// ————————————————————————————————————————————— 探针

QString ReviewView::RubricProbe() const {
    QString out = QStringLiteral("rubric verdict=%1 dims=%2\n")
                      .arg(verdict_fail_ ? QStringLiteral("FAIL") : QStringLiteral("PASS"))
                      .arg(scores_.size());
    for (const RubricScore& s : scores_) {
        out += QStringLiteral("dim %1|%2|%3|%4|below=%5\n")
                   .arg(QString::fromStdString(s.key), s.name)
                   .arg(s.score)
                   .arg(s.threshold)
                   .arg(s.below ? 1 : 0);
    }
    out += QStringLiteral("issues-present=%1\n").arg(critic_json_.empty() ? 0 : 1);
    out += QStringLiteral("reason=%1\n").arg(verdict_why_);
    return out;
}

QString ReviewView::CheckProbe() const {
    QString out = QStringLiteral("checks ran=%1 fail=%2 all-pass=%3\n")
                      .arg(ran_count_)
                      .arg(fail_count_)
                      .arg(fail_count_ == 0 ? 1 : 0);
    for (const CheckLine& c : checks_) {
        out += QStringLiteral("check %1|%2|%3|%4|%5\n")
                   .arg(c.id, c.name, c.outcome, c.severity, c.detail);
    }
    return out;
}

QString ReviewView::RepairProbe() const {
    bool receipt = false;
    int round = 0;
    if (chapter_ord_ > 0 && !project_dir_.empty()) {
        if (auto art = novelcore::ReadStageArtifact(project_dir_, chapter_ord_, "CHAPTER_REPAIR")) {
            receipt = true;
            const auto doc = util::json::ParseDoc(art->payload);
            if (doc) {
                round = util::json::GetInt(doc.root(), "round", 0);
            }
        }
    }
    return QStringLiteral("repair rounds-used=%1 max=%2 receipt=%3 page-runs=%4 running=%5\n")
        .arg(revisions_used_)
        .arg(max_revisions_)
        .arg(receipt ? 1 : 0)
        .arg(round)
        .arg(repairs_run_)
        .arg(running_ ? 1 : 0);
}

} // namespace shine::app
