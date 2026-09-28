#include "widget/images/Viewer.h"

#include "widget/motion/Tween.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QGraphicsOpacityEffect>

#include <algorithm>
#include <cmath>

namespace shine::images {
namespace {

// 把 imgSize 等比缩放到 box 内并居中
QRectF FitIn(const QRect& box, const QSize& imgSize) {
    if (imgSize.isEmpty()) {
        return box;
    }
    const double s = std::min(box.width() / static_cast<double>(imgSize.width()),
                              box.height() / static_cast<double>(imgSize.height()));
    const double w = imgSize.width() * s;
    const double h = imgSize.height() * s;
    return {box.x() + (box.width() - w) / 2, box.y() + (box.height() - h) / 2, w, h};
}

} // namespace

// ================================================================ ImageViewer

ImageViewer::ImageViewer(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setAlignment(Qt::AlignLeft | Qt::AlignTop); // 关掉自动居中偏移（锚点数学前提）
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
}

void ImageViewer::SetImage(const QImage& img) {
    image_ = img;
    scene()->clear();
    // 画布留 3000px 余量（GIMP 式可越界拖动）：保证任何锚点的滚动量都在量程内，
    // 锚点缩放数学永不被滚动条边界钳制（P02-S7 判据「锚点缩放正确」）
    const QRectF canvas = QRectF(QPointF(0, 0), QSizeF(img.size())).adjusted(-3000, -3000, 3000, 3000);
    scene()->setSceneRect(canvas);
    scene()->addPixmap(QPixmap::fromImage(img));
    ZoomFit();
}

void ImageViewer::SetGallery(std::vector<QImage> imgs, int index) {
    gallery_ = std::move(imgs);
    if (gallery_.empty()) {
        return;
    }
    index_ = std::clamp(index, 0, static_cast<int>(gallery_.size()) - 1);
    Show(index_);
}

void ImageViewer::Show(int index) {
    index_ = std::clamp(index, 0, static_cast<int>(gallery_.size()) - 1);
    SetImage(gallery_[static_cast<std::size_t>(index_)]);
    // ←→ 切图淡入（kDurFastMs 120ms ≈ 150ms 档；减少动效时 Tween 自动瞬时）
    if (auto* g = graphicsEffect()) {
        g->deleteLater();
    }
    auto* eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(0.0);
    setGraphicsEffect(eff);
    auto* tw = new motion::Tween(theme::motion::kStandard, this);
    tw->Run(0.0, 1.0, theme::motion::kDurFastMs,
            [eff](const QVariant& v) { eff->setOpacity(v.toDouble()); });
}

void ImageViewer::ZoomBy(double factor, const QPoint& anchor) {
    ApplyZoom(zoom_ * factor, anchor);
}

void ImageViewer::ApplyZoom(double z, const QPoint& anchor) {
    z = std::clamp(z, kMinZoom, kMaxZoom);
    // 锚点缩放精确式（AlignLeft|AlignTop ⇒ view = scene*z − scroll）：
    // 锚点下的场景坐标在换倍率前后必须相同 ⇒ scroll' = scenePos*z' − anchor
    const QPointF scenePos = mapToScene(anchor);
    setTransform(QTransform().scale(z, z));
    const QPointF want = scenePos * z - QPointF(anchor);
    horizontalScrollBar()->setValue(static_cast<int>(std::lround(want.x())));
    verticalScrollBar()->setValue(static_cast<int>(std::lround(want.y())));
    zoom_ = z;
}

void ImageViewer::ZoomFit() {
    // 适配窗口只针对图像矩形（不含画布余量）
    const QRectF imgRect(QPointF(0, 0), QSizeF(image_.size()));
    fitInView(imgRect, Qt::KeepAspectRatio);
    const double zx = viewport()->width() / std::max(imgRect.width(), 1.0);
    const double zy = viewport()->height() / std::max(imgRect.height(), 1.0);
    zoom_ = std::clamp(std::min(zx, zy), kMinZoom, kMaxZoom);
}

void ImageViewer::ZoomActual() { ApplyZoom(1.0, viewport()->rect().center()); }

void ImageViewer::wheelEvent(QWheelEvent* ev) {
    const double factor = std::pow(1.0015, ev->angleDelta().y());
    ZoomBy(factor, ev->position().toPoint()); // 锚 = 光标
}

void ImageViewer::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
        case Qt::Key_F:
            ZoomFit(); // F：适配窗口
            return;
        case Qt::Key_1:
            ZoomActual(); // 1：实际像素
            return;
        case Qt::Key_Left:
            if (!gallery_.empty()) {
                Show(index_ - 1); // ←：上一张（150ms 淡入）
            }
            return;
        case Qt::Key_Right:
            if (!gallery_.empty()) {
                Show(index_ + 1); // →：下一张
            }
            return;
        default:
            break;
    }
    QGraphicsView::keyPressEvent(ev);
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent* ev) {
    if (AtMinZoom() || zoom_ < 1.0) {
        ZoomActual(); // 双击：适配 ↔ 实际
    } else {
        ZoomFit();
    }
    QGraphicsView::mouseDoubleClickEvent(ev);
}

