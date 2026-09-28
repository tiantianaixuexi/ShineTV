#include "ui/pages/shell/BottomDock.h"

#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Navigation.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

BottomDock::BottomDock(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("bottomDock"));
    setMinimumHeight(120);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[2],
                            theme::space::kSteps[3], theme::space::kSteps[2]);
    lay->setSpacing(theme::space::kSteps[2]);

    tabs_ = new shine::widgets::Tabs({QStringLiteral("任务队列"), QStringLiteral("日志"),
                                      QStringLiteral("产物"), QStringLiteral("校验报告")},
                                     this);
    stack_ = new QStackedWidget(this);
    const QStringList hints = {
        QStringLiteral("任务队列由 P07 接入（Comfy 队列与视频任务）。"),
        QStringLiteral("运行日志由 P09 汇总（logs/ 目录）。"),
        QStringLiteral("产物浏览器指向项目 output/ 目录（图 / 视频 / 成片）。"),
        QStringLiteral("校验报告由 P04-P06 各阶段门禁产出（K / G / C 检查）。"),
    };
    for (const QString& hint : hints) {
        auto* page = new QLabel(hint, stack_);
        page->setWordWrap(true);
        stack_->addWidget(page);
    }
    tabs_->SetOnChanged([this](int i) {
        stack_->setCurrentIndex(i);
        if (on_tab_changed_) {
            on_tab_changed_(i);
        }
    });

    lay->addWidget(tabs_);
    lay->addWidget(stack_, 1);
}

void BottomDock::SetCurrentTab(int index) {
    if (index >= 0 && index < stack_->count()) {
        tabs_->SetCurrent(index, false);
        stack_->setCurrentIndex(index);
    }
}

int BottomDock::CurrentTab() const {
    return stack_ != nullptr ? stack_->currentIndex() : 0;
}

void BottomDock::SetOnTabChanged(std::function<void(int)> cb) {
    on_tab_changed_ = std::move(cb);
}

} // namespace shine::app
