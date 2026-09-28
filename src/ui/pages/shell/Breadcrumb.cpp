#include "ui/pages/shell/Breadcrumb.h"

#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>

namespace shine::app {

Breadcrumb::Breadcrumb(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("breadcrumb"));
    setFixedHeight(34); // webui shell.css:224 .crumbs：h34
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 0, 16, 0); // webui .crumbs：padding 0 16
    lay->setSpacing(7);

    // 右侧快捷键提示（webui Shell.jsx:138 `.tiny dim`）：占位不参与 Rebuild 的清理循环
    hint_ = new QLabel(QStringLiteral("Ctrl+B 侧栏 · Ctrl+J 底栏 · Ctrl+I 检查器 · Ctrl+K 命令"), this);
    hint_->setObjectName(QStringLiteral("crumbHint"));
    hint_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lay->addWidget(hint_, 0, Qt::AlignRight | Qt::AlignVCenter);
}

void Breadcrumb::SetPath(const QStringList& crumbs) {
    crumbs_ = crumbs;
    Rebuild();
}

void Breadcrumb::Rebuild() {
    auto* lay = qobject_cast<QHBoxLayout*>(layout());
    // 清掉全部旧段（构造期与历次 Rebuild 都可能留下 stretch，必须一并清掉，
    // 否则段落在两个 stretch 之间被推到中间，看起来像"面包屑浮在顶栏中央"）。
    // 右侧快捷键提示要留下，所以最后再补回末尾（Rebuild 之前它是 index 0）。
    QWidget* hint = hint_;
    while (lay->count() > 0) {
        QLayoutItem* item = lay->takeAt(0);
        if (item->widget() != nullptr && item->widget() != hint) {
            // 先 hide() 再 deleteLater()：deleteLater 要等事件循环才回收，而 Rebuild
            // 常常是「点某个段 → SetPath → Rebuild」，被删的正是在发信号的那个按钮，
            // 不能就地 delete（信号还在派发中）。但只 deleteLater 不够——旧段在事件循环
            // 跑起来之前仍会被绘制，于是和新段**叠在同一处**，截图上就是面包屑文字重叠、
            // 分隔符成对出现。先 hide() 立刻停止绘制，deleteLater() 仍负责安全回收。
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < crumbs_.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel(QStringLiteral("›"), this);
            sep->setObjectName(QStringLiteral("crumbSep"));
            lay->addWidget(sep);
        }
        // 末段是当前位置（不可点），前面的段可点回
        if (i + 1 == crumbs_.size()) {
            auto* cur = new QLabel(crumbs_[i], this);
            cur->setObjectName(QStringLiteral("crumbHere")); // 当前段：text-primary + w600
            lay->addWidget(cur);
        } else {
            auto* btn = new shine::widgets::Button(crumbs_[i],
                                                   shine::widgets::Button::Variant::Ghost,
                                                   shine::widgets::Button::Size::Sm, this);
            connect(btn, &shine::widgets::Button::clicked, this, [this, i] {
                if (on_pick_) {
                    on_pick_(i);
                }
            });
            lay->addWidget(btn);
        }
    }
    lay->addStretch();
    lay->addWidget(hint, 0, Qt::AlignRight | Qt::AlignVCenter);
}

} // namespace shine::app
