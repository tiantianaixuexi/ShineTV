#include "ui/pages/videoflow/ChainView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include <array>
#include "ui/layout/QtLayout.h"

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=chainView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:290–320 与 VideoFlow.jsx ChainList：
//   .drow        p8 2 + 底部发丝线 + hover fill.hover
//   镜号        mono 11px accent + w700（两端都是）
//   策略        tiny dim 单行省略
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#chainView *[shineKind=\"drow\"]:hover { background-color: %1; }\n"
               "QWidget#chainView QLabel[rowRole=\"code\"] { color: %2; font-size: 11px; font-weight: 700; }\n"
               "QWidget#chainView QLabel[rowRole=\"chev\"] { color: %3; font-size: 10px; }\n"
               "QWidget#chainView QLabel[rowRole=\"dim\"] { color: %3; font-size: 11px; }\n")
        .arg(shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.accentPrimary),
             shine::widget::CssRgb(t.textMuted));
}


} // namespace

ChainView::ChainView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chainView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);

    // 密集行列表（webui .dlist）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_ = new QWidget(scroll);
    list_lay_ = new QVBoxLayout(list_);
    list_lay_->setContentsMargins(0, 0, 0, 0);
    list_lay_->setSpacing(0);
    list_lay_->addStretch(1);
    scroll->setWidget(list_);
    layout->addWidget(scroll, 1);

    status_ = new QLabel(QStringLiteral("暂无链式关系"), this);
    widgets::SetKind(status_, "statemeta");
    layout->addWidget(status_);
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
}

void ChainView::SetChain(flow::VideoChain chain) {
    chain_ = std::move(chain);
    Rebuild();
}

void ChainView::Rebuild() {
    util::ClearLayout(list_lay_, 1); // 末尾常驻 addStretch(1)，保留
    for (std::size_t i = 0; i < chain_.links.size(); ++i) {
        const auto& link = chain_.links[i];
        const bool last = i + 1 == chain_.links.size();
        auto* row = new QWidget(list_);
        widgets::SetKind(row, "drow");
        if (last) row->setProperty("shineKind", QString{});
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        lay->setSpacing(theme::space::kSteps[2]);

        auto* from = new QLabel(QStringLiteral("S%1").arg(link.from_shot), row);
        from->setProperty("rowRole", QStringLiteral("code"));
        from->setFixedWidth(38);
        auto* chev = new QLabel(QStringLiteral("›"), row);
        chev->setProperty("rowRole", QStringLiteral("chev"));
        auto* to = new QLabel(QStringLiteral("S%1").arg(link.to_shot), row);
        to->setProperty("rowRole", QStringLiteral("code"));
        to->setFixedWidth(38);
        // 策略 + 建议：connected 时只给策略，断链时补一句「建议重生成首帧」
        const QString policy = QString::fromStdString(link.strategy) +
                               (link.connected ? QString()
                                               : QStringLiteral(" · 建议按上一镜末帧重生成首帧"));
        auto* note = new widgets::ElidedLabel(policy, row);
        note->setProperty("rowRole", QStringLiteral("dim"));
        note->SetExpandable(false);
        note->setToolTip(QString::fromStdString(link.detail));
        auto* tag = new widgets::Tag(
            link.connected ? QStringLiteral("✔ 已连接") : QStringLiteral("⚠ 断链"),
            link.connected ? "ok" : "warn", false, row);
        lay->addWidget(from, 0);
        lay->addWidget(chev, 0);
        lay->addWidget(to, 0);
        lay->addWidget(note, 1);
        lay->addWidget(tag, 0);
        list_lay_->insertWidget(list_lay_->count() - 1, row);
    }
    status_->setText(QString::fromStdString(chain_.Describe()));
}

QString ChainView::Probe() const {
    return QString::fromStdString(chain_.Describe());
}

} // namespace shine::app
