#pragma once
// shine::data —— 数据展示控件（P02-S6，UI.md §2.2）：DataTable / DataTree。
// 虚拟化走 Qt 视图原生机制（只画可见行，万行流畅）；样式走全局 QSS。
#include "ui/kit/controls/WidgetCommon.h"

#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTreeView>
#include <QWidget>

class QStandardItem;
class QStandardItemModel;
class QSortFilterProxyModel;
class QHeaderView;

namespace shine::data {

struct Column {
    QString id;    // 持久化键（列宽按 id 存，绝不用索引——UI.md §2.2）
    QString title;
    int width = 120;
};

// 行绘制代理 —— 补 QSS 拿不到的两条设计稿规则（webui ui.css:750-759）：
//   .table tbody tr:hover { background: var(--fill-hover) }
//   .table tbody tr.sel   { background: var(--fill-selected);
//                            box-shadow: inset 2px 0 0 var(--accent) }
// QSS 没有 `::row:hover` 选择器，也没有 `inset` 阴影语法（QSS 只认 border），
// 所以这两条只能自绘。选中行的**底色与文字色**已由全局 QSS 的
// `QTableView::item:selected` 兑现，这里只补 hover 底与左侧 2px accent 条。
class RowChrome final : public QStyledItemDelegate {
  public:
    explicit RowChrome(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    // 当前鼠标所在行（-1 = 无）。由视图在 mouseMoveEvent 里维护。
    void SetHoveredRow(int row) { hovered_ = row; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

  private:
    int hovered_ = -1;
};

// DataTable —— 虚拟化表格：列排序 / 全表筛选 / 冻结列 / 单选·多选·框选 /
// 行内操作列 / 列宽按列 id 持久化
class DataTable : public QTableView {
  public:
    explicit DataTable(const QString& tableId, QWidget* parent = nullptr);
    ~DataTable() override;

    void SetColumns(const std::vector<Column>& cols);
    void SetRows(const std::vector<std::vector<QString>>& rows);

    enum class Select { Single, Multi, Rubber };
    void SetSelectable(Select mode);

    void SetFilter(const QString& text); // 任意列包含即显示
    void SetActionColumn(const QString& title, std::function<void(int row)> onClick);
    void SetFixedColumns(int n); // 前 n 列冻结（左侧冻结窗格）

    void SaveWidths();                       // 列宽持久化（键 = 列 id）
    [[nodiscard]] std::vector<int> SelectedRows() const;
    [[nodiscard]] int RowCount() const;

  protected:
    void resizeEvent(QResizeEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void leaveEvent(QEvent* ev) override;

  private:
    void LoadWidths();
    void SetupFrozen();
    void SyncFrozen();
    // hover 行号同步到两个视图的代理（冻结窗格与主表共享 model，行号一致）
    void SetHoveredRow(int row);

    QString tableId_;
    QStandardItemModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* frozen_ = nullptr;
    std::unique_ptr<RowChrome> chrome_;
    std::unique_ptr<RowChrome> chromeFrozen_;
    std::vector<Column> cols_;
    std::function<void(int)> on_action_;
    int action_col_ = -1;
    int fixed_cols_ = 0;
    int hovered_row_ = -1;
};

// DataTree —— 虚拟化树：懒展开（OnNeedChildren 回调补子节点）/ 拖拽重排 / 多选
class DataTree : public QTreeView {
  public:
    explicit DataTree(QWidget* parent = nullptr);

    void SetColumns(const QStringList& headers);
    [[nodiscard]] QStandardItem* AddTop(const QStringList& texts, bool lazy = true);
    void SetOnNeedChildren(std::function<void(QStandardItem*)> cb);
    void SetOnContextMenu(std::function<void(const QPoint&)> cb);
    [[nodiscard]] std::vector<QStandardItem*> SelectedItems() const;

  protected:
    void contextMenuEvent(QContextMenuEvent* ev) override;

  private:
    QStandardItemModel* model_ = nullptr;
    std::function<void(QStandardItem*)> on_need_children_;
    std::function<void(const QPoint&)> on_context_;
};

} // namespace shine::data
