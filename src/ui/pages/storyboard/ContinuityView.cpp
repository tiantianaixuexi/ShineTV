#include "ui/pages/storyboard/ContinuityView.h"

#include "core/Async.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "util/Encoding.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace shine::app {
namespace {

// C1–C12 的规则名（与 novelcore::RunContinuityChecks 的 addIssue 码一一对应，
// 名字取自 src/novel/NovelContinuity.cpp 里每条规则的判据）
constexpr std::array<const char*, 12> kCheckCodes{
    "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10", "C11", "C12"};
constexpr std::array<const char*, 12> kCheckNames{
    "出场", "位置", "朝向", "姿势", "手部", "服装",
    "伤势", "道具持有", "道具状态", "光线", "环境", "机位"};

} // namespace

ContinuityView::ContinuityView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("连续性 · C1–C12"), this);
    outer->addWidget(title);

    // C1–C12 清单（webui .chips：一排药丸，✓ 通过 / ✕ 违规 / 未校验）
    chips_ = new QWidget(this);
    auto* chips_layout = new QHBoxLayout(chips_);
    chips_layout->setContentsMargins(0, 0, 0, 0);
    chips_layout->setSpacing(6);
    for (std::size_t i = 0; i < kCheckCodes.size(); ++i) {
        auto* chip = new widgets::Chip(
            QStringLiteral("%1 %2").arg(QString::fromLatin1(kCheckCodes[i]),
                                        QString::fromUtf8(kCheckNames[i])),
            "idle", chips_);
        chip->setEnabled(false);
        chip->setToolTip(QStringLiteral("%1：%2").arg(QString::fromLatin1(kCheckCodes[i]),
                                                   QString::fromUtf8(kCheckNames[i])));
        chips_layout->addWidget(chip);
        chip_items_.push_back(chip);
    }
    chips_layout->addStretch(1);
    outer->addWidget(chips_);

    status_ = new QLabel(QStringLiteral("选择章节后运行连续性校验。"), this);
    status_->setWordWrap(true);
    widgets::SetKind(status_, "statedetail");
    outer->addWidget(status_);
    auto* run_row = new QWidget(this);
    auto* rr = new QHBoxLayout(run_row);
    rr->setContentsMargins(0, 0, 0, 0);
    rr->setSpacing(theme::space::kSteps[1]);
    auto* run = new widgets::Button(QStringLiteral("运行 V8 连续性"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, run_row);
    rr->addWidget(run);
    rr->addStretch(1);
    outer->addWidget(run_row);
    list_ = new QWidget(this);
    auto* list_layout = new QVBoxLayout(list_);
    list_layout->setContentsMargins(0, 0, 0, 0);
    list_layout->addWidget(new widgets::EmptyState(
        QStringLiteral("◇"), QStringLiteral("尚未校验"), QStringLiteral("校验只比较状态，不让 LLM 自行猜测连续性。"),
        QString{}, list_));
    outer->addWidget(list_, 1);
    connect(run, &QPushButton::clicked, this, [this] { RunCheck(); });
}

void ContinuityView::SetContext(std::filesystem::path db_path, std::filesystem::path project_dir,
                                shine::novelcore::RowId chapter_id) {
    db_path_ = std::move(db_path);
    project_dir_ = std::move(project_dir);
    chapter_id_ = chapter_id;
    ClearResult();
}

void ContinuityView::ClearResult() {
    has_result_ = false;
    if (status_ != nullptr) status_->setText(QStringLiteral("选择章节后运行连续性校验。"));
    Rebuild();
}

bool ContinuityView::RunCheck() {
    if (db_path_.empty() || chapter_id_ <= 0) {
        return false;
    }
    status_->setText(QStringLiteral("校验中…"));
    const auto db_path = db_path_;
    const auto project_dir = project_dir_;
    const auto chapter_id = chapter_id_;
    async::RunOnWorker([this, db_path, project_dir, chapter_id] {
        shine::db::sqlite::Database db;
        shine::novelcore::ContinuityOutcome result;
        if (auto opened = db.Open({.path = db_path, .readOnly = true, .create = false}); !opened) {
            result.error = opened.error().message;
        } else {
            result = shine::novelcore::RunContinuityChecks(
                db, chapter_id, util::PathToUtf8(project_dir));
        }
        async::PostToUi([this, result = std::move(result)] { ShowResult(result); });
    });
    return true;
}

void ContinuityView::ShowResult(const shine::novelcore::ContinuityOutcome& result) {
    result_ = result;
    has_result_ = true;
    status_->setText(QString::fromStdString(result.Describe()));
    Rebuild();
}

void ContinuityView::Rebuild() {
    if (list_ == nullptr) return;
    RebuildChips();
    auto* layout = qobject_cast<QVBoxLayout*>(list_->layout());
    if (layout == nullptr) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    if (!has_result_) {
        layout->addWidget(new widgets::EmptyState(
            QStringLiteral("◇"), QStringLiteral("尚未校验"), QStringLiteral("校验只比较状态，不让 LLM 自行猜测连续性。"),
            QString{}, list_));
        return;
    }
    for (const auto& issue : result_.issues) {
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, list_);
        auto* body = card->BodyLayout();
        auto* label = new QLabel(QStringLiteral("%1 · %2\n%3")
                                       .arg(QString::fromStdString(issue.code),
                                            QString::fromStdString(issue.severity),
                                            QString::fromStdString(issue.detail)),
                                   card);
        label->setWordWrap(true);
        widgets::SetKind(label, issue.severity == "high" ? "danger" : "warn");
        body->addWidget(label);
        layout->addWidget(card);
    }
    if (result_.issues.empty()) {
        layout->addWidget(new widgets::EmptyState(
            QStringLiteral("✔"), QStringLiteral("连续性通过"), QStringLiteral("C1–C12 未发现机器可判定的不一致。"),
            QString{}, list_));
    }
}

// C1–C12 药丸着色：命中 issue = danger（✕），跑过且无 issue = ok（✓），没跑 = idle
void ContinuityView::RebuildChips() {
    for (std::size_t i = 0; i < chip_items_.size(); ++i) {
        widgets::Chip* chip = chip_items_[i];
        if (chip == nullptr || i >= static_cast<std::size_t>(kCheckCodes.size())) {
            continue;
        }
        if (!has_result_) {
            chip->setProperty("tone", QStringLiteral("idle"));
            chip->SetBaseText(QStringLiteral("%1 %2").arg(QString::fromLatin1(kCheckCodes[i]),
                                                     QString::fromUtf8(kCheckNames[i])));
        } else {
            bool failed = false;
            for (const auto& issue : result_.issues) {
                if (issue.code == kCheckCodes[i]) {
                    failed = true;
                    break;
                }
            }
            chip->setProperty("tone", failed ? QStringLiteral("danger") : QStringLiteral("ok"));
            chip->SetBaseText(QStringLiteral("%1 %2 %3")
                              .arg(failed ? QStringLiteral("✕") : QStringLiteral("✓"),
                                   QString::fromLatin1(kCheckCodes[i]),
                                   QString::fromUtf8(kCheckNames[i])));
        }
        widgets::Repolish(chip);
    }
}

QString ContinuityView::ContinuityProbe() const {    return QStringLiteral("checked=%1; shots=%2; pairs=%3; failed=%4; unverified=%5; issues=%6; report=%7")
        .arg(has_result_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(result_.shots_seen)
        .arg(result_.pairs_checked)
        .arg(result_.failed)
        .arg(result_.unverified)
        .arg(static_cast<int>(result_.issues.size()))
        .arg(QString::fromStdString(result_.report_path));
}

} // namespace shine::app
