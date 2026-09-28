#include "ui/kit/images/Grid.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QItemSelectionModel>

#include <algorithm>

namespace shine::images {

// ================================================================ ThumbGrid

ThumbGrid::ThumbGrid(QWidget* parent) : QAbstractItemView(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ThumbGrid::SetCellSize(int w, int h) {
    cell_w_ = w;
    cell_h_ = h;
    doItemsLayout();
    updateGeometries();
}

int ThumbGrid::Columns() const {
    const int usable = std::max(viewport()->width() - gap_, 1);
    return std::max(usable / (cell_w_ + gap_), 1);
}

QRect ThumbGrid::CellRect(int logicalRow) const {
    const int cols = Columns();
    const int col = logicalRow % cols;
    const int row = logicalRow / cols;
    const int x = gap_ + col * (cell_w_ + gap_);
    const int y = row * (cell_h_ + gap_) - verticalScrollBar()->value();
    return {x, y, cell_w_, cell_h_};
}

QModelIndex ThumbGrid::indexAt(const QPoint& p) const {
    const int cols = Columns();
    const int y = p.y() + verticalScrollBar()->value();
    const int row = y / (cell_h_ + gap_);
    const int col = (p.x() - gap_) / (cell_w_ + gap_);
    if (col < 0 || col >= cols || row < 0) {
        return {};
    }
    const int logical = row * cols + col;
    if (model() == nullptr || logical >= model()->rowCount()) {
        return {};
    }
    return model()->index(logical, 0);
}

QRect ThumbGrid::visualRect(const QModelIndex& index) const {
    return CellRect(index.row()).translated(0, 0);
}

QModelIndex ThumbGrid::moveCursor(CursorAction action, Qt::KeyboardModifiers) {
    if (model() == nullptr || model()->rowCount() == 0) {
        return {};
    }
    const int cols = Columns();
    int row = currentIndex().isValid() ? currentIndex().row() : 0;
    switch (action) {
        case MoveLeft:
            row -= 1;
            break;
        case MoveRight:
            row += 1;
            break;
        case MoveUp:
            row -= cols;
            break;
        case MoveDown:
            row += cols;
            break;
        case MoveHome:
            row = 0;
            break;
        case MoveEnd:
            row = model()->rowCount() - 1;
            break;
        case MovePageUp:
            row -= cols * 3;
            break;
        case MovePageDown:
            row += cols * 3;
            break;
        default:
            break;
    }
    row = std::clamp(row, 0, model()->rowCount() - 1);
    return model()->index(row, 0);
}

int ThumbGrid::horizontalOffset() const { return horizontalScrollBar()->value(); }

int ThumbGrid::verticalOffset() const { return verticalScrollBar()->value(); }

void ThumbGrid::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags) {
    if (model() == nullptr) {
        return;
    }
    QItemSelection sel;
    // 框选：矩形相交的格全选
    for (int r = 0; r < model()->rowCount(); ++r) {
        const QRect cell = CellRect(r);
        if (cell.intersects(rect)) {
            const QModelIndex idx = model()->index(r, 0);
            sel.select(idx, idx);
        }
    }
    selectionModel()->select(sel, flags);
}

QRegion ThumbGrid::visualRegionForSelection(const QItemSelection& sel) const {
    QRegion region;
    for (const QItemSelectionRange& range : sel) {
        for (int r = range.top(); r <= range.bottom(); ++r) {
            region += CellRect(r);
        }
    }
    return region;
}

bool ThumbGrid::isIndexHidden(const QModelIndex&) const { return false; }

void ThumbGrid::updateGeometries() {
    if (model() != nullptr) {
        const int cols = Columns();
        const int rows = (model()->rowCount() + cols - 1) / cols;
        verticalScrollBar()->setRange(0, std::max(rows * (cell_h_ + gap_) - viewport()->height(), 0));
        verticalScrollBar()->setPageStep(viewport()->height());
        verticalScrollBar()->setSingleStep(cell_h_ + gap_);
    }
    QAbstractItemView::updateGeometries();
}

void ThumbGrid::scrollTo(const QModelIndex& index, ScrollHint hint) {
    const QRect r = CellRect(index.row());
    const int viewH = viewport()->height();
    int v = verticalScrollBar()->value();
    if (hint == PositionAtTop || r.top() < 0) {
        v += r.top() - gap_;
    } else if (r.bottom() > viewH) {
        v += r.bottom() - viewH + gap_;
    }
    verticalScrollBar()->setValue(std::max(v, 0));
}

void ThumbGrid::rowsInserted(const QModelIndex&, int, int) {
    updateGeometries();
    update();
}

void ThumbGrid::dataChanged(const QModelIndex& tl, const QModelIndex& br, const QList<int>&) {
    update(QRect(0, CellRect(tl.row()).top(), viewport()->width(),
                 CellRect(br.row()).bottom() - CellRect(tl.row()).top() + cell_h_));
}

void ThumbGrid::resizeEvent(QResizeEvent* ev) {
    QAbstractItemView::resizeEvent(ev);
    updateGeometries();
}

void ThumbGrid::mousePressEvent(QMouseEvent* ev) {
    QAbstractItemView::mousePressEvent(ev);
    const QModelIndex idx = indexAt(ev->position().toPoint());
    if (idx.isValid()) {
        selectionModel()->select(idx, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    }
}

bool ThumbGrid::HasThumb(int row) const { return cache_.contains(row); }

QImage ThumbGrid::ThumbOf(int row) const { return cache_.value(row); }

int ThumbGrid::VisibleFirstRow() const {
    return std::max(verticalScrollBar()->value() / (cell_h_ + gap_) - 1, 0);
}

void ThumbGrid::PrefetchVisible() {
    if (model() == nullptr) {
        return;
    }
    const int cols = Columns();
    // 隐藏态（未 show）viewport 高为 0：用 600px 兜底，保证预取覆盖首屏
    const int vh = std::max(viewport()->height(), 600);
    const int visibleRows = vh / (cell_h_ + gap_) + 2;
    const int first = VisibleFirstRow();
    const int begin = first * cols;
    const int end = std::min((first + visibleRows * 2) * cols, model()->rowCount()); // ±1 屏
    for (int r = begin; r < end; ++r) {
        if (!cache_.contains(r)) {
            Request(r);
        }
    }
}

void ThumbGrid::Request(int row) {
    if (!provider_) {
        return;
    }
    if (std::find(inflight_.begin(), inflight_.end(), row) != inflight_.end()) {
        return;
    }
    inflight_.push_back(row);
    provider_(row, [this](int r, const QImage& img) { OnThumbReady(r, img); });
}

void ThumbGrid::OnThumbReady(int row, const QImage& img) {
    cache_.insert(row, img);
    inflight_.erase(std::remove(inflight_.begin(), inflight_.end(), row), inflight_.end());
    update(CellRect(row)); // 只重绘该格（渐进加载，不闪白）
}

void ThumbGrid::paintEvent(QPaintEvent* ev) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const theme::ColorToken& t = theme::Current();
    const QColor tileBg = widgets::TokenQColor(t.bgSurface);  // 占位底 = 主题表面色（永不纯白）
    const QColor border = widgets::TokenQColor(t.lineSubtle);
    const QColor textCol = widgets::TokenQColor(t.textSecondary);
    const QColor placeholder = widgets::TokenQColor(t.textMuted);

    if (model() == nullptr) {
        return;
    }
    const int cols = Columns();
    const int first = VisibleFirstRow();
    const int end = std::min(first * cols + (viewport()->height() / (cell_h_ + gap_) + 3) * cols,
                             model()->rowCount());
    for (int r = first * cols; r < end; ++r) {
        const QRect cell = CellRect(r);
        if (!cell.intersects(ev->rect().adjusted(-cell_w_, -cell_h_, cell_w_, cell_h_))) {
            continue;
        }
        const QRect tile(cell.x(), cell.y(), cell_w_, cell_h_ - (show_titles_ ? 24 : 0));
        // 1) 先画 tile 底 + 边（任何主题下都不闪白）
        p.setPen(Qt::NoPen);
        p.setBrush(tileBg);
        p.drawRoundedRect(tile, 8, 8);
        p.setPen(QPen{border, 1});
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(tile, 8, 8);

        // 2) 图到位贴图；空图 = 失败卡（danger 边 + ⚠ 重试提示）；未到位 = 占位符
        if (cache_.contains(r) && !cache_[r].isNull()) {
            p.drawImage(tile.adjusted(3, 3, -3, -3), cache_[r]);
        } else if (cache_.contains(r)) {
            p.setPen(QPen{widgets::TokenQColor(t.statusDanger), 2});
            p.drawRoundedRect(tile.adjusted(1, 1, -1, -1), 7, 7);
            p.setPen(widgets::TokenQColor(t.statusDanger));
            p.drawText(tile, Qt::AlignCenter, QStringLiteral("⚠ 加载失败"));
        } else {
            p.setPen(placeholder);
            p.drawText(tile, Qt::AlignCenter, QStringLiteral("…"));
        }

        // 选中描边
        const QModelIndex idx = model()->index(r, 0);
        if (selectionModel() != nullptr && selectionModel()->isSelected(idx)) {
            p.setPen(QPen{widgets::TokenQColor(t.accentPrimary), 2});
            p.drawRoundedRect(tile.adjusted(1, 1, -1, -1), 7, 7);
        }

        if (show_titles_) {
            p.setPen(textCol);
            p.drawText(QRect(cell.x(), tile.bottom() + 2, cell_w_, 20), Qt::AlignCenter,
                       model()->data(idx).toString());
        }
    }
}

// ================================================================= ImageCard

ImageCard::ImageCard(QWidget* parent) : widgets::Card(widgets::Card::Variant::Outlined, parent) {
    BodyLayout()->setSpacing(6);
    thumb_ = new QLabel(this);
    thumb_->setFixedHeight(120);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setStyleSheet(QStringLiteral("border-radius: 6px;"));
    widgets::SetKind(thumb_, "statedetail");
    BodyLayout()->addWidget(thumb_);
    title_ = new QLabel(this);
    widgets::SetKind(title_, "statetitle");
    BodyLayout()->addWidget(title_);
}

void ImageCard::SetThumb(const QImage& img) {
    thumb_->setPixmap(QPixmap::fromImage(img.scaled(
        thumb_->width() > 0 ? thumb_->width() : 200, 120, Qt::KeepAspectRatio,
        Qt::SmoothTransformation)));
}

void ImageCard::SetTitle(const QString& title) { title_->setText(title); }

void ImageCard::SetStatus(const QString& tag, const QString& tone) {
    const QByteArray t = tone.toUtf8();
    auto* tg = new shine::widgets::Tag(tag, t.constData(), false, this);
    BodyLayout()->addWidget(tg);
}

void ImageCard::SetActions(const QString& label, std::function<void()> onClick) {
    auto* b = new shine::widgets::Button(label, shine::widgets::Button::Variant::Ghost,
                                         shine::widgets::Button::Size::Sm, this);
    connect(b, &QPushButton::clicked, this, [cb = std::move(onClick)] {
        if (cb) {
            cb();
        }
    });
    BodyLayout()->addWidget(b, 0, Qt::AlignLeft);
}

} // namespace shine::images
