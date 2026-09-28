#include "pages/shell/CommandPalette.h"
#include "widget/theme/CssColor.h"

#include "widget/motion/Easing.h"
#include "widget/theme/Theme.h"
#include "widget/theme/Token.h"
#include "widget/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

namespace shine::app {

namespace {

// HTML 转义（结果行是富文本，标题里的 < & 必须放过）
[[nodiscard]] QString EscapeHtml(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('<')) out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>')) out += QStringLiteral("&gt;");
        else if (c == QLatin1Char('&')) out += QStringLiteral("&amp;");
        else out += c;
    }
    return out;
}

// 子序列模糊匹配：返回命中的下标集合（大小写不敏感）；不匹配返回空
[[nodiscard]] QVector<int> FuzzyHits(const QString& text, const QString& query) {
    QVector<int> hits;
    if (query.isEmpty()) {
        return hits;
    }
    int qi = 0;
    for (int i = 0; i < text.size() && qi < query.size(); ++i) {
        if (text.at(i).toLower() == query.at(qi).toLower()) {
            hits.push_back(i);
            ++qi;
        }
    }
    return qi == query.size() ? hits : QVector<int>{};
}

[[nodiscard]] QString ToHex(std::uint32_t rgba) {
    const QColor c = shine::widgets::TokenQColor(rgba);
    return c.name(QColor::HexArgb); // QSS 支持 #AARRGGBB
}

// Qt 富文本（QLabel HTML）的 CSS 子集只认 #RGB/#RRGGBB —— #AARRGGBB 会静默失效


[[nodiscard]] QString RichTitle(const QString& title, const QVector<int>& hits) {
    QString out;
    for (int i = 0; i < title.size(); ++i) {
        const QString ch = EscapeHtml(QString(title.at(i)));
        out += hits.contains(i)
                   ? QStringLiteral("<span style=\"color:%1\"><b>%2</b></span>")
                         .arg(shine::widget::CssRgb(theme::Current().accentPrimary), ch)
                   : ch;
    }
    return out;
}

[[nodiscard]] QString KindLabel(CommandItem::Kind k) {
    switch (k) {
    case CommandItem::Kind::Command: return QStringLiteral("命令");
    case CommandItem::Kind::Page: return QStringLiteral("页面");
    case CommandItem::Kind::Entity: return QStringLiteral("项目内容");
    }
    return {};
}

} // namespace

CommandPalette::CommandPalette(QWidget* host)
    : QWidget(host, Qt::Popup | Qt::FramelessWindowHint), host_(host) {
    setObjectName(QStringLiteral("commandPalette"));
    setFixedWidth(560);

    // 面板本体样式：运行时取 Token 构串（QSS 零字面色是源码纪律，变量构串是正规形态）
    const theme::ColorToken& t = theme::Current();
    setStyleSheet(QStringLiteral("CommandPalette { background: %1; border: 1px solid %2; "
                                 "border-radius: %3px; }")
                      .arg(ToHex(t.bgElevated), ToHex(t.lineNormal))
                      .arg(theme::radius::kLg));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                            theme::space::kSteps[2], theme::space::kSteps[2]);
    lay->setSpacing(theme::space::kSteps[2]);

    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(QStringLiteral("🔍 输入命令、页面或项目内容…"));
    edit_->setClearButtonEnabled(true);
    edit_->installEventFilter(this);
    lay->addWidget(edit_);

    list_ = new QListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setFocusPolicy(Qt::NoFocus); // 焦点始终在输入框，↑↓ 走事件过滤
    list_->installEventFilter(this);
    lay->addWidget(list_, 1);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(150); // P03 风险表：输入防抖 150ms
    connect(debounce_, &QTimer::timeout, this, [this] { Rebuild(edit_->text()); });
    connect(edit_, &QLineEdit::textChanged, this, [this](const QString&) {
        if (edit_->text().isEmpty()) {
            debounce_->stop();
            Rebuild(QString{});
        } else {
            debounce_->start();
        }
    });

    resize(560, 420);
}

void CommandPalette::SetCommands(std::vector<CommandItem> items) {
    commands_ = std::move(items);
}

void CommandPalette::SetEntityProvider(
    std::function<std::vector<CommandItem>(const QString&)> provider) {
    entity_provider_ = std::move(provider);
}

void CommandPalette::OpenPalette() {
    edit_->clear();
    Rebuild(QString{});
    Relayout();
    show();
    raise();
    edit_->setFocus();
}

void CommandPalette::ClosePalette() {
    hide();
}

bool CommandPalette::IsOpen() const {
    return isVisible();
}

std::vector<CommandPalette::Row> CommandPalette::Match(const QString& query) const {
    std::vector<Row> out;
    auto pushGroup = [&out, &query](const QString& header, std::vector<CommandItem> items) {
        std::vector<Row> group;
        for (CommandItem& item : items) {
            if (!query.isEmpty()) {
                const bool hit = !FuzzyHits(item.title, query).isEmpty() ||
                                 !FuzzyHits(item.detail, query).isEmpty();
                if (!hit) {
                    continue; // 只按标题/详情模糊；subtitle 是快捷键不参与
                }
            }
            group.push_back({std::move(item), true});
        }
        if (group.empty()) {
            return;
        }
        out.push_back({CommandItem{CommandItem::Kind::Command, header, {}, {}, {}}, false});
        for (Row& r : group) {
            out.push_back(std::move(r));
        }
    };

    // 命令 / 页面：静态注册里按 kind 分组
    std::vector<CommandItem> cmds;
    std::vector<CommandItem> pages;
    for (const CommandItem& c : commands_) {
        (c.kind == CommandItem::Kind::Page ? pages : cmds).push_back(c);
    }
    pushGroup(QStringLiteral("命令"), std::move(cmds));
    pushGroup(QStringLiteral("页面"), std::move(pages));
    if (entity_provider_) {
        pushGroup(QStringLiteral("项目内容"), entity_provider_(query));
    }
    return out;
}

