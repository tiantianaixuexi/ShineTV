#include "ui/pages/shell/BottomDock.h"

#include "core/Log.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Navigation.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace shine::app {

namespace {

// 日志行的等级配色（webui shell.css:429-434 .logline.ok/.warn/.err）。
// 亮色主题下 status.* 已经是对比度足够的实色，直接拿来当字色。
[[nodiscard]] std::uint32_t LogLevelColor(int level) {
    const theme::ColorToken& t = theme::Current();
    if (level >= 2) return t.statusDanger; // err
    if (level == 1) return t.statusWarn;   // warn
    return t.textSecondary;                // 普通行
}

} // namespace

BottomDock::BottomDock(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("bottomDock"));
    // .dock height 200px（shell.css:373-381），给 120 作下限供折叠动画过渡
    setMinimumHeight(120);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // .dock-tabs：p 5px 10px 0 + 底部细线（shell.css:382-388）
    auto* tabRow = new QFrame(this);
    tabRow->setObjectName(QStringLiteral("dockTabs"));
    auto* trl = new QHBoxLayout(tabRow);
    trl->setContentsMargins(10, 5, 10, 0);
    trl->setSpacing(2);

    tabs_ = new shine::widgets::Tabs({QStringLiteral("任务队列"), QStringLiteral("日志"),
                                      QStringLiteral("产物"), QStringLiteral("校验报告")},
                                     tabRow);
    trl->addWidget(tabs_, 1);

    // .dock-close：标签行右端的收起按钮（shell.css:411-414）
    close_ = new shine::widgets::IconButton(QStringLiteral("✕"),
                                            QStringLiteral("收起 · Ctrl+J"),
                                            shine::widgets::IconButton::Size::Sm, tabRow);
    close_->setFixedSize(22, 22);
    trl->addWidget(close_, 0, Qt::AlignRight | Qt::AlignBottom);
    connect(close_, &shine::widgets::IconButton::clicked, this, [this] {
        if (on_close_) on_close_();
    });
    lay->addWidget(tabRow);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("dockBody"));
    const QStringList hints = {
        QStringLiteral("任务队列由 P07 接入（Comfy 队列与视频任务）。"),
        QStringLiteral("运行日志由 P09 汇总（logs/ 目录）。"),
        QStringLiteral("产物浏览器指向项目 output/ 目录（图 / 视频 / 成片）。"),
        QStringLiteral("校验报告由 P04-P06 各阶段门禁产出（K / G / C 检查）。"),
    };
    for (int i = 0; i < hints.size(); ++i) {
        if (i == 1) {
            // 「日志」页接真实日志缓冲（shine::log），不是占位文案
            stack_->addWidget(MakeLogView());
            continue;
        }
        auto* page = new QLabel(hints[i], stack_);
        page->setWordWrap(true);
        page->setObjectName(QStringLiteral("dockBodyLabel"));
        page->setContentsMargins(14, 10, 14, 10);
        stack_->addWidget(page);
    }

    tabs_->SetOnChanged([this](int i) {
        stack_->setCurrentIndex(i);
        if (on_tab_changed_) {
            on_tab_changed_(i);
        }
    });
    lay->addWidget(stack_, 1);
}

QWidget* BottomDock::MakeLogView() {
    auto* scroll = new QScrollArea(stack_);
    scroll->setObjectName(QStringLiteral("logScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* box = new QWidget(scroll);
    auto* bl = new QVBoxLayout(box);
    // .dock-body：p 10px 14px / f12（shell.css:404-410）
    bl->setContentsMargins(14, 10, 14, 10);
    bl->setSpacing(0); // .logline 上下各 1.5px ≈ 3px 间距由 QLabel 自身高留出
    bl->addStretch();
    scroll->setWidget(box);

    // 日志缓冲在别的线程写；这里按 Version() 轮询增量追加，不做任何同步 IO。
    // 只在「日志」页可见时刷新（其它页不白跑）。
    //
    // ⚠️ seen / primed 必须是**静态局部**：定时器的 lambda 活得比本函数栈帧长，
    // 捕获局部变量引用会在 MakeLogView 返回后悬空（第一版就是这么写的）。
    static std::uint64_t seen = 0;
    static bool primed = false;
    auto* timer = new QTimer(scroll);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, scroll, [this, scroll, box, bl] {
        if (stack_->currentWidget() != scroll) {
            return;
        }
        const std::uint64_t ver = shine::log::Version();
        if (primed && ver == seen) {
            return;
        }
        const auto lines = shine::log::LinesSnapshot();
        seen = ver;
        // 首帧直接铺满（历史日志一次性显示），之后只补新增行
        const std::size_t have = static_cast<std::size_t>(bl->count() - 1);
        if (!primed || lines.size() < have) {
            for (const shine::log::Line& l : lines) {
                AppendLogLine(bl, l);
            }
        } else {
            for (std::size_t i = have; i < lines.size(); ++i) {
                AppendLogLine(bl, lines[i]);
            }
        }
        primed = true;
        // 自动跟随：本来就在底部才继续滚，否则用户往上翻历史时不要把他拽回来
        auto* bar = scroll->verticalScrollBar();
        if (bar != nullptr && bar->value() >= bar->maximum() - 4) {
            bar->setValue(bar->maximum());
        }
    });
    timer->start();
    return scroll;
}

void BottomDock::AppendLogLine(QVBoxLayout* lay, const shine::log::Line& line) {
    // .logline：mono 字体 / f11.5→11px / 时间与正文之间 gap 10（shell.css:417-434）。
    // 日志缓冲里没有时间戳字段（core 只存 level + text），所以只渲染正文，
    // 不编造时间 —— 设计稿的 `.t` 列需要日志层补字段后再对。
    auto* l = new QLabel(QString::fromStdString(line.text), lay->parentWidget());
    l->setObjectName(QStringLiteral("logline"));
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(false);
    l->setContentsMargins(0, 2, 0, 2); // .logline padding 1.5px 0
    QFont f = l->font();
    f.setFamilies({QString::fromUtf8(theme::font::kMonoFamily.data())});
    f.setPixelSize(11);
    l->setFont(f);
    shine::widgets::SetTextColor(l, LogLevelColor(line.level));
    // 插到末尾 stretch 之前
    lay->insertWidget(lay->count() - 1, l);
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

void BottomDock::SetOnClose(std::function<void()> cb) {
    on_close_ = std::move(cb);
}

} // namespace shine::app
