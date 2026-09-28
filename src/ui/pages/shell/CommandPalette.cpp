#include "ui/pages/shell/CommandPalette.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/theme/CssColor.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

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
    // .cmdk：width min(560px, 100vw - 40px) / max-height 60vh / r-lg / bg-overlay
    // （shell.css:487-499）
    setFixedWidth(560);
    // webui shell.css:489 `box-shadow: var(--shadow-2)`（QSS 无 box-shadow）
    widgets::ApplyShadow(this, widgets::ShadowLevel::Lg);

    // 面板本体样式：运行时取 Token 构串（QSS 零字面色是源码纪律，变量构串是正规形态）
    const theme::ColorToken& t = theme::Current();
    setStyleSheet(QStringLiteral("CommandPalette { background: %1; border: 1px solid %2; "
                                 "border-radius: %3px; }"
                                 "CommandPalette QLineEdit { background: transparent; "
                                 "border: none; font-size: 15px; color: %4; }"
                                 "CommandPalette QLabel#cmdkInputIcon { color: %5; }"
                                 "CommandPalette QLabel#cmdkFoot { color: %5; font-size: 11px; }"
                                 "CommandPalette QLabel#cmdkEmpty { color: %5; font-size: 12px; }")
                      .arg(ToHex(t.bgOverlay), ToHex(t.lineNormal),
                           QString::number(theme::radius::kLg),
                           shine::widget::CssRgb(t.textPrimary),
                           shine::widget::CssRgb(t.textMuted)));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // .cmdk-input：p 14px 16px + 底部细线 + 🔍 + 输入框 + Esc 标注
    // （shell.css:500-518 / CommandPalette.jsx:67-71）
    auto* inputRow = new QFrame(this);
    inputRow->setObjectName(QStringLiteral("cmdkInputRow"));
    input_row_ = inputRow;
    auto* in_lay = new QHBoxLayout(inputRow);
    in_lay->setContentsMargins(16, 14, 16, 14);
    in_lay->setSpacing(10);
    auto* icon = new QLabel(QStringLiteral("🔍"), inputRow);
    icon->setObjectName(QStringLiteral("cmdkInputIcon"));
    in_lay->addWidget(icon);
    edit_ = new QLineEdit(inputRow);
    edit_->setPlaceholderText(QStringLiteral("搜索命令、页面、项目内容…"));
    edit_->setClearButtonEnabled(true);
    edit_->setFrame(false);
    edit_->installEventFilter(this);
    in_lay->addWidget(edit_, 1);
    auto* esc = new widgets::Kbd(QStringLiteral("Esc"), inputRow);
    in_lay->addWidget(esc);
    lay->addWidget(inputRow);

    // .cmdk-list padding 6（shell.css:519-522）。QListWidget 的 viewport margin 是
    // protected，这里用一个 6px 内距的外壳承担（QSS 里 ::item 的 margin 不生效）。
    auto* listBox = new QFrame(this);
    listBox->setObjectName(QStringLiteral("cmdkListBox"));
    list_box_ = listBox;
    auto* box_lay = new QVBoxLayout(listBox);
    box_lay->setContentsMargins(6, 6, 6, 6);
    box_lay->setSpacing(0);
    list_ = new QListWidget(listBox);
    list_->setObjectName(QStringLiteral("cmdkList"));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setFocusPolicy(Qt::NoFocus); // 焦点始终在输入框，↑↓ 走事件过滤
    list_->installEventFilter(this);
    box_lay->addWidget(list_, 1);
    lay->addWidget(listBox, 1);

    empty_label_ = new QLabel(QStringLiteral("没有匹配的命令，换个关键词试试"), list_->viewport());
    empty_label_->setObjectName(QStringLiteral("cmdkEmpty"));
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->hide();

    // .cmdk-foot：p 9px 16px + 顶部细线 + 快捷键说明（shell.css:562-569）
    auto* foot = new QFrame(this);
    foot->setObjectName(QStringLiteral("cmdkFootRow"));
    foot_ = foot;
    auto* f_lay = new QHBoxLayout(foot);
    f_lay->setContentsMargins(16, 9, 16, 9);
    f_lay->setSpacing(14);
    const auto mk = [foot](const QString& k, const QString& text) -> QWidget* {
        auto* box = new QWidget(foot);
        auto* bl = new QHBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(4);
        bl->addWidget(new widgets::Kbd(k, box));
        auto* lbl = new QLabel(text, box);
        lbl->setObjectName(QStringLiteral("cmdkFoot"));
        bl->addWidget(lbl);
        return box;
    };
    f_lay->addWidget(mk(QStringLiteral("↑"), QStringLiteral("↓ 选择")));
    f_lay->addWidget(mk(QStringLiteral("↵"), QStringLiteral("执行")));
    f_lay->addWidget(mk(QStringLiteral("Esc"), QStringLiteral("关闭")));
    f_lay->addStretch();
    lay->addWidget(foot);

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

    Relayout();
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
    const QString dim = shine::widget::CssRgb(theme::Current().textMuted);
    const QString muted = shine::widget::CssRgb(theme::Current().textSecondary);
    // QListWidgetItem 只渲染纯文本（HTML 会原样吐出标签，实测踩过）——
    // 富文本行用透明 QLabel 承载；WA_TransparentForMouseEvents 让点击/悬停落到条目上，
    // 选中高亮从透明背景底下透出。
    auto makeLabel = [this](const QString& html, int height) {
        auto* label = new QLabel(html, list_->viewport());
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setFixedHeight(height);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setContentsMargins(12, 0, 12, 0); // .cmdk-item p 9px 12px
        return label;
    };
    bool any = false;
    for (const Row& row : rows_) {
        auto* item = new QListWidgetItem(list_);
        if (!row.selectable) {
            item->setFlags(Qt::NoItemFlags);
            // .cmdk-group：p 8px 10px 4px / f10 w700 letter-spacing .08em
            list_->setItemWidget(
                item, makeLabel(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                                    .arg(dim, EscapeHtml(row.item.title)),
                                26));
            item->setSizeHint(QSize(0, 26));
            continue;
        }
        any = true;
        const QVector<int> hits = FuzzyHits(row.item.title, query);
        // .cmdk-item .hint margin-left:auto —— 快捷键右对齐（用表格布局把 hint 顶到右侧）
        QString html = row.item.subtitle.isEmpty()
                           ? RichTitle(row.item.title, hits)
                           : QStringLiteral(
                                 "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                                 "<tr><td valign=\"middle\" width=\"*\">%1</td>"
                                 "<td valign=\"middle\" align=\"right\">"
                                 "<span style=\"color:%2; font-size:11px;\">%3</span></td>"
                                 "</tr></table>")
                                 .arg(RichTitle(row.item.title, hits), dim,
                                      EscapeHtml(row.item.subtitle));
        QString body = QStringLiteral("<div style=\"font-size:13px;\">%1</div>").arg(html);
        if (!row.item.detail.isEmpty()) {
            body += QStringLiteral("<div style=\"color:%1; font-size:11px;\">%2</div>")
                        .arg(muted, EscapeHtml(row.item.detail));
        }
        const int height = row.item.detail.isEmpty() ? 36 : 48;
        list_->setItemWidget(item, makeLabel(body, height));
        if (!row.item.subtitle.isEmpty()) {
            item->setToolTip(row.item.subtitle);
        }
        item->setSizeHint(QSize(0, height));
    }
    if (empty_label_ != nullptr) {
        empty_label_->setVisible(!any);
        if (!any && empty_label_->parentWidget() != nullptr) {
            // 空态盖在列表视口上（p28 居中，shell.css:556-561）
            QRect r = empty_label_->parentWidget()->rect();
            empty_label_->setGeometry(r.adjusted(0, 0, 0, 0));
        }
    }
    // 选中第一个可选行
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->flags() != Qt::NoItemFlags) {
            list_->setCurrentRow(i);
            break;
        }
    }
    // 面板高度随内容走，上限 60vh（.cmdk height fit-content / max-height 60vh）
    AdjustHeight();
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

void CommandPalette::AdjustHeight() {
    if (host_ == nullptr) {
        return;
    }
    // .cmdk height fit-content / max-height 60vh（shell.css:489-490）
    // 内容高度按各条目 sizeHint 累加（QListWidget::sizeHint 不随条目数变化，不能直接用）
    int content = 0;
    for (int i = 0; i < list_->count(); ++i) {
        content += list_->sizeHintForRow(i) > 0 ? list_->sizeHintForRow(i)
                                               : list_->item(i)->sizeHint().height();
    }
    content += list_box_->contentsMargins().top() + list_box_->contentsMargins().bottom();
    // 上下两块固定区（输入行 + 页脚）用各自的 sizeHint 实测，不写死常数
    const int chrome = input_row_->sizeHint().height() + foot_->sizeHint().height();
    const int cap = host_->height() * 3 / 5;
    setFixedHeight(std::max(160, std::min(content + chrome, cap)));
    Relayout();
}

void CommandPalette::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    Relayout();
}

} // namespace shine::app
