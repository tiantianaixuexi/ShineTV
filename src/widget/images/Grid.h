#pragma once
// shine::images —— 图片控件（P02-S7，UI.md §2.3）：ThumbGrid / ImageCard。
#include "widget/controls/Controls.h"
#include "widget/controls/WidgetCommon.h"

#include <functional>
#include <vector>

#include <QAbstractItemView>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;

namespace shine::images {

// ThumbGrid —— 虚拟化缩略图网格（2 万张流畅）：只画可视格；
// 渐进加载不闪白：占位格先画主题 tile 底色（永不纯白），图到位只重绘该格。
class ThumbGrid : public QAbstractItemView {
  public:
    // 完成回调（任意线程转回 UI 线程后调用）
    using Ready = std::function<void(int row, const QImage& img)>;
    // 取图提供者：请求 row 的缩略图，完成后调 done(row, img)
    using Provider = std::function<void(int row, const std::function<void(int, QImage)>& done)>;

    explicit ThumbGrid(QWidget* parent = nullptr);

    void SetCellSize(int w, int h);
    void SetThumbProvider(Provider p) { provider_ = std::move(p); }
    void SetTitleRole(bool showTitles) { show_titles_ = showTitles; }

    [[nodiscard]] QImage ThumbOf(int row) const;   // 已加载缓存（空 = 未到位）
    [[nodiscard]] bool HasThumb(int row) const;
    void PrefetchVisible();                        // 拉取可视区 ±1 屏
    [[nodiscard]] int VisibleFirstRow() const;

    // QAbstractItemView 必须实现的几何接口
    [[nodiscard]] QModelIndex indexAt(const QPoint& p) const override;
    void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;
    [[nodiscard]] QRect visualRect(const QModelIndex& index) const override;
    [[nodiscard]] QModelIndex moveCursor(CursorAction action,
                                         Qt::KeyboardModifiers mods) override;
    [[nodiscard]] int horizontalOffset() const override;
    [[nodiscard]] int verticalOffset() const override;
    void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags) override;
    [[nodiscard]] QRegion visualRegionForSelection(const QItemSelection& sel) const override;
    [[nodiscard]] bool isIndexHidden(const QModelIndex& index) const override;
    void updateGeometries() override;

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void rowsInserted(const QModelIndex& parent, int start, int end) override;
    void dataChanged(const QModelIndex& tl, const QModelIndex& br,
                     const QList<int>& roles) override;

  private:
    [[nodiscard]] int Columns() const;
    [[nodiscard]] QRect CellRect(int logicalRow) const; // 网格单元（含标题区）
    void Request(int row);
    void OnThumbReady(int row, const QImage& img);

    int cell_w_ = 152;
    int cell_h_ = 176;
    int gap_ = 10;
    bool show_titles_ = true;
    Provider provider_;
    QHash<int, QImage> cache_;
    std::vector<int> inflight_;
};

// ImageCard —— 图片卡：缩略图 + 标题 + 状态 Tag + 悬停操作（单卡形态）
class ImageCard : public widgets::Card {
  public:
    explicit ImageCard(QWidget* parent = nullptr);

    void SetThumb(const QImage& img);
    void SetTitle(const QString& title);
    void SetStatus(const QString& tag, const QString& tone); // tone 见 Tag
    void SetActions(const QString& label, std::function<void()> onClick);

  private:
    QLabel* thumb_ = nullptr;
    QLabel* title_ = nullptr;
};

} // namespace shine::images
