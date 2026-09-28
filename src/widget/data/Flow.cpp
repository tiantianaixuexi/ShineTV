#include "widget/data/Flow.h"

#include "widget/motion/Easing.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace shine::data {

// ================================================================== Timeline

Timeline::Timeline(Qt::Orientation o, QWidget* parent)
    : QFrame(parent), orientation_(o) {
    widgets::SetKind(this, "field");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll_);
}

void Timeline::SetEvents(std::vector<Event> events) {
    events_ = std::move(events);
    Rebuild();
}

void Timeline::SetCurrent(int index) {
    current_ = index;
    Rebuild();
}

void Timeline::Rebuild() {
    auto* host = new QWidget();
    const bool vert = orientation_ == Qt::Vertical;
    QBoxLayout* lay = vert ? static_cast<QBoxLayout*>(new QVBoxLayout(host))
                           : static_cast<QBoxLayout*>(new QHBoxLayout(host));
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(10);

    for (int i = 0; i < static_cast<int>(events_.size()); ++i) {
        const Event& e = events_[static_cast<std::size_t>(i)];
        auto* card = new shine::widgets::Card(
            i == current_ ? shine::widgets::Card::Variant::Elevated : shine::widgets::Card::Variant::Outlined,
            host);
        card->SetAccent(i == current_); // 当前项高亮
        auto* title = new QLabel(e.title, card);
        shine::widgets::SetKind(title, "statetitle");
        shine::widgets::SetSemibold(title, i == current_);
        card->BodyLayout()->addWidget(title);
        auto* meta = new QLabel(e.time + (e.detail.isEmpty() ? QString{} : QStringLiteral(" · ") + e.detail), card);
        shine::widgets::SetKind(meta, "statesub");
        card->BodyLayout()->addWidget(meta);
        card->setMinimumWidth(vert ? 320 : 200);
        card->SetOnClick([this, i] {
            if (on_pick_) {
                on_pick_(i);
            }
        });
        lay->addWidget(card);
    }
    lay->addStretch(1);

    QWidget* old = scroll_->takeWidget();
    if (old != nullptr) {
        old->deleteLater();
    }
    scroll_->setWidget(host);
}

// ================================================================== StageFlow

namespace {

struct Paint {
    theme::ColorToken t;
};

} // namespace

StageFlow::StageFlow(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setMouseTracking(true);
}

void StageFlow::SetGraph(std::vector<Node> nodes, std::vector<Link> links) {
    nodes_ = std::move(nodes);
    links_ = std::move(links);
    updateGeometry();
    update();
}

void StageFlow::SetNodeState(const QString& id, NodeState s) {
    for (Node& n : nodes_) {
        if (n.id == id) {
            n.state = s;
            update();
            return;
        }
    }
}

StageFlow::NodeState StageFlow::StateOf(const QString& id) const {
    for (const Node& n : nodes_) {
        if (n.id == id) {
            return n.state;
        }
    }
    return NodeState::Todo;
}

QSize StageFlow::sizeHint() const {
    return {static_cast<int>(nodes_.size()) * 172 + 24, 156};
}

QRectF StageFlow::BoxOf(std::size_t i) const {
    const Node& n = nodes_[i];
    const double x = 12.0 + static_cast<double>(i) * 172.0;
    const double y = n.branchOf.isEmpty() ? 46.0 : 108.0; // 支线下沉一行
    return {x, y, 148, 44};
}

QString StageFlow::NodeAt(const QPointF& pos) const {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (BoxOf(i).contains(pos)) {
            return nodes_[i].id;
        }
    }
    return QString{};
}

void StageFlow::mousePressEvent(QMouseEvent* ev) {
    const QString id = NodeAt(ev->position());
    if (!id.isEmpty() && on_pick_) {
        on_pick_(id);
    }
}

void StageFlow::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const theme::ColorToken& t = theme::Current();

    // 连线（主干水平；支线折线）
    QPen linkPen{widgets::TokenQColor(t.lineNormal), 1.5};
    p.setPen(linkPen);
    for (const Link& l : links_) {
        const auto fi = std::find_if(nodes_.begin(), nodes_.end(),
                                     [&](const Node& n) { return n.id == l.from; });
        const auto ti = std::find_if(nodes_.begin(), nodes_.end(),
                                     [&](const Node& n) { return n.id == l.to; });
        if (fi == nodes_.end() || ti == nodes_.end()) {
            continue;
        }
        const QRectF a = BoxOf(static_cast<std::size_t>(fi - nodes_.begin()));
        const QRectF b = BoxOf(static_cast<std::size_t>(ti - nodes_.begin()));
        const QPointF from{a.right(), a.center().y()};
        const QPointF to{b.left(), b.center().y()};
        QPainterPath path{from};
        path.cubicTo(from.x() + 24, from.y(), to.x() - 24, to.y(), to.x(), to.y());
        p.drawPath(path);
        // 箭头
        p.drawLine(to, to + QPointF(-6, -3));
        p.drawLine(to, to + QPointF(-6, 3));
    }

    // 节点（5 态：todo / running / done / failed / skipped）
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        const QRectF r = BoxOf(i);
        QColor fill = widgets::TokenQColor(t.bgPanel);
        QColor border = widgets::TokenQColor(t.lineNormal);
        QString glyph = QStringLiteral("○"); // todo
        QColor glyphCol = widgets::TokenQColor(t.statusIdle);
        bool dashed = true;
        switch (n.state) {
            case NodeState::Running:
                glyph = QStringLiteral("…");
                border = widgets::TokenQColor(t.statusBusy);
                glyphCol = widgets::TokenQColor(t.statusBusy);
                dashed = false;
                break;
            case NodeState::Done:
                glyph = QStringLiteral("✔");
                border = widgets::TokenQColor(t.statusOk);
                glyphCol = widgets::TokenQColor(t.statusOk);
                dashed = false;
                break;
            case NodeState::Failed:
                glyph = QStringLiteral("✕");
                border = widgets::TokenQColor(t.statusDanger);
                glyphCol = widgets::TokenQColor(t.statusDanger);
                dashed = false;
                break;
            case NodeState::Skipped:
                glyph = QStringLiteral("→");
                fill = widgets::TokenQColor(t.bgSurface);
                border = widgets::TokenQColor(t.lineSubtle);
                glyphCol = widgets::TokenQColor(t.textMuted);
                dashed = false;
                break;
            case NodeState::Todo:
            default:
                break;
        }
        p.setBrush(fill);
        QPen pen{border, n.state == NodeState::Running ? 2.0 : 1.0};
        if (dashed) {
            pen.setStyle(Qt::DashLine);
        }
        p.setPen(pen);
        p.drawRoundedRect(r, 8, 8);

        p.setPen(glyphCol);
        p.setFont(QFont{font().family(), 11});
        p.drawText(QRectF(r.x() + 6, r.y(), 22, r.height()), Qt::AlignCenter, glyph);

        p.setPen(widgets::TokenQColor(n.state == NodeState::Skipped ? t.textMuted : t.textPrimary));
        p.setFont(QFont{font().family(), 9});
        p.drawText(r.adjusted(28, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   n.title + (n.branchOf.isEmpty() ? QString{} : QStringLiteral("（支线）")));
    }
}

} // namespace shine::data
