#include "pages/imageflow/ComfyPanel.h"

#include "comfy/ComfySession.h"
#include "comfy/ComfySocket.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace shine::app {

ComfyPanel::ComfyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("ComfyUI · 连接与健康"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    layout->addWidget(title);
    state_ = new QLabel(QStringLiteral("● 未连接"), this);
    widgets::SetKind(state_, "statestatus");
    layout->addWidget(state_);
    detail_ = new QLabel(QStringLiteral("等待连接探活"), this);
    detail_->setWordWrap(true);
    widgets::SetKind(detail_, "statedetail");
    layout->addWidget(detail_);
    queue_ = new QLabel(QStringLiteral("队列 0 · 运行 0"), this);
    widgets::SetKind(queue_, "statemeta");
    layout->addWidget(queue_);
    auto* buttons = new QHBoxLayout;
    auto* connect_button = new widgets::Button(QStringLiteral("连接"), widgets::Button::Variant::Primary,
                                              widgets::Button::Size::Sm, this);
    auto* refresh_button = new widgets::Button(QStringLiteral("刷新"), widgets::Button::Variant::Secondary,
                                               widgets::Button::Size::Sm, this);
    auto* interrupt_button = new widgets::Button(QStringLiteral("中断"), widgets::Button::Variant::Danger,
                                                 widgets::Button::Size::Sm, this);
    auto* free_button = new widgets::Button(QStringLiteral("释放 VRAM"), widgets::Button::Variant::Ghost,
                                            widgets::Button::Size::Sm, this);
    buttons->addWidget(connect_button);
    buttons->addWidget(refresh_button);
    buttons->addWidget(interrupt_button);
    buttons->addWidget(free_button);
    layout->addLayout(buttons);
    connect(connect_button, &QPushButton::clicked, this, [this] {
        auto& session = comfy::ComfySession::Instance();
        session.SetBaseUrl(base_url_.toStdString());
        session.Connect();
        Update();
    });
    connect(refresh_button, &QPushButton::clicked, this, &ComfyPanel::Refresh);
    connect(interrupt_button, &QPushButton::clicked, this, [this] {
        comfy::ComfySession::Instance().RequestInterruptCurrent();
        Update();
    });
    connect(free_button, &QPushButton::clicked, this, [this] {
        comfy::ComfySession::Instance().FreeVram([this](comfy::OperationResult result) {
            detail_->setText(result.ok ? QStringLiteral("VRAM 已释放") : QString::fromStdString(result.error));
        });
    });
    auto* timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, &ComfyPanel::Update);
    timer->start();
    Update();
}

void ComfyPanel::SetBaseUrl(const QString& url) {
    base_url_ = url;
    Update();
}


void ComfyPanel::SetDemoBusy(bool busy) {
    demo_busy_ = busy;
    if (busy) {
        state_->setText(QStringLiteral("◐ 已连接 · 执行中"));
        detail_->setText(QStringLiteral("节点 4/9 · 队列剩余 3 · 距上次事件 2s"));
        queue_->setText(QStringLiteral("队列剩余 3 · 运行 1 · 失败 0"));
    } else {
        Update();
    }
}
void ComfyPanel::Refresh() {
    comfy::ComfySession::Instance().Connect();
    comfy::ComfySession::Instance().RefreshQueue();
    comfy::ComfySession::Instance().RefreshObjectInfo();
    Update();
}

void ComfyPanel::Update() {
    if (demo_busy_) return;
    auto& session = comfy::ComfySession::Instance();
    const auto state = session.State();
    const QString health = QString::fromStdString(std::string{session.HealthSummary()});
    const auto counts = session.Queue().CountsSnapshot();
    state_->setText(QStringLiteral("%1 %2")
                        .arg(state == comfy::ConnectionState::Connected ? QStringLiteral("● 已连接")
                             : state == comfy::ConnectionState::Connecting ? QStringLiteral("◐ 连接中")
                             : state == comfy::ConnectionState::Error ? QStringLiteral("● 连接错误")
                                                                        : QStringLiteral("○ 未连接"))
                        .arg(health));
    detail_->setText(QStringLiteral("忙碌：%1 · 距上次事件 %2ms")
                         .arg(QString::fromStdString(comfy::BusyStateLabel(session.Busy())))
                         .arg(session.SinceLastEvent().count()));
    queue_->setText(QStringLiteral("队列剩余 %1 · 运行 %2 · 失败 %3")
                        .arg(session.Queue().QueueRemaining())
                        .arg(counts.running)
                        .arg(counts.failed));
}

QString ComfyPanel::Probe() const {
    auto& session = comfy::ComfySession::Instance();
    return QStringLiteral("state=%1; busy=%2; health=%3; queue=%4")
        .arg(comfy::ConnectionStateLabel(session.State()))
        .arg(comfy::BusyStateLabel(session.Busy()))
        .arg(QString::fromStdString(std::string{session.HealthSummary()}))
        .arg(session.Queue().QueueRemaining());
}

} // namespace shine::app
