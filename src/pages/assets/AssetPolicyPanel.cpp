#include "pages/assets/AssetPolicyPanel.h"

#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"
#include "widget/controls/Inputs.h"
#include "widget/controls/Surfaces.h"
#include "widget/controls/WidgetCommon.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace shine::app {

AssetPolicyPanel::AssetPolicyPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);

    auto* card = new widgets::Card(widgets::Card::Variant::Outlined, this);
    auto* body = card->BodyLayout();

    auto* title = new QLabel(QStringLiteral("依赖等待与降级策略"), card);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    body->addWidget(title);

    summary_ = new QLabel(card);
    summary_->setWordWrap(true);
    widgets::SetKind(summary_, "statedetail");
    body->addWidget(summary_);

    auto* strict_row = new QWidget(card);
    auto* strict_layout = new QHBoxLayout(strict_row);
    strict_layout->setContentsMargins(0, 0, 0, 0);
    auto* strict_label = new QLabel(QStringLiteral("允许超期降级（关闭 = 严格模式）"), strict_row);
    strict_label->setWordWrap(true);
    strict_layout->addWidget(strict_label, 1);
    allow_degrade_ = new widgets::Toggle(strict_row);
    allow_degrade_->setToolTip(QStringLiteral("开启：超时后按策略 B 生成；关闭：任何时候都只挂起"));
    strict_layout->addWidget(allow_degrade_);
    body->addWidget(strict_row);

    auto* timeout_row = new QWidget(card);
    auto* timeout_layout = new QHBoxLayout(timeout_row);
    timeout_layout->setContentsMargins(0, 0, 0, 0);
    auto* timeout_label = new QLabel(QStringLiteral("挂起超时（分钟）"), timeout_row);
    timeout_layout->addWidget(timeout_label, 1);
    timeout_minutes_ = new widgets::NumberInput(false, 0, 120, timeout_row);
    timeout_minutes_->setToolTip(QStringLiteral("0 = 立即进入降级判定；严格模式仍不会降级"));
    timeout_layout->addWidget(timeout_minutes_);
    body->addWidget(timeout_row);

    runtime_ = new QLabel(card);
    runtime_->setWordWrap(true);
    widgets::SetKind(runtime_, "statedetail");
    body->addWidget(runtime_);

    auto* audit = new QLabel(
        QStringLiteral("缺依赖先按 C 挂起，不阻塞其他资产；超期后按 B 降级并写 degradations.jsonl 与 audit_logs。"),
        card);
    audit->setWordWrap(true);
    widgets::SetKind(audit, "statedetail");
    body->addWidget(audit);
    outer->addWidget(card);

    allow_degrade_->SetOnToggled([this](bool) { Refresh(); });
    timeout_minutes_->SetOnChanged([this](double) { Refresh(); });
    SetPolicy({});
}

AssetPolicy AssetPolicyPanel::Policy() const {
    const double minutes = timeout_minutes_ == nullptr ? 30.0 : timeout_minutes_->Value();
    const auto clamped = std::clamp(minutes, 0.0, 120.0);
    return {
        .suspendTimeoutMs = static_cast<std::int64_t>(std::llround(clamped * 60.0 * 1000.0)),
        .allowDegrade = allow_degrade_ == nullptr || allow_degrade_->IsChecked(),
    };
}

void AssetPolicyPanel::SetPolicy(const AssetPolicy& policy) {
    const bool allow = policy.allowDegrade;
    const double minutes = std::clamp(policy.suspendTimeoutMs / (60.0 * 1000.0), 0.0, 120.0);
    if (allow_degrade_ != nullptr) {
        allow_degrade_->blockSignals(true);
        allow_degrade_->SetChecked(allow);
        allow_degrade_->blockSignals(false);
    }
    if (timeout_minutes_ != nullptr) {
        timeout_minutes_->blockSignals(true);
        timeout_minutes_->SetValue(minutes);
        timeout_minutes_->blockSignals(false);
    }
    Refresh();
}

void AssetPolicyPanel::SetRuntimeState(const QString& layer, const QString& phase,
                                       const QString& detail, bool active, bool degraded) {
    runtime_layer_ = layer;
    runtime_phase_ = phase;
    runtime_detail_ = detail;
    runtime_active_ = active;
    runtime_degraded_ = degraded;
    Refresh();
}

QString AssetPolicyPanel::PolicyProbe() const {
    const AssetPolicy policy = Policy();
    return QStringLiteral("policy=C>B; allowDegrade=%1; strict=%2; timeoutMs=%3; runtime=%4; layer=%5; active=%6; degraded=%7")
        .arg(policy.allowDegrade ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(policy.allowDegrade ? QStringLiteral("0") : QStringLiteral("1"))
        .arg(policy.suspendTimeoutMs)
        .arg(runtime_phase_.isEmpty() ? QStringLiteral("none") : runtime_phase_,
             runtime_layer_.isEmpty() ? QStringLiteral("none") : runtime_layer_)
        .arg(runtime_active_ ? QStringLiteral("1") : QStringLiteral("0"),
             runtime_degraded_ ? QStringLiteral("1") : QStringLiteral("0"));
}

void AssetPolicyPanel::Refresh() {
    if (summary_ == nullptr || runtime_ == nullptr) {
        return;
    }
    const AssetPolicy policy = Policy();
    summary_->setText(policy.allowDegrade
                          ? QStringLiteral("C 挂起 → 超时 B 降级；超时 %1 分钟")
                                .arg(policy.suspendTimeoutMs / (60 * 1000))
                          : QStringLiteral("严格模式：缺依赖始终按 C 挂起，拒绝任何降级"));
    if (runtime_phase_.isEmpty()) {
        runtime_->setText(QStringLiteral("当前无依赖等待任务。"));
    } else {
        const QString prefix = runtime_degraded_ ? QStringLiteral("已降级")
                               : runtime_phase_ == QStringLiteral("PENDING")
                                   ? QStringLiteral("C 挂起")
                                   : QStringLiteral("处理中");
        runtime_->setText(QStringLiteral("%1 · %2 · %3")
                              .arg(prefix, runtime_phase_,
                                   runtime_detail_.isEmpty() ? QStringLiteral("无附加说明")
                                                              : runtime_detail_));
    }
}

} // namespace shine::app
