#include "ui/pages/shell/SidePanel.h"

#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

const QStringList& SidePanel::WorkspaceNames() {
    static const QStringList kNames = {QStringLiteral("总控"), QStringLiteral("小说"),
                                       QStringLiteral("视觉资产"), QStringLiteral("分镜"),
                                       QStringLiteral("出图"), QStringLiteral("出片")};
    return kNames;
}

SidePanel::SidePanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("sidePanel"));
    setMinimumWidth(180);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[4], theme::space::kSteps[3],
                            theme::space::kSteps[4], theme::space::kSteps[3]);
    lay->setSpacing(theme::space::kSteps[3]);

    title_ = new QLabel(WorkspaceNames().first(), this);
    shine::widgets::SetSemibold(title_, true);
    lay->addWidget(title_);

    stack_ = new QStackedWidget(this);
    for (int i = 0; i < WorkspaceNames().size(); ++i) {
        auto* page = new QWidget(stack_);
        auto* pl = new QVBoxLayout(page);
        pl->setContentsMargins(0, theme::space::kSteps[2], 0, 0);
        pl->setSpacing(theme::space::kSteps[2]);
        auto* lead = new QLabel(QStringLiteral("侧栏 · %1 面板").arg(WorkspaceNames()[i]), page);
        shine::widgets::SetSemibold(lead, true);
        auto* body = new QLabel(QStringLiteral("本阶段（P03）只搭舞台：七区布局、分栏与持久化。\n"
                                               "该面板的内容由后续阶段填充——\n"
                                               "小说 P04 · 资产 P05 · 分镜 P06 · 出图 P07 · 出片 P08 · 总控 P09。"),
                                page);
        body->setWordWrap(true);
        pl->addWidget(lead);
        pl->addWidget(body);
        pl->addStretch();
        stack_->addWidget(page);
    }
    lay->addWidget(stack_, 1);
}

void SidePanel::SetWorkspace(int index) {
    if (index < 0 || index >= stack_->count()) {
        return;
    }
    current_ = index;
    stack_->setCurrentIndex(index);
    title_->setText(WorkspaceNames()[index]);
}

} // namespace shine::app
