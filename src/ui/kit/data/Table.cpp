#include "ui/kit/data/Table.h"

#include "ui/kit/theme/Theme.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace shine::data {
namespace {

// 全表文本筛选 + 排序代理（无 Q_OBJECT 需求）
class FilterProxy final : public QSortFilterProxyModel {
  public:
    explicit FilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void SetFilterText(const QString& t) {
        text_ = t;
        invalidateFilter();
    }

  protected:
    [[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (text_.isEmpty()) {
            return true;
        }
        const QModelIndex base = mapToSource(index(row, 0, parent));
        const QAbstractItemModel* m = sourceModel();
        for (int c = 0; c < m->columnCount(); ++c) {
            if (m->data(m->index(base.row(), c)).toString().contains(text_, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

  private:
    QString text_;
};

std::filesystem::path WidthsPath(const QString& tableId) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return shine::util::PathFromUtf8(
        (dir + QStringLiteral("/table-widths-%1.txt")).arg(tableId).toStdString());
}

} // namespace

// ================================================================ RowChrome

namespace {
// 最左侧可见列：选中条的落点。冻结列开启时它就是逻辑第 0 列；
// 列被隐藏过时不能画在看不见的格子上。isColumnHidden 是 QTableView 的接口，
// QAbstractItemView 没有，故这里按表视图取。
int FirstVisualColumn(const QAbstractItemView* view) {
    const auto* table = qobject_cast<const QTableView*>(view);
    const QAbstractItemModel* m = view != nullptr ? view->model() : nullptr;
    if (m == nullptr) {
        return 0;
    }
    for (int c = 0; c < m->columnCount(); ++c) {
        if (table == nullptr || !table->isColumnHidden(c)) {
            return c;
        }
    }
    return 0;
}
} // namespace

void RowChrome::paint(QPainter* painter, const QStyleOptionViewItem& option,
                      const QModelIndex& index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const bool selected = opt.state.testFlag(QStyle::State_Selected);
    const bool hovered = index.isValid() && index.row() == hovered_;

    // hover 底（ui.css:750 `tr:hover { background: var(--fill-hover) }`）。
    // QSS 没有 ::row:hover，只能在代理里先铺一层再让基类画选中/文字。
    // 选中时底色交给 QSS 的 item:selected，这里不重复铺，避免两套色打架。
    if (hovered && !selected) {
        painter->save();
        painter->fillRect(opt.rect, widgets::TokenQColor(theme::Current().fillHover));
        painter->restore();
        // 强制非选中态，否则基类仍会按 State_MouseOver 走 hover 样式分支
        opt.state &= ~QStyle::State_MouseOver;
    }

    // 基类负责文字 / 图标 / 已选中的底色（QSS 兑现）
    QStyledItemDelegate::paint(painter, opt, index);

    if (!selected) {
        return;
    }
    // 选中行左侧 2px accent 条（ui.css:755 `box-shadow: inset 2px 0 0 var(--accent)`）。
    // QSS 的 inset 阴影不存在，border-left 又会被 td 的下边线视觉吃掉，
    // 故自绘。只画在**第一列**上：代理逐 cell 调用，每格都画会变成 N 根竖条。
    const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(parent());
    if (view == nullptr || index.column() != FirstVisualColumn(view)) {
        return;
    }
    painter->save();
    painter->fillRect(QRect(opt.rect.left(), opt.rect.top(), 2, opt.rect.height()),
                      widgets::TokenQColor(theme::Current().accentPrimary));
    painter->restore();
}

// ================================================================ DataTable

DataTable::DataTable(const QString& tableId, QWidget* parent)
    : QTableView(parent), tableId_(tableId) {
    model_ = new QStandardItemModel(this);
    proxy_ = new FilterProxy(this);
    proxy_->setSourceModel(model_);
    setModel(proxy_);

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection); // 默认单选
    setSortingEnabled(true);
    horizontalHeader()->setSectionsClickable(true);
    horizontalHeader()->setSortIndicatorShown(true);
    verticalHeader()->setVisible(false);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setWordWrap(false);
    setMouseTracking(true);
    // 框选（Rubber）由 SetSelectable 打开

    // 行 hover 底 + 选中左条：QSS 拿不到 ::row:hover 与 inset 阴影（见 RowChrome）
    chrome_ = std::make_unique<RowChrome>(this);
    setItemDelegate(chrome_.get());

    // 选中变化时要重画：左侧 accent 条是自绘的，不随选中态自动刷新
    if (selectionModel() != nullptr) {
        connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
                [this](const QItemSelection&, const QItemSelection&) {
                    viewport()->update();
                    if (frozen_ != nullptr) {
                        frozen_->viewport()->update();
                    }
                });
    }
}

DataTable::~DataTable() { SaveWidths(); }

void DataTable::SetColumns(const std::vector<Column>& cols) {
    cols_ = cols;
    QStringList headers;
    for (const Column& c : cols_) {
        headers << c.title;
    }
    model_->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
        horizontalHeader()->resizeSection(i, cols_[static_cast<std::size_t>(i)].width);
    }
    LoadWidths(); // 列宽按 id 恢复
    if (frozen_ != nullptr) {
        frozen_->deleteLater();
        frozen_ = nullptr;
    }
}

void DataTable::SetRows(const std::vector<std::vector<QString>>& rows) {
    model_->setRowCount(0);
    model_->setRowCount(static_cast<int>(rows.size()));
    const int colN = static_cast<int>(cols_.size());
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const auto& row = rows[static_cast<std::size_t>(r)];
        for (int c = 0; c < colN; ++c) {
            auto* item = new QStandardItem(c < static_cast<int>(row.size())
                                               ? row[static_cast<std::size_t>(c)]
                                               : QString{});
            item->setEditable(false);
            // 居中：QHeaderView 的表头默认就是居中，表体保持默认左对齐会让
            // "表头居中 / 数据靠左"看起来像没对齐。两者统一为居中。
            // 冻结窗格（frozen_）与本表共享 model，改 item 即两边同时生效。
            item->setTextAlignment(Qt::AlignCenter);
            model_->setItem(r, c, item);
        }
    }
}

