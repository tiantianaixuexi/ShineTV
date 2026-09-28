#pragma once
// shine::images —— 看图控件（P02-S7，UI.md §2.3）：ImageViewer / CompareView / CropZoom。
#include "ui/kit/controls/WidgetCommon.h"

#include <QGraphicsView>
#include <QImage>
#include <QString>
#include <QWidget>

#include <vector>

namespace shine::images {

// ImageViewer —— 0.1–16× **锚点缩放**（滚轮以光标为锚）；F 适配 / 1 实际 /
// ←→ 切图（150ms 淡入）/ Esc 退出全屏；双击在适配↔实际间切换
class ImageViewer : public QGraphicsView {
  public:
    explicit ImageViewer(QWidget* parent = nullptr);

    void SetImage(const QImage& img);
    void SetGallery(std::vector<QImage> imgs, int index = 0); // ←→ 切图用
    [[nodiscard]] QImage Current() const { return image_; }

    void ZoomBy(double factor, const QPoint& anchor); // 锚点缩放（核心）
    void ZoomFit();                                   // F：适配窗口
    void ZoomActual();                                // 1：实际像素
    [[nodiscard]] double Zoom() const { return zoom_; }
    [[nodiscard]] bool AtMinZoom() const { return zoom_ <= kMinZoom + 1e-9; }
    [[nodiscard]] bool AtMaxZoom() const { return zoom_ >= kMaxZoom - 1e-9; }

    static constexpr double kMinZoom = 0.1;
    static constexpr double kMaxZoom = 16.0;

  protected:
    void wheelEvent(QWheelEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;

  private:
    void Show(int index);
    void ApplyZoom(double z, const QPoint& anchor);

    QImage image_;
    std::vector<QImage> gallery_;
    int index_ = 0;
    double zoom_ = 1.0;
};

// CompareView —— 三模式：并排 / 滑动对比（拖分割线）/ 差异叠加
class CompareView : public QWidget {
  public:
    enum class Mode { SideBySide, Wipe, Diff };

    explicit CompareView(QWidget* parent = nullptr);

    void SetPair(const QImage& a, const QImage& b);
    void SetMode(Mode m);
    [[nodiscard]] Mode GetMode() const { return mode_; }

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;

  private:
    QImage a_;
    QImage b_;
    Mode mode_ = Mode::SideBySide;
    double wipe_ = 0.5; // 滑动对比分割位置 0..1
};

// CropZoom —— 3–4× 放大镜（跟随光标取样，参考图/裁剪辅助）
class CropZoom : public QWidget {
  public:
    explicit CropZoom(QWidget* parent = nullptr);

    void SetSource(const QImage& img);
    void SetMagnify(double m); // 3.0–4.0
    void Track(const QPointF& imagePos); // 鼠标在图上的位置 → 重绘放大镜

  protected:
    void paintEvent(QPaintEvent* ev) override;

  private:
    QImage source_;
    QPointF center_;
    double mag_ = 3.5;
};

} // namespace shine::images
