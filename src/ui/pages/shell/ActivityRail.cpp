#include "ui/pages/shell/ActivityRail.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVariant>
#include <QVBoxLayout>

namespace shine::app {

namespace {
// 六个入口（UI.md §2.3）：图标为字符图标（IconButton 约定，P03 不引外部图标资源）
struct RailEntry {
    const char* icon;
    const char* tooltip;
};
inline constexpr RailEntry kEntries[] = {
    {"⌂", "总控 —— 全流程总控台"},   {"📖", "小说 —— 章节与设定"},
    {"🎭", "资产 —— 视觉资产库"},   {"🎬", "分镜 —— 故事板与镜头表"},
    {"🖼", "出图 —— ComfyUI 出图"}, {"🎞", "出片 —— 视频与成片"},
};
inline constexpr double kIndicatorW = 2.5; // UI.md §2.3：2.5px 指示条
} // namespace

const QStringList& WorkspaceNames() {
    // 顺序 = kEntries 顺序 = ActivityRail 下标；改这里等于改工作区数量
    static const QStringList kNames = {QStringLiteral("总控"), QStringLiteral("小说"),
                                       QStringLiteral("视觉资产"), QStringLiteral("分镜"),
                                       QStringLiteral("出图"), QStringLiteral("出片")};
    return kNames;
}

ActivityRail::ActivityRail(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("activityRail"));
    setFixedWidth(56);

    auto* lay = new QVBoxLayout(this);
    // webui shell.css:157-168 .rail：w56 / gap 4 / padding 10px 0 / 右细线
    lay->setContentsMargins(0, 10, 0, 10);
    lay->setSpacing(4);
    for (const RailEntry& e : kEntries) {
        auto* btn = new shine::widgets::IconButton(QString::fromUtf8(e.icon),
                                                   QString::fromUtf8(e.tooltip),
                                                   shine::widgets::IconButton::Size::Md, this);
        btn->setFixedSize(40, 40); // .rail-btn 40×40（r-md 由 kit IconButton 承担）
        const int index = static_cast<int>(items_.size());
        connect(btn, &shine::widgets::IconButton::clicked, this, [this, index] { Select(index, true); });
        items_.push_back(btn);
        lay->addWidget(btn, 0, Qt::AlignHCenter);
    }

    // .rail-sep：24×1 细线，上下各留 5（shell.css:203-208）
    auto* sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("railSep"));
    sep->setFixedSize(24, 1);
    lay->addSpacing(5);
    lay->addWidget(sep, 0, Qt::AlignHCenter);
    lay->addSpacing(5);

    // 三个面板开关（shell.css:100-108）：侧栏 / 底栏 / 检查器
    struct PanelEntry {
        const char* icon;
        const char* tooltip;
        const char* shortcut;
    };
    constexpr PanelEntry kPanels[] = {
        {"◧", "侧栏 · 显示 / 隐藏左侧导航", "Ctrl+B"},
        {"▤", "任务队列 / 日志 · 显示 / 隐藏底栏", "Ctrl+J"},
        {"◨", "检查器 · 显示 / 隐藏右侧属性", "Ctrl+I"},
    };
    for (const PanelEntry& p : kPanels) {
        auto* btn = new shine::widgets::IconButton(QString::fromUtf8(p.icon),
                                                   QString::fromUtf8(p.tooltip),
                                                   shine::widgets::IconButton::Size::Md, this);
        btn->setFixedSize(40, 40);
        btn->setToolTip(QStringLiteral("%1（%2）").arg(QString::fromUtf8(p.tooltip),
                                                      QString::fromUtf8(p.shortcut)));
        const int slot = static_cast<int>(panels_.size());
        // 回调在构造后才由外壳注入（SetOnToggle*），所以这里转发一层成员函数而不是捕获局部
        connect(btn, &shine::widgets::IconButton::clicked, this, [this, slot] {
            if (slot == 0 && on_toggle_side_) on_toggle_side_();
            if (slot == 1 && on_toggle_dock_) on_toggle_dock_();
            if (slot == 2 && on_toggle_inspector_) on_toggle_inspector_();
        });
        panels_.push_back(btn);
        lay->addWidget(btn, 0, Qt::AlignHCenter);
    }

    lay->addStretch();
    items_[current_]->SetActive(true);
}

void ActivityRail::SetPanelActive(bool side, bool dock, bool inspector) {
    if (panels_.size() >= 3) {
        panels_[0]->SetActive(side);
        panels_[1]->SetActive(dock);
        panels_[2]->SetActive(inspector);
    }
}

void ActivityRail::SetCurrent(int index, bool animated) {
    if (index < 0 || index >= static_cast<int>(items_.size()) || index == current_) {
        return;
    }
    Select(index, animated);
}

void ActivityRail::Select(int index, bool animated) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    items_[current_]->SetActive(false);
    current_ = index;
    items_[current_]->SetActive(true);

    const double targetY = IndicatorTopFor(index);
    if (tween_ == nullptr) {
        tween_ = new shine::motion::Tween(theme::motion::kStandard, this);
    }
    const double from = indicator_y_;
    tween_->Run(from, targetY, theme::motion::kDurBaseMs,
                [this](const QVariant& v) {
                    indicator_y_ = v.toDouble();
                    update();
                });
    if (!animated || shine::motion::ReduceMotion()) {
        tween_->Settle();
    }
    if (on_changed_) {
        on_changed_(current_);
    }
}

double ActivityRail::IndicatorTopFor(int index) const {
    // webui shell.css:192-202：指示条贴在 40px 按钮的左缘外侧 8px，
    // 上下各缩进 9px（即 22px 高），与按钮垂直居中对齐。
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return indicator_y_;
    }
    return items_[index]->y() + items_[index]->height() / 2.0 - 11.0;
}

void ActivityRail::paintEvent(QPaintEvent* ev) {
    QFrame::paintEvent(ev);
    QPainter p(this);
    // 指示条：2.5px × 22px，accent.primary（Token 取色，零字面色）；圆角 3px（shell.css:199）
    const QRectF bar(0.0, indicator_y_, kIndicatorW, 22.0);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(shine::widgets::TokenQColor(theme::Current().accentPrimary));
    p.drawRoundedRect(bar, 3.0, 3.0);
}

void ActivityRail::resizeEvent(QResizeEvent* ev) {
    QFrame::resizeEvent(ev);
    if (indicator_y_ == 0.0 && !items_.empty()) {
        indicator_y_ = IndicatorTopFor(current_);
    }
}

} // namespace shine::app
