#include "ui/pages/imageflow/ComfyPanel.h"

#include "comfy/ComfySession.h"
#include "comfy/ComfySocket.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace shine::app {

ComfyPanel::ComfyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kXs); // webui .fp-f 两行之间 gap 6

    // 第一行（views.css ImageFlow.jsx fp-f 第一行）：状态点 + 强文案 + 右对齐 mono 地址
    auto* row1 = new QHBoxLayout;
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(theme::space::kSteps[2]);
    state_ = new QLabel(QStringLiteral("○ 未连接"), this);
    widgets::SetKind(state_, "statestatus");
    url_ = new QLabel(QStringLiteral("127.0.0.1:8188"), this);
    url_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    widgets::SetKind(url_, "statemeta");
    row1->addWidget(state_, 1);
    row1->addWidget(url_, 0);
    layout->addLayout(row1);

    // 第二行：队列信息（tiny dim）+ 右侧按钮组
    auto* row2 = new QHBoxLayout;
    row2->setContentsMargins(0, 0, 0, 0);
    row2->setSpacing(theme::space::kXs);
    queue_ = new QLabel(QStringLiteral("队列剩余 0 · 运行 0 · 失败 0"), this);
    widgets::SetKind(queue_, "statemeta");
    row2->addWidget(queue_, 1);
    auto* connect_button = new widgets::Button(QStringLiteral("连接"), widgets::Button::Variant::Secondary,
                                              widgets::Button::Size::Sm, this);
    auto* free_button = new widgets::Button(QStringLiteral("释放 VRAM"), widgets::Button::Variant::Ghost,
                                            widgets::Button::Size::Sm, this);
    row2->addWidget(connect_button, 0);
    row2->addWidget(free_button, 0);
    layout->addLayout(row2);

    // 忙碌 / 健康细节（节点统计、距上次事件）：小字两行，与 fp-f 的 tiny dim 同档
    detail_ = new QLabel(QStringLiteral("等待连接探活"), this);
    widgets::SetKind(detail_, "statemeta");
    layout->addWidget(detail_);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(theme::space::kXs);
    auto* refresh_button = new widgets::Button(QStringLiteral("刷新"), widgets::Button::Variant::Ghost,
                                               widgets::Button::Size::Sm, this);
    auto* interrupt_button = new widgets::Button(QStringLiteral("中断"), widgets::Button::Variant::Ghost,
                                                 widgets::Button::Size::Sm, this);
    actions->addWidget(refresh_button);
    actions->addWidget(interrupt_button);
    actions->addStretch(1);
    layout->addLayout(actions);

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
    // 右上角显示的地址就是实际 base url（剥掉 http:// 前缀，与 webui 的 mono 短址一致）
    QString shown = url;
    if (shown.startsWith(QStringLiteral("http://"))) shown = shown.mid(7);
    if (shown.endsWith(QLatin1Char('/'))) shown.chop(1);
    url_->setText(shown);
    Update();
}


void ComfyPanel::SetDemoBusy(bool busy) {
    demo_busy_ = busy;
    if (busy) {
        state_->setText(QStringLiteral("● 已连接 · 执行中"));
        widgets::SetTextColor(state_, theme::Current().statusOk);
        detail_->setText(QStringLiteral("节点目录已加载 · 距上次事件 2s"));
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
    // 状态四态取 status.* token：已连接=ok / 连接中=busy / 错误=danger / 未连接=warn
    state_->setText(QStringLiteral("%1 %2")
                        .arg(state == comfy::ConnectionState::Connected ? QStringLiteral("● 已连接")
                             : state == comfy::ConnectionState::Connecting ? QStringLiteral("◐ 连接中")
                             : state == comfy::ConnectionState::Error ? QStringLiteral("● 连接错误")
                                                                        : QStringLiteral("○ ComfyUI 未连接"))
                        .arg(health));
    widgets::SetTextColor(state_, state == comfy::ConnectionState::Connected ? theme::Current().statusOk
                             : state == comfy::ConnectionState::Connecting ? theme::Current().statusBusy
                             : state == comfy::ConnectionState::Error ? theme::Current().statusDanger
                                                                        : theme::Current().statusWarn);
    // 节点统计走 object_info 真实目录大小（webui ComfyPanel 的「节点 4/9」同义）
    detail_->setText(QStringLiteral("节点目录 %1 个 · 忙碌：%2 · 距上次事件 %3ms")
                         .arg(session.ObjectInfoNodeCount())
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
