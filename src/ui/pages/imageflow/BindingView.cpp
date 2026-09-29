#include "ui/pages/imageflow/BindingView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include "ui/layout/QtLayout.h"

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=bindView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:290–320：
//   .dlist .drow      p8 2 + 底部 line-subtle 发丝线 + f12.5 text-secondary
//   .dlist .drow:hover fill.hover 底
//   .dlist .dsub      f11 text-muted 单行省略
// QSS 没有 :last-child，末行由构造方摘掉底边（见 Rebuild）。
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#bindView *[shineKind=\"drow\"]:hover { background-color: %1; }\n"
               "QWidget#bindView QLabel[bindRole=\"from\"] { color: %2; font-size: 11px; font-weight: 700; }\n"
               "QWidget#bindView QLabel[bindRole=\"to\"] { color: %3; font-size: 11px; }\n"
               "QWidget#bindView QLabel[bindRole=\"chev\"] { color: %4; font-size: 10px; }\n"
               "QWidget#bindView QLabel[bindRole=\"dsub\"] { color: %5; font-size: 11px; }\n"
               "QWidget#bindView QLabel[bindRole=\"warn\"] { color: %6; font-size: 11px; }\n")
        .arg(shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.accentPrimary),
             shine::widget::CssRgb(t.textSecondary), shine::widget::CssRgb(t.textMuted),
             shine::widget::CssRgb(t.textMuted), shine::widget::CssRgb(t.statusDanger));
}


} // namespace

BindingView::BindingView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("bindView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("数据绑定 · 镜头字段 → 节点参数"), this);
    layout->addWidget(title);
    // webui BindingList：按钮行在列表之上（fp-b 顶部）
    auto* action_row = new QWidget(this);
    auto* action_lay = new QHBoxLayout(action_row);
    action_lay->setContentsMargins(0, 0, 0, 0);
    action_lay->setSpacing(theme::space::kSteps[1]);
    auto* add = new widgets::Button(QStringLiteral("添加默认绑定"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, this);
    auto* validate = new widgets::Button(QStringLiteral("校验全部"), widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, this);
    action_lay->addWidget(add);
    action_lay->addWidget(validate);
    action_lay->addStretch(1);
    layout->addWidget(action_row);

    // 密集行列表（webui .dlist：发丝线分隔、无卡片框）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_ = new QWidget(scroll);
    list_lay_ = new QVBoxLayout(list_);
    list_lay_->setContentsMargins(0, 0, 0, 0);
    list_lay_->setSpacing(0);
    list_lay_->addStretch(1);
    scroll->setWidget(list_);
    layout->addWidget(scroll, 1);

    status_ = new QLabel(QStringLiteral("还没有绑定"), this);
    widgets::SetKind(status_, "statemeta");
    layout->addWidget(status_);
    connect(add, &QPushButton::clicked, this, &BindingView::AddDefaultBindings);
    connect(validate, &QPushButton::clicked, this, &BindingView::Validate);
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
}

void BindingView::SetShotContext(const flow::BindingShotContext& shot) {
    shot_ = shot;
    Rebuild();
}

void BindingView::AddDefaultBindings() {
    binder_.Add({"", "2", "positive", flow::BindingSource::ShotPrompt, "shot.prompt", true});
    binder_.Add({"", "3", "negative", flow::BindingSource::ShotNegative, "shot.negative", false});
    binder_.Add({"", "4", "seed", flow::BindingSource::StableSeed, "stableSeed(shot.id)", true});
    Rebuild();
    Validate();
}

void BindingView::Rebuild() {
    // 先摘掉旧行：布局项逐个 takeAt 后销毁，行控件本身由 list_ 父级链管
    util::ClearLayout(list_lay_, 1); // 末尾常驻 addStretch(1)，保留
    const auto& bindings = binder_.Bindings();
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        const auto value = binder_.Evaluate(binding, shot_);
        const bool last = i + 1 == bindings.size();

        auto* row = new QWidget(list_);
        widgets::SetKind(row, "drow");
        // QSS 没有 :last-child，末行摘掉底边（与 kit drow 约定一致）
        if (last) row->setProperty("shineKind", QString{});
        auto* col = new QVBoxLayout(row);
        col->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        col->setSpacing(3);

        auto* line = new QHBoxLayout;
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(theme::space::kSteps[2]);
        auto* from = new QLabel(
            QStringLiteral("%1.%2").arg(QString::fromStdString(binding.node_id),
                                        QString::fromStdString(binding.input_name)), row);
        from->setProperty("bindRole", QStringLiteral("from"));
        auto* chev = new QLabel(QStringLiteral("›"), row);
        chev->setProperty("bindRole", QStringLiteral("chev"));
        const QString source = binding.source == flow::BindingSource::ShotPrompt ? QStringLiteral("shot.prompt")
                             : binding.source == flow::BindingSource::ShotNegative ? QStringLiteral("shot.negative")
                             : binding.source == flow::BindingSource::StableSeed ? QStringLiteral("stableSeed(shot.id)")
                             : QString::fromStdString(binding.expression);
        auto* to = new widgets::ElidedLabel(source, row);
        to->setProperty("bindRole", QStringLiteral("to"));
        to->SetExpandable(false);
        auto* tag = new widgets::Tag(
            value.has_value() ? QStringLiteral("✔ 可用") : QStringLiteral("✘ 缺数据"),
            value.has_value() ? "ok" : "danger", false, row);
        line->addWidget(from, 0);
        line->addWidget(chev, 0);
        line->addWidget(to, 1);
        line->addWidget(tag, 0);
        col->addLayout(line);

        auto* preview = new widgets::ElidedLabel(
            value.has_value() ? QString::fromStdString(*value) : QStringLiteral("<缺失>"), row);
        preview->setProperty("bindRole", QStringLiteral("dsub"));
        preview->SetExpandable(false);
        col->addWidget(preview);
        list_lay_->insertWidget(list_lay_->count() - 1, row);
    }
    if (bindings.empty()) {
        status_->setText(QStringLiteral("还没有绑定：先「添加默认绑定」把镜头字段挂到节点参数"));
    }
}

void BindingView::Validate() {
    const auto issues = binder_.Validate(shot_);
    if (issues.empty()) {
        status_->setText(QStringLiteral("绑定校验通过"));
    } else {
        // webui BindingList 末尾的 danger 提示：数量 + 首条原因
        QStringList parts;
        for (const auto& issue : issues) parts << QString::fromStdString(issue.message);
        status_->setText(QStringLiteral("发现 %1 个提交前错误：%2")
                             .arg(issues.size())
                             .arg(parts.join(QStringLiteral("；"))));
        widgets::SetTextColor(status_, theme::Current().statusDanger);
    }
    Rebuild();
}

void BindingView::ValidateNow() { Validate(); }

QString BindingView::Probe() const {
    return QStringLiteral("bindings=%1; shot=%2; prompt=%3; issues=%4")
        .arg(binder_.Bindings().size())
        .arg(shot_.shot_id)
        .arg(shot_.prompt.empty() ? QStringLiteral("missing") : QStringLiteral("ready"))
        .arg(binder_.Validate(shot_).size());
}

} // namespace shine::app
