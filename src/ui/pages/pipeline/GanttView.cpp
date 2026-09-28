#include "ui/pages/pipeline/GanttView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// webui .gantt .gcell：m4 3 / h22 / r-xs(4) / f10.5 w600 / fill-muted 底 + line-subtle 边；
// done = ok 18% 底 + ok 30% 边；run = accent 底 + accent 边 + 3px 光环；stop = danger 底。
// 单元格内容：run = spinner，done = ✓，stop = ✕，待办 = 空（与设计稿一致，不再写「完成」两个字）。
constexpr int kCellHeight = 22;
constexpr int kCellRadius = 4;

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

        // 胶囊本体
        QRect pill = option.rect.adjusted(3, 4, -3, -4);
        QColor bg = widgets::TokenQColor(theme::Current().fillMuted);
        QColor border = widgets::TokenQColor(theme::Current().lineSubtle);
        QColor fg = widgets::TokenQColor(theme::Current().textMuted);
        if (state == 2) { // done
            bg = widgets::TokenQColor(theme::Current().statusOk);
            bg.setAlpha(46); // ≈ ok 18%
            border = widgets::TokenQColor(theme::Current().statusOk);
            border.setAlpha(77); // ≈ ok 30%
            fg = widgets::TokenQColor(theme::Current().statusOk);
        } else if (state == 1) { // run
            bg = widgets::TokenQColor(theme::Current().fillSelected);
            border = widgets::TokenQColor(theme::Current().accentPrimary);
            fg = widgets::TokenQColor(theme::Current().accentPrimary);
        } else if (state == 3) { // stop
            bg = widgets::TokenQColor(theme::Current().statusDanger);
            bg.setAlpha(36); // ≈ danger 14%
            fg = widgets::TokenQColor(theme::Current().statusDanger);
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
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
            painter->drawText(pill, Qt::AlignCenter, state == 2 ? QStringLiteral("✓")
                                                               : QStringLiteral("✕"));
        } else {
            painter->setFont(option.font);
            painter->setPen(widgets::TokenQColor(theme::Current().textMuted));
            painter->drawText(pill, Qt::AlignCenter, text);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return {index.data(Qt::SizeHintRole).toSize().width(),
                kCellHeight + 8}; // 上下各留 4px 外边距，与 webui margin 4 对齐
    }

    static constexpr int kStateRole = Qt::UserRole + 41;
};

} // namespace

GanttView::GanttView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    title_ = widgets::SectionTitle(QStringLiteral("全书甘特 · 章节 × 阶段"), this);
    layout->addWidget(title_);
    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // 第 0 列是章名（普通文字），其余列走胶囊委托（每列都要挂，Qt 不继承）
    auto* cells = new GanttCellDelegate(table_);
    for (int column = 1; column < static_cast<int>(pipeline::AllStages().size()) + 1; ++column) {
        table_->setItemDelegateForColumn(column, cells);
    }
    layout->addWidget(table_, 1);
    SetChapters(3);
}

void GanttView::SetOwnTitle(bool on) {
    if (title_ != nullptr) {
        title_->setVisible(on);
    }
}

void GanttView::SetChapters(int count) {
    chapters_ = count;
    const int stages = static_cast<int>(pipeline::AllStages().size());
    table_->setColumnCount(stages + 1);
    table_->setRowCount(count);
    QStringList headers{QStringLiteral("章")};
    for (const auto& stage : pipeline::AllStages()) headers << QString::fromStdString(stage.code);
    table_->setHorizontalHeaderLabels(headers);
    for (int chapter = 0; chapter < count; ++chapter) {
        auto* name = new QTableWidgetItem(QStringLiteral("第 %1 章").arg(chapter + 1));
        name->setData(Qt::SizeHintRole, QSize(88, kCellHeight + 8));
        table_->setItem(chapter, 0, name);
        for (std::size_t stage = 0; stage < pipeline::AllStages().size(); ++stage) {
            auto* item = new QTableWidgetItem();
            item->setData(GanttCellDelegate::kStateRole, 0);
            item->setData(Qt::SizeHintRole, QSize(64, kCellHeight + 8));
            table_->setItem(chapter, static_cast<int>(stage) + 1, item);
        }
    }
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
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
        item->setData(Qt::SizeHintRole, QSize(64, kCellHeight + 8));
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
