#include "ui/verify/gallery/Bench.h"

#include "ui/kit/images/Grid.h"
#include "ui/kit/images/Viewer.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QElapsedTimer>
#include <QPixmap>

#include <cstdio>
#include <random>

namespace shine::gallery {
namespace {

// 2 万行标题模型
class BigModel final : public QAbstractListModel {
  public:
    explicit BigModel(int n, QObject* parent = nullptr) : QAbstractListModel(parent), n_(n) {}
    [[nodiscard]] int rowCount(const QModelIndex& = {}) const override { return n_; }
    [[nodiscard]] QVariant data(const QModelIndex& idx, int role) const override {
        if (role == Qt::DisplayRole) {
            return QStringLiteral("图 %1").arg(idx.row() + 1);
        }
        return {};
    }

  private:
    int n_;
};

// 确定性程序图（渐变 + 圆），无外部资源依赖
QImage MakeThumb(int seed) {
    QImage img(140, 130, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            img.setPixelColor(x, y,
                              QColor((x * 2 + seed * 7) % 256, (y * 2 + seed * 13) % 256,
                                     (x + y + seed * 3) % 256));
        }
    }
    return img;
}

} // namespace

std::string ThumbBench() {
    QWidget host;
    host.resize(960, 620);
    auto* grid = new shine::images::ThumbGrid(&host);
    grid->setGeometry(host.rect());
    grid->SetCellSize(152, 176);
    grid->setModel(new BigModel(20000, grid));
    grid->SetThumbProvider([](int row, const std::function<void(int, QImage)>& done) {
        done(row, MakeThumb(row % 97)); // 模拟异步回图（此处同步回，机制相同）
    });
    host.show();
    QApplication::processEvents();
    grid->PrefetchVisible();
    QApplication::processEvents();

    // 1) 滚动流畅：200 帧随机滚动计时
    QElapsedTimer clock;
    std::mt19937 rng{7};
    clock.start();
    for (int i = 0; i < 200; ++i) {
        grid->scrollTo(grid->model()->index(static_cast<int>(rng() % 20000), 0));
        QApplication::processEvents();
    }
    const qint64 ms = clock.elapsed();

    // 2) 不闪白：滚到未加载区，抓一格占位像素 —— 必须等于主题 bgSurface（深空=深色，永不纯白）
    grid->scrollTo(grid->model()->index(19000, 0)); // 远处未预取区
    QApplication::processEvents();
    const shine::theme::ColorToken& t = shine::theme::Current();
    const QColor tile = shine::widgets::TokenQColor(t.bgSurface);
    const QPixmap snap = grid->grab();
    // 占位格采样：第一格中心
    const QColor got = snap.toImage().pixelColor(snap.width() / 6, snap.height() / 4);
    const bool notWhite = got.red() < 250 || got.green() < 250 || got.blue() < 250;
    const bool themeTracked = std::abs(got.red() - tile.red()) < 3 &&
                              std::abs(got.green() - tile.green()) < 3 &&
                              std::abs(got.blue() - tile.blue()) < 3;

    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "rows=20000 scroll-200-frames=%lldms %s | placeholder rgb(%d,%d,%d) "
                  "not-white=%s theme-tracked=%s %s",
                  static_cast<long long>(ms), ms < 2000 ? "FLUID" : "SLOW",
                  got.red(), got.green(), got.blue(), notWhite ? "yes" : "no",
                  themeTracked ? "yes" : "no", (notWhite && themeTracked) ? "PASS" : "FAIL");
    return buf;
}

std::string ViewerSelfTest() {
    // 测试图：带坐标纹理（肉眼/像素可判锚点）
    QImage img(800, 600, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            img.setPixelColor(x, y, QColor(x % 256, y % 256, (x * y) % 256));
        }
    }

    shine::images::ImageViewer v;
    v.resize(640, 480);
    v.SetImage(img);
    v.show();
    QApplication::processEvents();

    // 1) 锚点缩放正确：20 次随机锚点 × 随机方向缩放，锚点下场景点漂移 ≤ 0.75px
    //    （0.75 = 滚动条整型像素量化上限；仅在内容大于视口的缩放域量测——
    //      内容小于视口时滚动条量程为 0，锚点保持在数学上不可定义）
    std::mt19937 rng{11};
    double maxDrift = 0.0;
    v.ZoomActual();
    QApplication::processEvents();
    double z = v.Zoom();
    for (int i = 0; i < 20; ++i) {
        const QPoint anchor(static_cast<int>(rng() % 600) + 20, static_cast<int>(rng() % 440) + 20);
        double f = (i % 2 == 0) ? 1.2 : (1.0 / 1.2);
        if (z * f < 0.9) {
            f = 1.2; // 保持内容大于视口（可滚动域）
        }
        if (z * f > 3.0) {
            f = 1.0 / 1.2;
        }
        const QPointF before = v.mapToScene(anchor);
        v.ZoomBy(f, anchor);
        QApplication::processEvents();
        z = v.Zoom();
        const QPointF after = v.mapToScene(anchor);
        const double drift = std::hypot(before.x() - after.x(), before.y() - after.y());
        maxDrift = std::max(maxDrift, drift);
    }

    // 2) 边界：疯狂放大到顶 = 16×，缩到底 = 0.1×
    const QPoint c(320, 240);
    for (int i = 0; i < 60; ++i) {
        v.ZoomBy(1.5, c);
    }
    const bool maxOk = v.AtMaxZoom() && v.Zoom() <= 16.0 + 1e-9;
    for (int i = 0; i < 90; ++i) {
        v.ZoomBy(0.66, c);
    }
    const bool minOk = v.AtMinZoom() && v.Zoom() >= 0.1 - 1e-9;

    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "anchor-max-drift=%.3fpx %s | clamp-max(<=16x)=%s | clamp-min(>=0.1x)=%s | %s",
                  maxDrift, maxDrift <= 0.75 ? "OK" : "BAD", maxOk ? "yes" : "no",
                  minOk ? "yes" : "no",
                  (maxDrift <= 0.75 && maxOk && minOk) ? "PASS" : "FAIL");
    return buf;
}

} // namespace shine::gallery