// ================================================================= CompareView

CompareView::CompareView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(220);
    setMouseTracking(true);
}

void CompareView::SetPair(const QImage& a, const QImage& b) {
    a_ = a;
    b_ = b;
    update();
}

void CompareView::SetMode(Mode m) {
    mode_ = m;
    update();
}

void CompareView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const theme::ColorToken& t = theme::Current();
    p.fillRect(rect(), widgets::TokenQColor(t.bgSurface));
    if (a_.isNull() && b_.isNull()) {
        return;
    }

    if (mode_ == Mode::SideBySide) {
        const QRect left(0, 0, width() / 2, height());
        const QRect right(width() / 2, 0, width() / 2, height());
        if (!a_.isNull()) {
            p.drawImage(FitIn(left, a_.size()), a_);
        }
        if (!b_.isNull()) {
            p.drawImage(FitIn(right, b_.size()), b_);
        }
        p.setPen(widgets::TokenQColor(t.lineStrong));
        p.drawLine(width() / 2, 0, width() / 2, height());
    } else if (mode_ == Mode::Wipe) {
        const QRect all = rect();
        if (!a_.isNull()) {
            p.drawImage(FitIn(all, a_.size()), a_);
        }
        if (!b_.isNull()) {
            const QRectF f = FitIn(all, b_.size());
            p.save();
            p.setClipRect(QRect(0, 0, static_cast<int>(width() * wipe_), height()));
            p.drawImage(f, b_);
            p.restore();
        }
        const int x = static_cast<int>(width() * wipe_);
        p.setPen(QPen{widgets::TokenQColor(t.accentPrimary), 2});
        p.drawLine(x, 0, x, height());
    } else { // Diff：差异叠加（放大 4× 差值）
        QImage d = a_.size() == b_.size() ? a_ : QImage();
        if (!d.isNull()) {
            for (int y = 0; y < d.height(); ++y) {
                for (int x = 0; x < d.width(); ++x) {
                    const QColor ca = a_.pixelColor(x, y);
                    const QColor cb = b_.pixelColor(x, y);
                    const int df = (std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) +
                                    std::abs(ca.blue() - cb.blue())) *
                                   4;
                    d.setPixelColor(x, y, QColor(std::min(df, 255), std::min(df, 255), std::min(df, 255)));
                }
            }
            p.drawImage(FitIn(rect(), d.size()), d);
        }
    }
}

void CompareView::mousePressEvent(QMouseEvent* ev) {
    if (mode_ == Mode::Wipe) {
        wipe_ = std::clamp(ev->position().x() / std::max(width(), 1), 0.0, 1.0);
        update();
    }
}

void CompareView::mouseMoveEvent(QMouseEvent* ev) {
    if (mode_ == Mode::Wipe && ev->buttons().testFlag(Qt::LeftButton)) {
        wipe_ = std::clamp(ev->position().x() / std::max(width(), 1), 0.0, 1.0);
        update();
    }
}

// ================================================================== CropZoom

CropZoom::CropZoom(QWidget* parent) : QWidget(parent) {
    setFixedSize(180, 180);
}

void CropZoom::SetSource(const QImage& img) {
    source_ = img;
    update();
}

void CropZoom::SetMagnify(double m) {
    mag_ = std::clamp(m, 3.0, 4.0); // 3–4×（UI.md §2.3）
    update();
}

void CropZoom::Track(const QPointF& imagePos) {
    center_ = imagePos;
    update();
}

void CropZoom::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const theme::ColorToken& t = theme::Current();
    p.fillRect(rect(), widgets::TokenQColor(t.bgPanel));
    if (source_.isNull()) {
        return;
    }
    // 放大镜：mag_× 取样（3–4×）
    const QRectF src(center_.x() - width() / (2 * mag_), center_.y() - height() / (2 * mag_),
                     width() / mag_, height() / mag_);
    p.drawImage(rect(), source_, src);
    p.setPen(QPen{widgets::TokenQColor(t.accentPrimary), 2});
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    // 中心十字
    p.drawLine(width() / 2 - 8, height() / 2, width() / 2 + 8, height() / 2);
    p.drawLine(width() / 2, height() / 2 - 8, width() / 2, height() / 2 + 8);
}

} // namespace shine::images
