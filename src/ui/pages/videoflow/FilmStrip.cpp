#include "ui/pages/videoflow/FilmStrip.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本控件根（objectName=floatStrip）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:243–262：
//   .float-strip        左下浮动玻璃条：bg-elevated + line-subtle 边 + r-lg(14)
//   .float-strip .film-cell  w118 / r-sm / line-normal 边
//   .float-strip .fthumb     h56
//   .film-cell:hover     边转 accent-glow（kit 侧 = shadow.accent %31）
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#floatStrip {\n"
               "  background-color: %1; border: 1px solid %2; border-radius: 14px; }\n"
               "QWidget#floatStrip *[shineKind=\"filmcell\"]:hover { border-color: %3; }\n"
               "QWidget#floatStrip QLabel[filmRole=\"code\"] { color: %4; font-size: 11px; font-weight: 700; }\n"
               "QWidget#floatStrip QLabel[filmRole=\"meta\"] { color: %5; font-size: 11px; }\n")
        .arg(shine::widget::CssRgb(t.bgElevated), shine::widget::CssRgb(t.lineSubtle),
             shine::widget::CssRgb(t.shadowAccent), shine::widget::CssRgb(t.accentPrimary),
             shine::widget::CssRgb(t.textMuted));
}

} // namespace

FilmStrip::FilmStrip(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("floatStrip"));
    setStyleSheet(PageQss());
    // webui .float-strip：p10 12 / gap 10；横向可滚动
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(theme::space::kSteps[2]);

    row_ = new QHBoxLayout;
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(10); // .float-strip gap 10
    // webui .float-strip 的固定前缀块：胶片图标 + 「成片」两字（flex:none）
    auto* lead = new QWidget;
    auto* lead_lay = new QHBoxLayout(lead);
    lead_lay->setContentsMargins(0, 0, 4, 0);
    lead_lay->setSpacing(theme::space::kSteps[1]);
    auto* lead_icon = new QLabel(QStringLiteral("◈"), lead);
    shine::widgets::SetKind(lead_icon, "stateicon");
    shine::widgets::SetTextColor(lead_icon, theme::Current().accentSecondary);
    auto* lead_text = new QLabel(QStringLiteral("成片"), lead);
    lead_text->setProperty("filmRole", QStringLiteral("code"));
    lead_lay->addWidget(lead_icon);
    lead_lay->addWidget(lead_text);
    lead->setFixedWidth(44);
    row_->addWidget(lead, 0, Qt::AlignTop);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(false);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* holder = new QWidget(scroll_);
    holder->setLayout(row_);
    scroll_->setWidget(holder);
    scroll_->setFixedHeight(112); // 缩略 56 + meta 30 + 上下内距，固定高度避免横向滚动条抖动
    scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    outer->addWidget(scroll_);

    Rebuild();
}

void FilmStrip::SetCells(std::vector<Cell> cells) {
    cells_ = std::move(cells);
    Rebuild();
}

void FilmStrip::Rebuild() {
    // 清空旧格：布局项逐个摘掉后销毁（控件本身由 holder 父级链管）
    // 保留首个前缀块（胶片图标 + 「成片」），其余旧格逐个清掉
    while (row_->count() > 1) {
        QLayoutItem* item = row_->takeAt(1);
        if (item == nullptr) {
            break;
        }
        if (QWidget* cell = item->widget()) cell->deleteLater();
        delete item;
    }
    int ready = 0;
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i) {
        const Cell& cell = cells_[static_cast<std::size_t>(i)];
        ready += cell.ready ? 1 : 0;

        auto* btn = new QPushButton(row_->parentWidget());
        btn->setFlat(true);
        // webui .float-strip .film-cell：w118 / fthumb h56
        btn->setFixedWidth(118);
        btn->setCursor(cell.ready ? Qt::PointingHandCursor : Qt::ArrowCursor);
        shine::widgets::SetKind(btn, "filmcell");
        auto* lay = new QVBoxLayout(btn);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        auto* thumb = new QLabel(btn);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFixedHeight(56); // .float-strip .fthumb height 56
        if (!cell.thumb.isNull()) {
            thumb->setPixmap(QPixmap::fromImage(cell.thumb).scaled(
                118, 56, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        } else {
            // 未出片：显式空态，不复用别的镜头的图
            thumb->setText(cell.ready ? QStringLiteral("无首帧") : QStringLiteral("待出片"));
            thumb->setProperty("filmRole", QStringLiteral("meta"));
        }
        lay->addWidget(thumb);

        // webui .film-cell .fmeta：p6 9 / gap7 / f11；镜号 accent mono，右侧状态点
        auto* meta = new QWidget(btn);
        auto* meta_lay = new QHBoxLayout(meta);
        meta_lay->setContentsMargins(9, 6, 9, 6);
        meta_lay->setSpacing(7);
        auto* code = new QLabel(cell.code, meta);
        code->setProperty("filmRole", QStringLiteral("code"));
        auto* dur = new QLabel(cell.ready ? cell.duration : QStringLiteral("—"), meta);
        dur->setProperty("filmRole", QStringLiteral("meta"));
        auto* dot = new QLabel(cell.ready ? QStringLiteral("●") : QStringLiteral("○"), meta);
        dot->setProperty("filmRole", QStringLiteral("meta"));
        shine::widgets::SetTextColor(
            dot, cell.ready ? theme::Current().statusOk : theme::Current().statusIdle);
        meta_lay->addWidget(code, 0);
        meta_lay->addWidget(dur, 0);
        meta_lay->addStretch(1);
        meta_lay->addWidget(dot, 0);
        lay->addWidget(meta);

        if (cell.ready) {
            connect(btn, &QPushButton::clicked, this, [this, i] {
                if (on_picked_) on_picked_(i);
            });
            btn->setToolTip(QStringLiteral("查看 %1").arg(cell.videoPath));
        } else {
            btn->setEnabled(false);
            btn->setToolTip(QStringLiteral("%1 尚未出片").arg(cell.code));
        }
        row_->addWidget(btn, 0, Qt::AlignTop);
    }
}

int FilmStrip::ReadyCount() const {
    int ready = 0;
    for (const Cell& c : cells_) {
        ready += c.ready ? 1 : 0;
    }
    return ready;
}

QString FilmStrip::Probe() const {
    return QStringLiteral("cells=%1; ready=%2").arg(cells_.size()).arg(ReadyCount());
}

} // namespace shine::app
