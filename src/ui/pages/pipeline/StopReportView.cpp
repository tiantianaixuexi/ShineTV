#include "ui/pages/pipeline/StopReportView.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/layout/QtLayout.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace shine::app {
namespace {

// views.css:164-169 `.dot`：7×7 正圆（kit::Badge 的 dot 态同径同形）
widgets::Badge* MakeDot(const char* tone, QWidget* parent) {
    auto* dot = new widgets::Badge(parent);
    dot->SetDot(true);
    if (tone != nullptr && *tone != '\0') {
        dot->SetTone(tone);
    }
    return dot;
}

} // namespace

StopReportView::StopReportView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[2]);
    // 标题只说「判定」：pipeline::Runner 不写 stop_report.md（那是 src/novel 的
    // NovelRunLoop 的产物），沿用那个文件名会是对用户的假话。
    title_ = widgets::SectionTitle(QStringLiteral("停止条件判定"), this);
    layout->addWidget(title_);

    // Overview.jsx:120-126：每行 = 状态点 + 规则码（mono，固定 26px）+ 规则名
    status_ = new QLabel(QStringLiteral("当前未触发停止条件"), this);
    status_->setWordWrap(true);
    widgets::SetKind(status_, "statestatus");
    layout->addWidget(status_);

    rules_ = new QVBoxLayout;
    rules_->setContentsMargins(0, 0, 0, 0);
    rules_->setSpacing(theme::space::kXs); // views.css:121 .col gap-2
    layout->addLayout(rules_);

    // views.css:128 触发即停的说明
    hint_ = new QLabel(QStringLiteral("触发即停：预算与前置条件任一不满足即中止本轮"), this);
    widgets::SetKind(hint_, "statemeta");
    hint_->setStyleSheet(QStringLiteral("font-size: 12px;"));
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    Rebuild();
}

void StopReportView::SetOwnTitle(bool on) {
    if (title_ != nullptr) {
        title_->setVisible(on);
    }
}

void StopReportView::SetDecision(pipeline::StopDecision decision) {
    decision_ = std::move(decision);
    Rebuild();
}

void StopReportView::SetBudget(pipeline::Budget budget) {
    budget_ = budget;
    Rebuild();
}

void StopReportView::Rebuild() {
    util::ClearLayout(rules_);

    if (decision_.stop) {
        status_->setText(QStringLiteral("%1：%2")
                             .arg(QString::fromStdString(decision_.rule),
                                  QString::fromStdString(decision_.reason)));
        widgets::SetTextColor(status_, theme::Current().statusDanger);
    } else {
        status_->setText(QStringLiteral("未触发停止条件 · 预算与前置条件均满足"));
        widgets::SetTextColor(status_, theme::Current().textMuted);
    }

    // S1–S4 是 StopPolicy 里唯一的预算组，阈值取自真实 Budget（不是编造的数）
    const std::array<std::pair<QString, QString>, 4> budget_rows{
        std::pair<QString, QString>{QStringLiteral("S1"), QStringLiteral("LLM 调用 %1 / %2")
                                                          .arg(budget_.llm_calls)
                                                          .arg(budget_.max_llm_calls)},
        {QStringLiteral("S2"), QStringLiteral("高档调用 %1 / %2")
                                  .arg(budget_.high_quality_calls)
                                  .arg(budget_.max_high_quality_calls)},
        {QStringLiteral("S3"), QStringLiteral("镜头 %1 / %2").arg(budget_.shots).arg(budget_.max_shots)},
        {QStringLiteral("S4"), QStringLiteral("估算成本 ¥%1 / ¥%2")
                                  .arg(budget_.cost, 0, 'f', 2)
                                  .arg(budget_.max_cost, 0, 'f', 2)},
    };
    for (const auto& [code, text] : budget_rows) {
        auto* row = new QWidget(this);
        auto* row_lay = new QHBoxLayout(row);
        row_lay->setContentsMargins(0, 0, 0, 0);
        row_lay->setSpacing(theme::space::kSteps[2]); // views.css:121 .row gap-2
        const bool over =
            decision_.stop && QString::fromStdString(decision_.rule).startsWith(QStringLiteral("S1"));
        row_lay->addWidget(MakeDot(over ? "danger" : "", row), 0, Qt::AlignVCenter);
        auto* code_label = new QLabel(code, row);
        code_label->setFixedWidth(26); // views.css:124 width: 26
        widgets::SetKind(code_label, "statemeta");
        code_label->setProperty("shineWeight", QStringLiteral("mono"));
        widgets::Repolish(code_label);
        row_lay->addWidget(code_label, 0);
        auto* value = new QLabel(text, row);
        widgets::SetKind(value, "statemeta");
        value->setStyleSheet(QStringLiteral("font-size: 12px;"));
        row_lay->addWidget(value, 1);
        rules_->addWidget(row);
    }
}

QString StopReportView::Probe() const {
    return QStringLiteral("stop=%1; rule=%2; reason=%3")
        .arg(decision_.stop ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(QString::fromStdString(decision_.rule))
        .arg(QString::fromStdString(decision_.reason));
}

} // namespace shine::app
