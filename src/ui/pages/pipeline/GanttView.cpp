#include "ui/pages/pipeline/GanttView.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"

#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// views.css:408-506 甘特段的关键数值，逐条对齐：
//   .grow-grid  grid-template-columns: 88px repeat(N, minmax(64px, 1fr)); font-size: 11.5px
//   .ghead      color: text-muted; font-weight: 700; padding: 6px 8px; border-bottom: line-normal
//   .gcell      margin: 4px 3px; height: 22px; radius 4; f10.5 w600; text-muted;
//               bg fill-muted; border 1px line-subtle
//               done = ok 18% 底 + ok 30% 边 + ok 字
//               run  = accent 底 + accent 边 + 3px 光环
//               stop = danger 14% 底 + danger 字
//   .grow-name  color: text-secondary; font-weight: 600; border-bottom: line-subtle
// 单元格内容：run = 转圈弧，done = ✓，stop = ✕，待办 = 空（与设计稿一致）。
constexpr int kCellHeight = 22;
constexpr int kCellRadius = 4;
constexpr int kNameWidth = 88;  // .grow-grid 第 0 列
constexpr int kStageWidth = 64; // minmax(64px, 1fr) 的下限
constexpr int kGridMinWidth = 640; // .grow-grid min-width: 640px

class GanttCellDelegate : public QStyledItemDelegate {
  public:
    explicit GanttCellDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        const QString text = index.data(Qt::DisplayRole).toString();
        const int state = index.data(kStateRole).toInt();

        // 行分隔线（webui .gantt .grow-name 的 border-bottom）
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, widgets::TokenQColor(theme::Current().fillSelected));
        }
        painter->setPen(widgets::TokenQColor(theme::Current().lineSubtle));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

        // 胶囊本体：margin 4px 3px → 上下 4、左右 3
        QRect pill = option.rect.adjusted(3, 4, -3, -4);
        QColor bg = widgets::TokenQColor(theme::Current().fillMuted);
        QColor border = widgets::TokenQColor(theme::Current().lineSubtle);
        QColor fg = widgets::TokenQColor(theme::Current().textMuted);
        if (state == 2) { // .gcell.done
            bg = widgets::TokenQColor(theme::Current().statusOk);
            bg.setAlpha(46); // ≈ ok 18%
            border = widgets::TokenQColor(theme::Current().statusOk);
            border.setAlpha(77); // ≈ ok 30%
            fg = widgets::TokenQColor(theme::Current().statusOk);
        } else if (state == 1) { // .gcell.run
            bg = widgets::TokenQColor(theme::Current().fillSelected);
            border = widgets::TokenQColor(theme::Current().accentPrimary);
            fg = widgets::TokenQColor(theme::Current().accentPrimary);
        } else if (state == 3) { // .gcell.stop
            bg = widgets::TokenQColor(theme::Current().statusDanger);
            bg.setAlpha(36); // ≈ danger 14%
            fg = widgets::TokenQColor(theme::Current().statusDanger);
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
        // .gcell.run 的 `box-shadow: 0 0 0 3px var(--accent-dim)`：
        // QSS 无 box-shadow，这里用一圈 accent-dim 的外描边等效画出来。
        if (state == 1) {
            QColor halo = widgets::TokenQColor(theme::Current().accentPrimary);
            halo.setAlpha(46); // ≈ --accent-dim .14
            painter->setPen(QPen(halo, 3.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(pill.adjusted(-3, -3, 3, 3), kCellRadius + 3, kCellRadius + 3);
        }
        painter->setPen(QPen(border, 1.0));
        painter->setBrush(bg);
        painter->drawRoundedRect(pill, kCellRadius, kCellRadius);

        // 内容：待办 = 空胶囊；run = 转圈弧；done = ✓；stop = ✕
        painter->setPen(QPen(fg, 1.6));
        if (state == 1) {
            const QRectF arc(pill.center().x() - 5.0, pill.center().y() - 5.0, 10.0, 10.0);
            painter->drawArc(arc, 30 * 16, 260 * 16);
        } else if (state == 2 || state == 3) {
            QFont f = option.font;
            f.setPixelSize(13);
            f.setWeight(QFont::DemiBold);
            painter->setFont(f);
            painter->drawText(pill, Qt::AlignCenter,
                             state == 2 ? QStringLiteral("✓") : QStringLiteral("✕"));
        } else {
            painter->setFont(option.font);
            painter->setPen(widgets::TokenQColor(theme::Current().textMuted));
            painter->drawText(pill, Qt::AlignCenter, text);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option)
        return {index.data(Qt::SizeHintRole).toSize().width(),
                kCellHeight + 8}; // 上下各留 4px 外边距，与 webui margin 4 对齐
    }

    static constexpr int kStateRole = Qt::UserRole + 41;
};

} // namespace

