#include "ui/pages/shell/RightPanel.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/theme/Token.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

namespace {

// 可折叠段：点头部展开/收起（QToolBox 不在 kit 样式覆盖面内，这里用 kit 件自拼）
//
// ⚠️ 段的 body 是**页面拥有**的（页面自己 new 出来、自己拿指针刷新数据），
// 段只是把它借过来显示。因此 ClearSections() 只能拆掉段壳，
// 绝不能 deleteLater 掉 body —— 否则页面手里的指针立刻悬空，
// 下一次刷新（SetPairs / 切页）就是野指针崩溃。
// 归还时 body 重新挂回 host_ 并隐藏，等下一次 AddSection 复用。
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

    // 把 body 从段里摘下来还给外壳（不销毁）：页面仍持有它。
    // 只换父级 + 隐藏；body 自带的布局不动。
    void ReleaseBody(QWidget* newParent) {
        if (body_ == nullptr) {
            return;
        }
        body_->setParent(newParent);
        body_->hide();
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
    // body 自带布局（页面 new 出来时就装好了）。这里只换父级，不碰它的布局：
    // 段壳的 lay->addWidget(body) 会把 body 挂进段的布局，body 自己的布局原样保留。
    // 上一版先 setParent(host_) 再由 Section 重复装载，Qt 会报
    // "QLayout: Attempting to add QLayout to QWidget which already has a layout"。
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
        if (auto* section = dynamic_cast<Section*>(item->widget()); section != nullptr) {
            section->ReleaseBody(host_); // 只拆段壳；body 归页面所有，不能销毁
        }
        if (QWidget* w = item->widget(); w != nullptr) {
            w->deleteLater();
        }
        delete item;
    }
}

} // namespace shine::app
