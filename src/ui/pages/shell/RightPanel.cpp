#include "ui/pages/shell/RightPanel.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/Token.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

namespace {

// 可折叠段：点头部展开/收起（QToolBox 不在 kit 样式覆盖面内，这里用 kit 件自拼）
//
// ⚠️ 段的 body 是**页面拥有**的（页面自己 new 出来、自己拿指针刷新数据），
// 段只是把它借过来显示。因此 ClearSections() 只能拆掉段壳，
// 绝不能 deleteLater 掉 body —— 否则页面手里的指针立刻悬空，
// 下一次刷新（SetPairs / 切页）就是野指针崩溃。
// 归还时 body 重新挂回 host_ 并隐藏，等下一次 AddSection 复用。
class Section : public QFrame {
  public:
    Section(const QString& title, QWidget* body, QWidget* parent = nullptr) : QFrame(parent) {
        setObjectName(QStringLiteral("sect")); // .sect：段间一条 line-subtle
        title_ = title;
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        header_ = new QPushButton(this);
        header_->setObjectName(QStringLiteral("sectHeader"));
        header_->setCursor(Qt::PointingHandCursor);
        header_->setCheckable(false);
        header_->setFlat(true);
        body_ = body;
        SetOpen(true);
        connect(header_, &QPushButton::clicked, this, [this] { SetOpen(!open_); });
        // .sect-b：p 2px 14px 14px（shell.css:367-370）—— 头部自带 10/14 内距，
        // 这里只补正文容器的上/下/左右
        auto* box = new QWidget(this);
        auto* box_lay = new QVBoxLayout(box);
        box_lay->setContentsMargins(14, 2, 14, 14);
        box_lay->setSpacing(0);
        box_lay->addWidget(body_);
        lay->addWidget(header_);
        lay->addWidget(box);
    }

    void SetOpen(bool on) {
        open_ = on;
        // .sect-h .tw：展开时 chevron 旋转 90°（shell.css:361-366）。
        // QSS 无 transform，这里用两个字符图标表示朝向（同一 widget 的两种字形）。
        header_->setText(on ? QStringLiteral("▾ %1").arg(title_)
                            : QStringLiteral("▸ %1").arg(title_));
    }

    // 把 body 从段里摘下来还给外壳（不销毁）：页面仍持有它。
    // 只换父级 + 隐藏；body 自带的布局不动。
    void ReleaseBody(QWidget* newParent) {
        if (body_ == nullptr) {
            return;
        }
        body_->setParent(newParent);
        body_->hide();
    }

  private:
    QPushButton* header_ = nullptr;
    QWidget* body_ = nullptr;
    QString title_;
    bool open_ = true;
};

} // namespace

RightPanel::RightPanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("rightPanel"));
    // .inspector：w280（shell.css:335-342）。收起态宽度由外壳给 0；
    // 打开态给一个够用的下限，避免检查器自己被压成残条（用户可拖宽到 560）。
    setMinimumWidth(280);
    setMaximumWidth(560);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // .inspector overflow-y: auto → 检查器整体可滚（段内容由页面提供，高度不定）
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setObjectName(QStringLiteral("inspectorScroll"));
    lay->addWidget(scroll_, 1);

    stack_ = new QStackedWidget(scroll_);

    // 空状态：没有选中对象时唯一可见的内容。不写任何「即将接入 / 占位」文案。
    empty_ = new QWidget(stack_);
    auto* el = new QVBoxLayout(empty_);
    el->setContentsMargins(14, 10, 14, 14);
    empty_text_ = new QLabel(QStringLiteral("未选中任何对象"), empty_);
    shine::widgets::SetKind(empty_text_, "statesub");
    empty_text_->setAlignment(Qt::AlignCenter);
    empty_text_->setWordWrap(true);
    el->addWidget(empty_text_);
    el->addStretch();
    stack_->addWidget(empty_);

    // 段容器：页面 AddSection() 往这里放
    host_ = new QWidget(stack_);
    host_lay_ = new QVBoxLayout(host_);
    host_lay_->setContentsMargins(0, 0, 0, 0);
    host_lay_->setSpacing(0);
    host_lay_->addStretch();
    stack_->addWidget(host_);
    scroll_->setWidget(stack_);
}

QWidget* RightPanel::AddSection(const QString& title, QWidget* body) {
    if (body == nullptr) {
        return nullptr;
    }
    // body 自带布局（页面 new 出来时就装好了）。这里只换父级，不碰它的布局：
    // 段壳的 lay->addWidget(body) 会把 body 挂进段的布局，body 自己的布局原样保留。
    // 上一版先 setParent(host_) 再由 Section 重复装载，Qt 会报
    // "QLayout: Attempting to add QLayout to QWidget which already has a layout"。
    auto* section = new Section(title, body, host_);
    host_lay_->insertWidget(host_lay_->count() - 1, section); // 插到末尾 stretch 之前
    if (stack_->currentWidget() == empty_) {
        stack_->setCurrentWidget(host_);
    }
    return section;
}

void RightPanel::SetSelection(const QString& what) {
    if (what.isEmpty()) {
        empty_text_->setText(QStringLiteral("未选中任何对象"));
        ClearSections();
        stack_->setCurrentWidget(empty_);
        return;
    }
    empty_text_->setText(what);
    if (stack_->currentWidget() == empty_) {
        stack_->setCurrentWidget(host_);
    }
}

void RightPanel::ClearSections() {
    while (host_lay_->count() > 1) { // 保留末尾 stretch
        QLayoutItem* item = host_lay_->takeAt(0);
        if (auto* section = dynamic_cast<Section*>(item->widget()); section != nullptr) {
            section->ReleaseBody(host_); // 只拆段壳；body 归页面所有，不能销毁
        }
        if (QWidget* w = item->widget(); w != nullptr) {
            w->deleteLater();
        }
        delete item;
    }
}

} // namespace shine::app