GanttView::GanttView(QWidget* parent) : QWidget(parent) {
    // 页面局部 QSS 的作用域前缀（只落本页子树，不影响其他页面的表格）
    setObjectName(QStringLiteral("ganttWs"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    title_ = widgets::SectionTitle(QStringLiteral("全书甘特 · 章节 × 阶段"), this);
    layout->addWidget(title_);

    // 无章节数据时用 kit 的 Empty 态（不编造演示行）
    empty_ = new widgets::EmptyState(
        QStringLiteral("▦"), QStringLiteral("还没有章节数据"),
        QStringLiteral("打开书库后这里按章 × 阶段链展开；没有数据就不画占位行。"), QString{}, this);
    layout->addWidget(empty_);

    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->verticalHeader()->setVisible(false);
    // .grow-grid 的第 0 列 88px 定宽，其余列 minmax(64px, 1fr)
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->setColumnWidth(0, kNameWidth);
    // 第 0 列是章名（普通文字），其余列走胶囊委托（每列都要挂，Qt 不继承）
    auto* cells = new GanttCellDelegate(table_);
    for (int column = 1; column < static_cast<int>(pipeline::AllStages().size()) + 1; ++column) {
        table_->setItemDelegateForColumn(column, cells);
    }
    layout->addWidget(table_, 1);
    SetChapters(0);
    ApplyQss();
}

void GanttView::changeEvent(QEvent* ev) {
    QWidget::changeEvent(ev);
    // 换肤走 setPalette + setStyleSheet，Qt 派发的是 PaletteChange / StyleChange；
    // ThemeChange 只在样式本身变化时才有，一并兜住。
    if (ev->type() == QEvent::ThemeChange || ev->type() == QEvent::PaletteChange ||
        ev->type() == QEvent::StyleChange) {
        ApplyQss(); // 表头 / 行底色都取自 token，换肤要跟着重建
    }
}

void GanttView::ApplyQss() {
    if (applying_qss_) {
        return; // 重入保护：setStyleSheet → StyleChange → ApplyQss → …（见 .h 里的说明）
    }
    applying_qss_ = true;
    // views.css:499-506 `.grow-name`：text-secondary + w600 + 底部 line-subtle；
    // views.css:467-475 `.ghead`：muted + w700 + p6 8 + 底部 line-normal
    // （全局 QSS 给的是 p8 12 + text-secondary + w600，差在这三处）。
    setStyleSheet(QStringLiteral(
                       "#ganttWs QTableWidget { border: none; border-radius: 0px;"
                       " background: transparent; }"
                       "#ganttWs QHeaderView::section { background-color: %1; color: %2;"
                       " border: none; border-bottom: 1px solid %3; padding: 6px 8px;"
                       " font-size: 11px; font-weight: 700; }"
                       "#ganttWs QTableWidget QTableView::item { color: %4; font-weight: 600;"
                       " padding: 0px 8px; border-bottom: 1px solid %5; }")
                       .arg(widget::CssRgb(theme::Current().bgPanel),
                            widget::CssRgb(theme::Current().textMuted),
                            widget::CssRgb(theme::Current().lineNormal),
                            widget::CssRgb(theme::Current().textSecondary),
                            widget::CssRgb(theme::Current().lineSubtle)));
    applying_qss_ = false;
}

void GanttView::RefreshEmpty() {
    const bool empty = table_->rowCount() == 0;
    table_->setVisible(!empty);
    empty_->setVisible(empty);
}

void GanttView::SetOwnTitle(bool on) {
    if (title_ != nullptr) {
        title_->setVisible(on);
    }
}

void GanttView::SetChapters(int count) {
    chapters_ = count;
    const auto stages = static_cast<int>(pipeline::AllStages().size());
    table_->setColumnCount(stages + 1);
    table_->setRowCount(count);
    QStringList headers{QStringLiteral("章")};
    for (const auto& stage : pipeline::AllStages()) headers << QString::fromStdString(stage.code);
    table_->setHorizontalHeaderLabels(headers);
    for (int chapter = 0; chapter < count; ++chapter) {
        auto* name = new QTableWidgetItem(QStringLiteral("第 %1 章").arg(chapter + 1));
        // .grow-name：text-secondary + w600（第 0 列由局部 QSS 给色）
        name->setData(Qt::SizeHintRole, QSize(kNameWidth, kCellHeight + 8));
        name->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table_->setItem(chapter, 0, name);
        for (int stage = 0; stage < stages; ++stage) {
            auto* item = new QTableWidgetItem();
            item->setData(GanttCellDelegate::kStateRole, 0);
            item->setData(Qt::SizeHintRole, QSize(kStageWidth, kCellHeight + 8));
            table_->setItem(chapter, stage + 1, item);
        }
    }
    // 阶段列按 64px 起步；表格整体不窄于 .grow-grid 的 min-width 640px
    for (int column = 1; column < stages + 1; ++column) {
        table_->setColumnWidth(column, kStageWidth);
    }
    table_->setMinimumWidth(kGridMinWidth);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    RefreshEmpty();
}

void GanttView::SetStageState(int chapter, pipeline::StageId stage, const QString& state) {
    if (chapter < 0 || chapter >= table_->rowCount()) return;
    const int column = static_cast<int>(stage) + 1;
    if (column < 0 || column >= table_->columnCount()) return;
    // 状态文案 → webui CELL_CLS：待办 0 / 进行 1 / 完成 2 / 停止 3
    int code = 0;
    if (state.contains(QStringLiteral("进行")) || state.contains(QStringLiteral("运行"))) {
        code = 1;
    } else if (state.contains(QStringLiteral("完成")) || state.contains(QStringLiteral("成功"))) {
        code = 2;
    } else if (state.contains(QStringLiteral("停止")) || state.contains(QStringLiteral("失败")) ||
               state.contains(QStringLiteral("降级"))) {
        code = 3;
    }
    auto* item = table_->item(chapter, column);
    if (item == nullptr) {
        item = new QTableWidgetItem();
        item->setData(Qt::SizeHintRole, QSize(kStageWidth, kCellHeight + 8));
        table_->setItem(chapter, column, item);
    }
    item->setText(state);
    item->setData(GanttCellDelegate::kStateRole, code);
    item->setToolTip(QStringLiteral("第 %1 章 · %2：%3")
                         .arg(chapter + 1)
                         .arg(QString::fromStdString(pipeline::StageCode(stage)), state));
}

QString GanttView::Probe() const {
    return QStringLiteral("chapters=%1; columns=%2").arg(chapters_).arg(table_->columnCount());
}

} // namespace shine::app