void DataTable::SetSelectable(Select mode) {
    switch (mode) {
        case Select::Single:
            setSelectionMode(QAbstractItemView::SingleSelection);
            break;
        case Select::Multi:
        case Select::Rubber: // 框选 = ExtendedSelection 原生橡皮筋
            setSelectionMode(QAbstractItemView::ExtendedSelection);
            break;
    }
}

void DataTable::SetFilter(const QString& text) {
    static_cast<FilterProxy*>(proxy_)->SetFilterText(text);
}

void DataTable::SetActionColumn(const QString& title, std::function<void(int row)> onClick) {
    on_action_ = std::move(onClick);
    action_col_ = model_->columnCount();
    model_->setHorizontalHeaderItem(action_col_, new QStandardItem(title));
    horizontalHeader()->resizeSection(action_col_, 88);
    for (int r = 0; r < model_->rowCount(); ++r) { // 行内操作标记
        auto* it = new QStandardItem(QStringLiteral("⋯"));
        it->setEditable(false);
        it->setTextAlignment(Qt::AlignCenter);
        model_->setItem(r, action_col_, it);
    }
    connect(this, &QTableView::clicked, this, [this](const QModelIndex& idx) {
        if (on_action_ && idx.column() == action_col_) {
            on_action_(idx.row());
        }
    });
}

void DataTable::SetFixedColumns(int n) {
    if (n <= 0) {
        return;
    }
    fixed_cols_ = n;
    SetupFrozen();
}

void DataTable::SetupFrozen() {
    if (frozen_ != nullptr) {
        frozen_->deleteLater();
    }
    frozen_ = new QTableView(this->viewport()->parentWidget());
    frozen_->setModel(model());
    frozen_->setSelectionModel(selectionModel()); // 共享选中
    frozen_->setFocusPolicy(Qt::NoFocus);
    frozen_->setFrameShape(QFrame::NoFrame);
    frozen_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen_->verticalHeader()->hide();
    frozen_->setAlternatingRowColors(true);
    frozen_->setShowGrid(false);
    for (int c = 0; c < fixed_cols_; ++c) {
        setColumnHidden(c, false);
    }
    for (int c = fixed_cols_; c < model()->columnCount(); ++c) {
        frozen_->setColumnHidden(c, true);
    }
    frozen_->setHorizontalScrollMode(ScrollPerPixel);
    frozen_->setSortingEnabled(false);
    // 冻结窗格是独立的 QTableView，必须挂自己的代理，否则它那几列
    // 既没有 hover 底也没有选中左条（主表的选中状态是共享的）
    chromeFrozen_ = std::make_unique<RowChrome>(frozen_);
    frozen_->setItemDelegate(chromeFrozen_.get());
    frozen_->viewport()->stackUnder(viewport());
    connect(verticalScrollBar(), &QScrollBar::valueChanged, frozen_->verticalScrollBar(),
            &QScrollBar::setValue);
    connect(frozen_->verticalScrollBar(), &QScrollBar::valueChanged, verticalScrollBar(),
            &QScrollBar::setValue);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logical, int, int newsize) {
                if (frozen_ != nullptr && logical < fixed_cols_) {
                    frozen_->setColumnWidth(logical, newsize);
                }
            });
    frozen_->show();
    SyncFrozen();
}

void DataTable::SyncFrozen() {
    if (frozen_ == nullptr) {
        return;
    }
    int w = 0;
    for (int c = 0; c < fixed_cols_; ++c) {
        w += columnWidth(c);
    }
    frozen_->setGeometry(0, 0, w, viewport()->height());
    frozen_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen_->setVisible(w > 0);
}

