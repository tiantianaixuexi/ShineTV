#include "ui/pages/shell/RightPanel.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/theme/Token.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

namespace {

// 可折叠段：点头部展开/收起（QToolBox 不在 kit 样式覆盖面内，这里用 kit 件自拼）
class Section : public QFrame {
  public:
    Section(const QString& title, QWidget* body, QWidget* parent = nullptr) : QFrame(parent) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(theme::space::kSteps[1]);
        header_ = new shine::widgets::Button(QStringLiteral("▾ %1").arg(title),
                                             shine::widgets::Button::Variant::Ghost,
                                             shine::widgets::Button::Size::Sm, this);
        connect(header_, &shine::widgets::Button::clicked, this, [this, title] {
            body_->setVisible(!body_->isVisible());
            header_->setText((body_->isVisible() ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + title);
        });
        body_ = body;
        lay->addWidget(header_);
        lay->addWidget(body_);
    }

  private:
    shine::widgets::Button* header_ = nullptr;
    QWidget* body_ = nullptr;
};

} // namespace

RightPanel::RightPanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("rightPanel"));
    // 收起态宽度由外壳给 0；打开态给一个够用的下限，避免检查器自己被压成残条
    setMinimumWidth(280);
    setMaximumWidth(560);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[3],
                            theme::space::kSteps[3], theme::space::kSteps[3]);
    lay->setSpacing(theme::space::kSteps[3]);

    stack_ = new QStackedWidget(this);

    // 空状态：没有选中对象时唯一可见的内容。不写任何「即将接入 / 占位」文案。
    empty_ = new QWidget(stack_);
    auto* el = new QVBoxLayout(empty_);
    el->setContentsMargins(0, theme::space::kSteps[4], 0, 0);
    empty_text_ = new QLabel(QStringLiteral("未选中任何对象"), empty_);
    shine::widgets::SetKind(empty_text_, "statesub");
    empty_text_->setAlignment(Qt::AlignCenter);
    empty_text_->setWordWrap(true);
    el->addWidget(empty_text_);
    el->addStretch();
    stack_->addWidget(empty_);

    // 段容器：页面 AddSection() 往这里放
    host_ = new QWidget(stack_);
    host_lay_ = new QVBoxLayout(host_);
    host_lay_->setContentsMargins(0, 0, 0, 0);
    host_lay_->setSpacing(theme::space::kSteps[3]);
    host_lay_->addStretch();
    stack_->addWidget(host_);

    lay->addWidget(stack_, 1);
}

QWidget* RightPanel::AddSection(const QString& title, QWidget* body) {
    if (body == nullptr) {
        return nullptr;
    }
    if (body->parentWidget() != host_) {
        body->setParent(host_);
    }
    auto* section = new Section(title, body, host_);
    host_lay_->insertWidget(host_lay_->count() - 1, section); // 插到末尾 stretch 之前
    if (stack_->currentWidget() == empty_) {
        stack_->setCurrentWidget(host_);
    }
    return section;
}

void RightPanel::SetSelection(const QString& what) {
    if (what.isEmpty()) {
        empty_text_->setText(QStringLiteral("未选中任何对象"));
        ClearSections();
        stack_->setCurrentWidget(empty_);
        return;
    }
    empty_text_->setText(what);
    if (stack_->currentWidget() == empty_) {
        stack_->setCurrentWidget(host_);
    }
}

void RightPanel::ClearSections() {
    while (host_lay_->count() > 1) { // 保留末尾 stretch
        QLayoutItem* item = host_lay_->takeAt(0);
        if (QWidget* w = item->widget(); w != nullptr) {
            w->deleteLater();
        }
        delete item;
    }
}

} // namespace shine::app
