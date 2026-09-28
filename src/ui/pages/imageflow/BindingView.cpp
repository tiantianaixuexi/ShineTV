#include "ui/pages/imageflow/BindingView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {

BindingView::BindingView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("数据绑定 · 镜头字段 → 节点参数"), this);
    layout->addWidget(title);
    auto* add = new widgets::Button(QStringLiteral("添加默认绑定"), widgets::Button::Variant::Primary,
                                     widgets::Button::Size::Sm, this);
    auto* validate = new widgets::Button(QStringLiteral("校验全部"), widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, this);
    // webui BindingList：操作按钮收进标题行（fp-b 顶部），不占整幅宽度
    auto* action_row = new QWidget(this);
    auto* action_lay = new QHBoxLayout(action_row);
    action_lay->setContentsMargins(0, 0, 0, 0);
    action_lay->setSpacing(8);
    action_lay->addWidget(add);
    action_lay->addWidget(validate);
    action_lay->addStretch(1);
    layout->addWidget(action_row);
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({QStringLiteral("节点参数"), QStringLiteral("来源"),
                                       QStringLiteral("预览"), QStringLiteral("状态")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
    status_ = new QLabel(QStringLiteral("还没有绑定"), this);
    widgets::SetKind(status_, "statedetail");
    layout->addWidget(status_);
    connect(add, &QPushButton::clicked, this, &BindingView::AddDefaultBindings);
    connect(validate, &QPushButton::clicked, this, &BindingView::Validate);
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
    table_->setRowCount(static_cast<int>(binder_.Bindings().size()));
    for (int row = 0; row < static_cast<int>(binder_.Bindings().size()); ++row) {
        const auto& binding = binder_.Bindings()[static_cast<std::size_t>(row)];
        const auto value = binder_.Evaluate(binding, shot_);
        table_->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("%1.%2").arg(QString::fromStdString(binding.node_id),
                                         QString::fromStdString(binding.input_name))));
        const QString source = binding.source == flow::BindingSource::ShotPrompt ? QStringLiteral("shot.prompt")
                             : binding.source == flow::BindingSource::ShotNegative ? QStringLiteral("shot.negative")
                             : binding.source == flow::BindingSource::StableSeed ? QStringLiteral("stableSeed(shot.id)")
                             : QString::fromStdString(binding.expression);
        table_->setItem(row, 1, new QTableWidgetItem(source));
        table_->setItem(row, 2, new QTableWidgetItem(
            QString::fromStdString(value.value_or(std::string{"<缺失>"}))));
        table_->setItem(row, 3, new QTableWidgetItem(value.has_value() ? QStringLiteral("✔ 可用")
                                                                         : QStringLiteral("✘ 缺数据")));
    }
}

void BindingView::Validate() {

    const auto issues = binder_.Validate(shot_);
    status_->setText(issues.empty() ? QStringLiteral("绑定校验通过")
                                    : QStringLiteral("发现 %1 个提交前错误：%2")
                                          .arg(issues.size())
                                          .arg(QString::fromStdString(issues.front().message)));
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