void DataTable::resizeEvent(QResizeEvent* ev) {
    QTableView::resizeEvent(ev);
    SyncFrozen();
}

void DataTable::mouseMoveEvent(QMouseEvent* ev) {
    QTableView::mouseMoveEvent(ev);
    // hover 底要跟着鼠标走（design: tr:hover）。indexAt 命中的是代理模型的行号，
    // 代理模型与源模型行号一致，两个视图共用同一个号。
    const QModelIndex idx = indexAt(ev->pos());
    SetHoveredRow(idx.isValid() ? idx.row() : -1);
}

void DataTable::leaveEvent(QEvent* ev) {
    QTableView::leaveEvent(ev);
    SetHoveredRow(-1); // 指针移出表格，hover 底必须收掉（否则整行常亮）
}

void DataTable::SetHoveredRow(int row) {
    if (hovered_row_ == row) {
        return; // 同行移动不重绘
    }
    hovered_row_ = row;
    if (chrome_ != nullptr) {
        chrome_->SetHoveredRow(row);
    }
    if (chromeFrozen_ != nullptr) {
        chromeFrozen_->SetHoveredRow(row);
    }
    if (model() != nullptr) {
        viewport()->update(); // 只刷可见区域，万行表不整表重绘
        if (frozen_ != nullptr) {
            frozen_->viewport()->update();
        }
    }
}

std::vector<int> DataTable::SelectedRows() const {
    std::vector<int> out;
    const QModelIndexList rows = selectionModel()->selectedRows();
    out.reserve(static_cast<std::size_t>(rows.size()));
    for (const QModelIndex& idx : rows) {
        out.push_back(idx.row());
    }
    std::sort(out.begin(), out.end());
    return out;
}

int DataTable::RowCount() const { return proxy_->rowCount(); }

void DataTable::LoadWidths() {
    const std::filesystem::path path = WidthsPath(tableId_);
    std::ifstream f(shine::util::PathToUtf8(path));
    if (!f) {
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string id = line.substr(0, eq);
        const int w = std::atoi(line.c_str() + eq + 1);
        // 按**列 id**找列（不是索引——UI.md §2.2 硬要求）
        for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
            if (cols_[static_cast<std::size_t>(i)].id.toStdString() == id && w > 16) {
                horizontalHeader()->resizeSection(i, w);
                if (frozen_ != nullptr && i < fixed_cols_) {
                    frozen_->setColumnWidth(i, w);
                }
            }
        }
    }
}

void DataTable::SaveWidths() {
    std::string out;
    for (int i = 0; i < static_cast<int>(cols_.size()); ++i) {
        out += cols_[static_cast<std::size_t>(i)].id.toStdString();
        out += "=" + std::to_string(columnWidth(i)) + "\n";
    }
    shine::util::WriteFileBytes(WidthsPath(tableId_), out);
}

// ================================================================== DataTree

DataTree::DataTree(QWidget* parent) : QTreeView(parent) {
    model_ = new QStandardItemModel(this);
    setModel(model_);
    setSelectionMode(QAbstractItemView::ExtendedSelection); // 多选
    setDragDropMode(QAbstractItemView::InternalMove);       // 拖拽重排
    setDefaultDropAction(Qt::MoveAction);
    setHeaderHidden(false);
    setAlternatingRowColors(true);
    connect(this, &QTreeView::expanded, this, [this](const QModelIndex& idx) {
        QStandardItem* it = model_->itemFromIndex(idx);
        if (it != nullptr && it->rowCount() == 0 && it->data(Qt::UserRole + 1).toBool() &&
            on_need_children_) {
            on_need_children_(it); // 懒展开：按需补子节点
        }
    });
}

void DataTree::SetColumns(const QStringList& headers) { model_->setHorizontalHeaderLabels(headers); }

QStandardItem* DataTree::AddTop(const QStringList& texts, bool lazy) {
    auto* root = model_->invisibleRootItem();
    QList<QStandardItem*> row;
    for (const QString& t : texts) {
        row << new QStandardItem(t);
    }
    row[0]->setData(lazy, Qt::UserRole + 1); // 懒展开标记
    root->appendRow(row);
    return row[0];
}

void DataTree::SetOnNeedChildren(std::function<void(QStandardItem*)> cb) {
    on_need_children_ = std::move(cb);
}

void DataTree::SetOnContextMenu(std::function<void(const QPoint&)> cb) {
    on_context_ = std::move(cb);
}

std::vector<QStandardItem*> DataTree::SelectedItems() const {
    std::vector<QStandardItem*> out;
    const QModelIndexList idxs = selectionModel()->selectedRows();
    for (const QModelIndex& idx : idxs) {
        out.push_back(model_->itemFromIndex(idx));
    }
    return out;
}

void DataTree::contextMenuEvent(QContextMenuEvent* ev) {
    if (on_context_) {
        on_context_(ev->pos());
    }
    QTreeView::contextMenuEvent(ev);
}

} // namespace shine::data