void CommandPalette::Rebuild(const QString& query) {
    rows_ = Match(query);
    list_->clear();
    const QString hi = shine::widget::CssRgb(theme::Current().accentPrimary);
    const QString dim = shine::widget::CssRgb(theme::Current().textMuted);
    (void)hi;
    // QListWidgetItem 只渲染纯文本（HTML 会原样吐出标签，实测踩过）——
    // 富文本行用透明 QLabel 承载；WA_TransparentForMouseEvents 让点击/悬停落到条目上，
    // 选中高亮从透明背景底下透出。
    auto makeLabel = [this](const QString& html, int height) {
        auto* label = new QLabel(html, list_->viewport());
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setFixedHeight(height);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setContentsMargins(theme::space::kSteps[3], 0, theme::space::kSteps[3], 0);
        return label;
    };
    for (const Row& row : rows_) {
        auto* item = new QListWidgetItem(list_);
        if (!row.selectable) {
            item->setFlags(Qt::NoItemFlags);
            list_->setItemWidget(
                item, makeLabel(QStringLiteral("<span style=\"color:%1\">%2</span>")
                                    .arg(dim, EscapeHtml(row.item.title)),
                                26));
            item->setSizeHint(QSize(0, 26));
            continue;
        }
        const QVector<int> hits = FuzzyHits(row.item.title, query);
        QString html = QStringLiteral("<div style=\"font-size:14px;\">%1</div>")
                           .arg(RichTitle(row.item.title, hits));
        if (!row.item.detail.isEmpty()) {
            html += QStringLiteral("<div style=\"color:%1; font-size:11px;\">%2</div>")
                        .arg(dim, EscapeHtml(row.item.detail));
        }
        const int height = row.item.detail.isEmpty() ? 34 : 46;
        list_->setItemWidget(item, makeLabel(html, height));
        if (!row.item.subtitle.isEmpty()) {
            item->setData(Qt::UserRole + 1, row.item.subtitle); // 右侧快捷键（绘制简化：进工具提示）
            item->setToolTip(row.item.subtitle);
        }
        item->setSizeHint(QSize(0, height));
    }
    // 选中第一个可选行
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->flags() != Qt::NoItemFlags) {
            list_->setCurrentRow(i);
            break;
        }
    }
}

void CommandPalette::MoveSelection(int delta) {
    int row = list_->currentRow();
    for (int step = 0; step < list_->count(); ++step) {
        row += delta;
        if (row < 0 || row >= list_->count()) {
            return;
        }
        if (list_->item(row)->flags() != Qt::NoItemFlags) {
            list_->setCurrentRow(row);
            list_->scrollToItem(list_->item(row));
            return;
        }
    }
}

void CommandPalette::ExecuteCurrent() {
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return;
    }
    const CommandItem item = rows_[static_cast<std::size_t>(row)].item;
    ClosePalette();
    if (item.action) {
        item.action();
    }
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* ev) {
    if ((watched == edit_ || watched == list_) && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        switch (ke->key()) {
        case Qt::Key_Escape:
            ClosePalette();
            return true;
        case Qt::Key_Down:
            MoveSelection(+1);
            return true;
        case Qt::Key_Up:
            MoveSelection(-1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            ExecuteCurrent();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, ev);
}

int CommandPalette::QueryCount(const QString& query) const {
    int n = 0;
    for (const Row& r : Match(query)) {
        if (r.selectable) {
            ++n;
        }
    }
    return n;
}

QString CommandPalette::GroupSummary(const QString& query) const {
    int cmds = 0, pages = 0, entities = 0;
    for (const Row& r : Match(query)) {
        if (!r.selectable) {
            continue;
        }
        switch (r.item.kind) {
        case CommandItem::Kind::Command: ++cmds; break;
        case CommandItem::Kind::Page: ++pages; break;
        case CommandItem::Kind::Entity: ++entities; break;
        }
    }
    return QStringLiteral("命令 %1 / 页面 %2 / 项目内容 %3").arg(cmds).arg(pages).arg(entities);
}

QString CommandPalette::FirstTitle(const QString& query) const {
    for (const Row& r : Match(query)) {
        if (r.selectable) {
            return r.item.title;
        }
    }
    return {};
}

void CommandPalette::Relayout() {
    if (host_ == nullptr) {
        return;
    }
    const int x = (host_->width() - width()) / 2;
    const int y = host_->height() / 5;
    // Popup 是顶层窗口，坐标要走全局（踩过：按父件坐标摆位会飞出屏幕）
    move(host_->mapToGlobal(QPoint(x, y)));
}

void CommandPalette::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    Relayout();
}

} // namespace shine::app
