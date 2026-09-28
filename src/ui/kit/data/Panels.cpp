#include "ui/kit/data/Panels.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <yyjson.h>

#include <cstdio>
#include <utility>

namespace shine::data {
namespace {

QString NameOf(const QColor& c) { return c.name(QColor::HexRgb); }

// 双击复制到剪贴板的值标签
class CopyLabel : public QLabel {
  public:
    explicit CopyLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {
        setToolTip(QStringLiteral("双击复制"));
    }

  protected:
    void mouseDoubleClickEvent(QMouseEvent* ev) override {
        QApplication::clipboard()->setText(text());
        QLabel::mouseDoubleClickEvent(ev);
    }
};

} // namespace

// ================================================================== StatTile

StatTile::StatTile(QWidget* parent) : QFrame(parent) {
    widgets::SetKind(this, "card");
    widgets::SetVariant(this, "outlined");
    auto* col = new QVBoxLayout(this);
    // webui views.css .kpi：p14 16；label 11.5 w700 muted 在上 → value 24 w800 → foot 11.5 muted
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(2);

    auto* label = new QLabel(this);
    label->setObjectName(QStringLiteral("shineStatLabel"));
    widgets::SetKind(label, "statemeta");
    widgets::SetSemibold(label, true);
    QFont lf = label->font();
    lf.setPixelSize(11);
    label->setFont(lf);
    col->addWidget(label);

    auto* value = new QLabel(QStringLiteral("0"), this);
    value->setObjectName(QStringLiteral("shineStatValue"));
    widgets::SetKind(value, "statetitle");
    widgets::SetSemibold(value, true);
    QFont vf = value->font();
    vf.setPixelSize(24); // webui .kpi .k-value：24px w800
    value->setFont(vf);
    col->addWidget(value);

    auto* trend = new QLabel(this);
    trend->setObjectName(QStringLiteral("shineStatTrend"));
    widgets::SetKind(trend, "statemeta");
    col->addWidget(trend);
}

void StatTile::SetValue(const QString& v) {
    if (auto* l = findChild<QLabel*>(QStringLiteral("shineStatValue"))) {
        l->setText(v);
    }
}

void StatTile::SetLabel(const QString& text) {
    if (auto* l = findChild<QLabel*>(QStringLiteral("shineStatLabel"))) {
        l->setText(text);
    }
}

void StatTile::SetTrend(int percent) {
    auto* l = findChild<QLabel*>(QStringLiteral("shineStatTrend"));
    if (l == nullptr) {
        return;
    }
    // 涨跌色走 QSS tone 属性（.kpi .k-delta.up/.down），不内联 setStyleSheet 写色值。
    if (percent == 0) {
        l->setText(QStringLiteral("→ 持平"));
        widgets::SetVariant(l, "flat");
    } else if (percent > 0) {
        l->setText(QStringLiteral("↑ %1%").arg(percent));
        widgets::SetVariant(l, "up");
    } else {
        l->setText(QStringLiteral("↓ %1%").arg(-percent));
        widgets::SetVariant(l, "down");
    }
    widgets::Repolish(l);
}

// ==================================================================== KeyValue

KeyValue::KeyValue(QWidget* parent) : QFrame(parent) {
    widgets::SetKind(this, "field");
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
}

void KeyValue::SetPairs(const std::vector<std::pair<QString, QString>>& pairs) {
    auto* grid = qobject_cast<QGridLayout*>(layout());
    while (grid->count() > 0) {
        QLayoutItem* it = grid->takeAt(0);
        if (it->widget() != nullptr) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    int row = 0;
    for (const auto& [k, v] : pairs) {
        auto* key = new QLabel(k, this);
        widgets::SetKind(key, "fieldlabel");
        grid->addWidget(key, row, 0, Qt::AlignTop);

        if (v.size() > 40) { // 长值折叠
            auto* box = new QFrame(this);
            widgets::SetKind(box, "field");
            auto* col = new QVBoxLayout(box);
            col->setContentsMargins(0, 0, 0, 0);
            col->setSpacing(2);
            auto* shortL = new CopyLabel(v.left(40) + QStringLiteral("…"), box);
            auto* fullL = new CopyLabel(v, box);
            fullL->setWordWrap(true);
            fullL->hide();
            auto* toggle = new QPushButton(QStringLiteral("展开"), box);
            widgets::SetKind(toggle, "button");
            widgets::SetVariant(toggle, "ghost");
            widgets::SetSizeAttr(toggle, "sm");
            QObject::connect(toggle, &QPushButton::clicked, box,
                             [fullL, toggle] {
                                 const bool open = fullL->isHidden();
                                 fullL->setVisible(open);
                                 toggle->setText(open ? QStringLiteral("收起") : QStringLiteral("展开"));
                             });
            col->addWidget(shortL);
            col->addWidget(fullL);
            col->addWidget(toggle, 0, Qt::AlignLeft);
            grid->addWidget(box, row, 1);
        } else {
            auto* val = new CopyLabel(v, this); // 值可复制
            val->setWordWrap(true);
            widgets::SetKind(val, "statesub");
            grid->addWidget(val, row, 1);
        }
        ++row;
    }
    grid->setRowStretch(row, 1);
}

// ==================================================================== DiffView

DiffView::DiffView(QWidget* parent) : QFrame(parent) {
    widgets::SetKind(this, "field");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
}

void DiffView::SetInline(bool on) {
    setProperty("inlineMode", on);
    if (auto* g = findChild<QFrame*>(QStringLiteral("shineDiffBody"))) {
        g->setProperty("inlineMode", on);
        widgets::Repolish(g);
    }
}

void DiffView::SetRows(std::vector<Row> rows) {
    const theme::ColorToken& t = theme::Current();
    auto* col = qobject_cast<QVBoxLayout*>(layout());
    while (col->count() > 0) {
        QLayoutItem* it = col->takeAt(0);
        if (it->widget() != nullptr) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    auto* body = new QFrame(this);
    body->setObjectName(QStringLiteral("shineDiffBody"));
    widgets::SetKind(body, "statedetail");
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(4, 4, 4, 4);
    bl->setSpacing(1);

    const bool inlineMode = property("inlineMode").toBool();
    for (const Row& r : rows) {
        QColor bg = widgets::TokenQColor(t.bgSurface);
        QColor fg = widgets::TokenQColor(t.textPrimary);
        QString mark = QStringLiteral("  ");
        switch (r.kind) {
            case Kind::Add:
                bg = widgets::TokenQColor(t.statusOk);
                fg = widgets::TokenQColor(t.textInverse);
                mark = QStringLiteral("+ ");
                break;
            case Kind::Del:
                bg = widgets::TokenQColor(t.statusDanger);
                fg = widgets::TokenQColor(t.textInverse);
                mark = QStringLiteral("- ");
                break;
            case Kind::Change:
                bg = widgets::TokenQColor(t.statusWarn);
                fg = widgets::TokenQColor(t.textInverse);
                mark = QStringLiteral("~ ");
                break;
            case Kind::Same:
            default:
                break;
        }
        // 增删改三色来自 status.*（UI.md §2.2）；Same 用表面色
        const QString text = inlineMode
                                 ? mark + (r.kind == Kind::Del ? r.left : r.right)
                                 : (r.left + QStringLiteral("    │    ") + r.right);
        auto* line = new QLabel(text, body);
        line->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2; padding: 1px 6px;")
                .arg(NameOf(bg), NameOf(fg)));
        bl->addWidget(line);
    }
    bl->addStretch(1);
    col->addWidget(body);
}

// ===================================================================== JsonTree

JsonTree::JsonTree(QWidget* parent) : QTreeWidget(parent) {
    widgets::SetKind(this, "field");
    setColumnCount(2);
    setHeaderLabels({QStringLiteral("键"), QStringLiteral("值")});
    setAlternatingRowColors(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* it = itemAt(pos);
        if (it == nullptr) {
            return;
        }
        QMenu menu(this);
        menu.addAction(QStringLiteral("复制路径"), this, [this, it] {
            QApplication::clipboard()->setText(PathOf(it)); // 复制路径
        });
        menu.addAction(QStringLiteral("复制值"), this, [it] {
            QApplication::clipboard()->setText(it->text(1));
        });
        menu.exec(viewport()->mapToGlobal(pos));
    });
}

void JsonTree::SetJson(const QString& jsonText) {
    clear();
    const QByteArray utf8 = jsonText.toUtf8();
    yyjson_doc* doc = yyjson_read(utf8.constData(), static_cast<std::size_t>(utf8.size()), 0);
    if (doc == nullptr) {
        auto* err = new QTreeWidgetItem(this, {QStringLiteral("<解析失败>"), QString{}});
        err->setForeground(0, widgets::TokenQColor(theme::Current().statusDanger));
        return;
    }
    yyjson_val* root = yyjson_doc_get_root(doc); // 硬规矩：取 root 后再遍历
    BuildFrom(root, nullptr, QString{});
    yyjson_doc_free(doc);
    expandToDepth(1);
}

void JsonTree::BuildFrom(const yyjson_val* val, QTreeWidgetItem* parent, const QString& key) {
    const theme::ColorToken& t = theme::Current();
    auto add = [&](const QString& k, const QString& v, const QColor& col) {
        auto* it = parent != nullptr ? new QTreeWidgetItem(parent, {k, v})
                                     : new QTreeWidgetItem(this, {k, v});
        it->setForeground(0, widgets::TokenQColor(t.textPrimary)); // 类型着色在值列
        it->setForeground(1, col);
        return it;
    };

    if (yyjson_is_obj(val)) {
        QTreeWidgetItem* it = add(key.isEmpty() ? QStringLiteral("{…}") : key,
                                  QStringLiteral("{}"), widgets::TokenQColor(t.textMuted));
        yyjson_obj_iter iter = yyjson_obj_iter_with(val);
        yyjson_val* vk = nullptr;
        while ((vk = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* vv = yyjson_obj_iter_get_val(vk);
            BuildFrom(vv, it, QString::fromUtf8(yyjson_get_str(vk)));
        }
    } else if (yyjson_is_arr(val)) {
        QTreeWidgetItem* it = add(key.isEmpty() ? QStringLiteral("[…]") : key,
                                  QStringLiteral("[]"), widgets::TokenQColor(t.textMuted));
        std::size_t idx = 0;
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter = yyjson_arr_iter_with(val);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr) {
            BuildFrom(item, it, QStringLiteral("[%1]").arg(idx));
            ++idx;
        }
    } else if (yyjson_is_str(val)) {
        add(key, QString::fromUtf8(yyjson_get_str(val)), widgets::TokenQColor(t.accentSecondary));
    } else if (yyjson_is_num(val)) {
        char buf[64];
        if (yyjson_is_int(val)) {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(yyjson_get_sint(val)));
        } else {
            std::snprintf(buf, sizeof(buf), "%g", yyjson_get_real(val));
        }
        add(key, QString::fromLatin1(buf), widgets::TokenQColor(t.accentPrimary));
    } else if (yyjson_is_bool(val)) {
        add(key, yyjson_get_bool(val) ? QStringLiteral("true") : QStringLiteral("false"),
            widgets::TokenQColor(t.statusBusy));
    } else {
        add(key, QStringLiteral("null"), widgets::TokenQColor(t.textMuted));
    }
}

QString JsonTree::PathOf(QTreeWidgetItem* item) const {
    QStringList parts;
    for (QTreeWidgetItem* it = item; it != nullptr; it = it->parent()) {
        parts.prepend(it->text(0));
    }
    QString path;
    for (const QString& part : parts) {
        if (part.startsWith(QLatin1Char('['))) {
            path += part;
        } else if (part.startsWith(QLatin1Char('{'))) {
            continue;
        } else {
            path += (path.isEmpty() ? QString{} : QStringLiteral(".")) + part;
        }
    }
    return path;
}

} // namespace shine::data
