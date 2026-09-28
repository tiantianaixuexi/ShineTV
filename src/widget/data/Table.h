#pragma once
// shine::data —— 数据展示控件（P02-S6，UI.md §2.2）：DataTable / DataTree。
// 虚拟化走 Qt 视图原生机制（只画可见行，万行流畅）；样式走全局 QSS。
#include "widget/controls/WidgetCommon.h"

#include <functional>
#include <vector>

#include <QString>
#include <QStringList>
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

  private:
    void LoadWidths();
    void SetupFrozen();
    void SyncFrozen();

    QString tableId_;
    QStandardItemModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* frozen_ = nullptr;
    std::vector<Column> cols_;
    std::function<void(int)> on_action_;
    int action_col_ = -1;
    int fixed_cols_ = 0;
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
