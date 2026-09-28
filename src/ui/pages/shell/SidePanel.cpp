#include "ui/pages/shell/SidePanel.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/theme/Token.h"

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

SidePanel::SidePanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("sidePanel"));
    // webui shell.css:572-579 .sidepanel：w240 / p12 10 / overflow-y auto
    setMinimumWidth(240);
    setMaximumWidth(460);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 12, 10, 12);
    lay->setSpacing(8);

    // 标题行：当前工作区名（与活动栏一致），让用户知道侧栏属于谁
    auto* head = new QWidget(this);
    auto* head_lay = new QHBoxLayout(head);
    head_lay->setContentsMargins(0, 0, 0, 0);
    caption_ = new QLabel(QStringLiteral("导航"), head);
    shine::widgets::SetKind(caption_, "statetitle");
    shine::widgets::SetSemibold(caption_, true);
    head_lay->addWidget(caption_);
    head_lay->addStretch();
    lay->addWidget(head);

    stack_ = new QStackedWidget(this);

    // 空态：当前工作区没有导航时唯一可见内容。不写占位路线图文案。
    empty_ = new shine::widgets::EmptyState(QStringLiteral("☰"), QStringLiteral("本工作区无导航"),
                                            QStringLiteral("导航会显示在这里"), QString{}, stack_);
    empty_lay_ = nullptr;
    empty_text_ = nullptr;
    stack_->addWidget(empty_);

    // 导航容器：页面 AdoptNav() 借进来的 widget 挂这里
    host_ = new QWidget(stack_);
    host_lay_ = new QVBoxLayout(host_);
    host_lay_->setContentsMargins(0, 0, 0, 0);
    host_lay_->setSpacing(theme::space::kSteps[2]);
    host_lay_->addStretch();
    stack_->addWidget(host_);

    lay->addWidget(stack_, 1);
}

QWidget* SidePanel::TakeCurrentNav() {
    while (host_lay_->count() > 1) { // 保留末尾 stretch
        QLayoutItem* item = host_lay_->takeAt(0);
        if (QWidget* w = item->widget(); w != nullptr) {
            delete item;
            return w;
        }
        delete item;
    }
    return nullptr;
}

namespace {

// 把导航挂回页面的分栏容器第一格。宿主的布局可能是 QSplitter（QBoxLayout）
// 或 QVBoxLayout，两者的插入 API 不同，这里按实际类型分派。
void InsertAtFront(QWidget* box, QWidget* nav) {
    if (box == nullptr) {
        nav->setParent(nullptr);
        nav->hide();
        return;
    }
    if (auto* box_lay = qobject_cast<QBoxLayout*>(box->layout()); box_lay != nullptr) {
        box_lay->insertWidget(0, nav);
        nav->show();
        return;
    }
    if (auto* lay = box->layout(); lay != nullptr) {
        lay->addWidget(nav);
        nav->show();
        return;
    }
    nav->setParent(box);
    nav->hide();
}

} // namespace

void SidePanel::AdoptNav(const QString& workspace, QWidget* nav, QWidget* hostBox,
                         QWidget* originPage) {
    if (nav == nullptr) {
        return;
    }
    // 换工作区：先把上一个导航还给它自己的页面（不销毁，页面仍持有指针）
    if (QWidget* old = TakeCurrentNav(); old != nullptr) {
        old->setParent(this);
        old->hide();
    }
    // 从页面原布局里摘出来再挂进侧栏：Qt 会自动 reparent，
    // 但页面那边仍有一份布局项会跟着空指针走，所以先 removeWidget。
    if (auto* owner = nav->parentWidget(); owner != nullptr) {
        if (auto* owner_lay = owner->layout(); owner_lay != nullptr) {
            owner_lay->removeWidget(nav);
        }
    }
    origin_box_ = hostBox;
    origin_page_ = originPage;
    nav->setParent(host_);
    nav->show();
    host_lay_->insertWidget(host_lay_->count() - 1, nav);
    stack_->setCurrentWidget(host_);
    active_ = workspace;
    caption_->setText(workspace);
}

void SidePanel::ReturnNavTo(QWidget* box) {
    QWidget* nav = TakeCurrentNav();
    if (nav == nullptr) {
        stack_->setCurrentWidget(empty_);
        return;
    }
    // 导航的所有权始终在页面：这里只把父级还回去，绝不销毁它。
    // box 非空 = 明确指定宿主；box 为空 = 用借出时记录的页面宿主盒
    //（页面即将销毁时，MainWindow 先调它把导航挂回页面，再销毁页面 ——
    //  否则导航会变成侧栏的子控件，页面 deleteLater 收不到它，直接泄漏）。
    QWidget* target = box != nullptr ? box : origin_box_;
    InsertAtFront(target, nav);
    origin_box_ = nullptr;
    origin_page_ = nullptr;
    stack_->setCurrentWidget(empty_);
    active_.clear();
    caption_->setText(QStringLiteral("导航"));
}

void SidePanel::ClearNav() {
    // 清空侧栏但**保留**页面侧的导航控件：挂回原宿主盒的第一格，
    // 页面下次被切回来时布局仍是完整两列（导航 + 内容）。
    QWidget* nav = TakeCurrentNav();
    InsertAtFront(nav != nullptr ? origin_box_ : nullptr, nav);
    origin_box_ = nullptr;
    origin_page_ = nullptr;
    stack_->setCurrentWidget(empty_);
    active_.clear();
    caption_->setText(QStringLiteral("导航"));
}

void SidePanel::SetActiveWorkspace(const QString& workspace) {
    if (workspace == active_) {
        return;
    }
    ClearNav();
}

QWidget* SidePanel::CurrentNav() const {
    if (host_lay_->count() <= 1) {
        return nullptr; // 只有末尾 stretch = 没借导航
    }
    if (auto* item = host_lay_->itemAt(0); item != nullptr) {
        return item->widget();
    }
    return nullptr;
}

bool SidePanel::HasNav() const {
    return stack_->currentWidget() == host_;
}

} // namespace shine::app
