#include "ui/pages/storyboard/ContinuityView.h"

#include "core/Async.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "util/Encoding.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
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

// views.css:673 .chips { gap: 6px }
constexpr int kChipGap = 6;

// widgets::Tag 只读；改文案/色调走动态属性 + repolish（与 kit 的 Chip tone 同构）
void SetTagText(widgets::Tag* tag, const QString& text, const char* tone) {
    if (tag == nullptr) {
        return;
    }
    if (auto* label = tag->findChild<QLabel*>(); label != nullptr) {
        label->setText(text);
    }
    tag->setProperty("tone", QString::fromLatin1(tone));
    widgets::Repolish(tag);
}

} // namespace

ContinuityView::ContinuityView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("continuity"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    // Storyboard.jsx:128 <Card title="连续性 · C1–C12" extra={<Tag tone="ok">通过</Tag>}>
    auto* head = new QWidget(this);
    auto* head_row = new QHBoxLayout(head);
    head_row->setContentsMargins(0, 0, 0, 0);
    head_row->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("连续性 · C1–C12"), head);
    head_row->addWidget(title);
    head_row->addStretch(1);
    verdict_ = new widgets::Tag(QStringLiteral("未校验"), "idle", false, head);
    head_row->addWidget(verdict_);
    auto* run = new widgets::Button(QStringLiteral("运行 V8 连续性"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, head);
    head_row->addWidget(run);
    outer->addWidget(head);
    connect(run, &QPushButton::clicked, this, [this] { RunCheck(); });

    // C1–C12 清单（webui .chips：一排药丸，✓ 通过 / ✕ 违规 / 未校验）。
    // 设计稿是 flex + wrap，Qt 无 flow layout → 网格 + resizeEvent 重排。
    chips_ = new QWidget(this);
    chips_grid_ = new QGridLayout(chips_);
    chips_grid_->setContentsMargins(0, 0, 0, 0);
    chips_grid_->setSpacing(kChipGap);
    for (std::size_t i = 0; i < kCheckCodes.size(); ++i) {
        auto* chip = new widgets::Chip(
            QStringLiteral("%1 %2").arg(QString::fromLatin1(kCheckCodes[i]),
                                        QString::fromUtf8(kCheckNames[i])),
            "idle", chips_);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setToolTip(QStringLiteral("%1：%2").arg(QString::fromLatin1(kCheckCodes[i]),
                                                   QString::fromUtf8(kCheckNames[i])));
        chips_grid_->addWidget(chip, 0, static_cast<int>(i));
        chip_items_.push_back(chip);
    }
    outer->addWidget(chips_);

    status_ = new QLabel(QStringLiteral("选择章节后运行连续性校验。"), this);
    status_->setWordWrap(true);
    widgets::SetKind(status_, "statemeta");
    outer->addWidget(status_);
    list_ = new QWidget(this);
    auto* list_layout = new QVBoxLayout(list_);
    list_layout->setContentsMargins(0, 0, 0, 0);
    list_layout->addWidget(new widgets::EmptyState(
        QStringLiteral("◇"), QStringLiteral("尚未校验"), QStringLiteral("校验只比较状态，不让 LLM 自行猜测连续性。"),
        QString{}, list_));
    outer->addWidget(list_, 1);
    ReflowChips();
}

void ContinuityView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    ReflowChips();
}

// .chips 的 wrap 等价物：按当前宽度能塞下几枚药丸重排网格。分栏页里这一列
// 只有 1fr 宽（views.css:947），12 枚药丸挤一行会被压扁，必须换行。
void ContinuityView::ReflowChips() {
    if (chips_grid_ == nullptr || chip_items_.empty()) {
        return;
    }
    const int avail = chips_->width() > 0 ? chips_->width() : width();
    int widest = 0;
    for (const auto* chip : chip_items_) {
        // Chip 把 sizeHint 收成私有覆写，只能经 QWidget* 这个公共入口问宽度
        const QWidget* probe = chip;
        if (probe != nullptr) {
            widest = std::max(widest, probe->sizeHint().width());
        }
    }
    if (widest <= 0) {
        return;
    }
    const int cols = std::max(1, (avail + kChipGap) / (widest + kChipGap));
    if (cols == chip_cols_) {
        return;
    }
    chip_cols_ = cols;
    for (std::size_t i = 0; i < chip_items_.size(); ++i) {
        chips_grid_->addWidget(chip_items_[i], static_cast<int>(i / static_cast<std::size_t>(cols)),
                               static_cast<int>(i % static_cast<std::size_t>(cols)));
    }
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
    // Storyboard.jsx:128 extra={<Tag tone="ok">通过</Tag>} 的三态：
    // 未校验 idle / 有异常 danger / 全通过 ok
    SetTagText(verdict_, QStringLiteral("未校验"), "idle");
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
    if (status_ != nullptr) {
        status_->setText(QString::fromStdString(result.Describe()));
    }
    SetTagText(verdict_,
               result.issues.empty() ? QStringLiteral("通过")
                                    : QStringLiteral("%1 项异常").arg(result.issues.size()),
               result.issues.empty() ? "ok" : "danger");
    Rebuild();
}

void ContinuityView::Rebuild() {
    if (list_ == nullptr) return;
    RebuildChips();
    ReflowChips();
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
    // 文案变了（多一个 ✓/✕ 前缀）宽度也变，缓存的列数必须作废重算
    chip_cols_ = 0;
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
