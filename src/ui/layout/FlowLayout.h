#pragma once
// shine::util — FlowLayout：CSS `flex-wrap: wrap` 的 Qt 替身
//
// 为什么需要（docs/90-reference/ui-design-parity-gaps.md 第三节第 2 条）：
// Qt Widgets 只有 QHBoxLayout（不换行，会把子控件压到 sizeHint 以下 = 截字）
// 和 QGridLayout（要事先知道列数，容器宽度变化时列分配会漂）。设计稿里
// `.tl-below`、`.flowwrap` 这类「一行放不下就折到下一行」的容器两者都不等价，
// 之前的做法是把子控件横向策略设成 Minimum 让它溢出——安全但不换行。
//
// 用法（首选 FlowContainer，它包掉了高度这件事）：
//     auto* box = new shine::util::FlowContainer(parent);
//     box->Flow()->setSpacing(8);              // gap，对齐设计稿的 gap
//     box->Flow()->AddWidget(chip);
//     box->Flow()->AddStretch();               // 行尾弹性留白（CSS flex:1）
//
// ⚠️ 三个必须知道的约束：
//  1. **高度必须显式给**。QLayout 无法反向通知父控件「我需要多高」，而 QWidget
//     布局系统不会主动调用 QLayout::heightForWidth()。直接把 FlowLayout 装进
//     QVBoxLayout，容器高度不会随换行结果变，底部会被裁 —— 要么用 FlowContainer，
//     要么在父控件的 resizeEvent 里 setFixedHeight(flow->heightForWidth(width()))。
//  2. 换行判定按子控件的 sizeHint。所以放进来的子控件仍应保持「不低于 sizeHint」
//     的横向策略，别为了塞进一行把 minimumSizeHint 调小（那会让 wrap 在错的位置断行）。
//  3. 一个 FlowContainer 里**不要**再嵌套需要撑满宽度的控件；行宽按容器宽度算。
//
// 颜色一律走 theme token；本文件不出现任何色值（tools/check-layers.ps1 rule 3）。

#include <QHash>
#include <QLayout>
#include <QList>
#include <QMargins>
#include <QRect>
#include <QSize>
#include <QSpacerItem>
#include <QWidget>
#include <QWidgetItem>

#include <algorithm>

namespace shine::util {

class FlowLayout final : public QLayout {
  public:
    explicit FlowLayout(QWidget* parent, int spacing = 0) : QLayout(parent), spacing_(spacing) {}

    ~FlowLayout() override {
        while (QLayoutItem* it = takeAt(0)) {
            delete it;
        }
    }

    // ---- QLayout 接口 ----
    // 注意：QLayout **没有** horizontalSpacing()/verticalSpacing() 虚函数
    // （那两个在 QBoxLayout/QGridLayout 上），所以这里不能写 override，
    // 否则编译期报「marked override, but does not override」。
    [[nodiscard]] int Spacing() const { return spacing_; }
    void setSpacing(int spacing) { spacing_ = spacing; invalidate(); }

    void addItem(QLayoutItem* item) override {
        if (item != nullptr) {
            items_.append(item);
            invalidate();
        }
    }

    [[nodiscard]] int count() const override { return static_cast<int>(items_.size()); }
    [[nodiscard]] QLayoutItem* itemAt(int index) const override {
        return index >= 0 && index < items_.size() ? items_.at(index) : nullptr;
    }

    QLayoutItem* takeAt(int index) override {
        if (index < 0 || index >= items_.size()) {
            return nullptr;
        }
        QLayoutItem* it = items_.takeAt(index);
        invalidate();
        return it;
    }

    // 行数随宽度变化，没有固定的扩张方向
    [[nodiscard]] Qt::Orientations expandingDirections() const override { return {}; }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override {
        return DoLayout(QRect(0, 0, width, 0), /*test=*/true);
    }

    void setGeometry(const QRect& rect) override { DoLayout(rect, /*test=*/false); }

    [[nodiscard]] QSize sizeHint() const override {
        const QSize s = MinimumSize();
        return QSize(s.width(), heightForWidth(s.width()));
    }

    [[nodiscard]] QSize minimumSize() const override { return MinimumSize(); }

    // ---- 用法糖：与 QLayout::addWidget 同名，页面迁移时不必改调用点 ----
    void AddWidget(QWidget* w) {
        if (w != nullptr) {
            addWidget(w);
        }
    }

    // 行尾弹性留白（CSS flex:1）：吃掉本行剩余宽度，不触发换行
    void AddStretch() {
        // QSpacerItem 的构造是 (w, h, 水平策略, 垂直策略)，没有 expanding/shrinking 参数
        addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
    }

  private:
    [[nodiscard]] QSize MinimumSize() const {
        QSize s;
        for (const QLayoutItem* it : items_) {
            s = s.expandedTo(it->minimumSize());
        }
        return s;
    }

    [[nodiscard]] QSize HintOf(const QLayoutItem* it) const {
        if (const auto* spacer = dynamic_cast<const QSpacerItem*>(it); spacer != nullptr) {
            return spacer->sizeHint();
        }
        return it->sizeHint();
    }

    [[nodiscard]] bool IsElastic(const QLayoutItem* it) const {
        const auto* spacer = dynamic_cast<const QSpacerItem*>(it);
        return spacer != nullptr &&
               spacer->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding;
    }

    // 返回内容高度。test=true 只算不动几何（heightForWidth/sizeHint 用），
    // test=false 真正 setGeometry。
    [[nodiscard]] int DoLayout(const QRect& rect, bool test) const {
        const QMargins m = contentsMargins();
        const QRect box = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        const int gap = spacing_;

        int x = box.x();
        int y = box.y();
        int lineHeight = 0;

        for (QLayoutItem* it : items_) {
            if (it->isEmpty()) {
                continue;
            }

            // 弹性空隙：吃掉本行剩余宽度（CSS flex:1），不参与换行判定
            if (IsElastic(it)) {
                const int rest = box.right() + 1 - x;
                if (rest > 0) {
                    if (!test) {
                        it->setGeometry(QRect(QPoint(x, y), QSize(rest, lineHeight)));
                    }
                    x += rest;
                }
                continue;
            }

            const QSize hint = HintOf(it);
            // 当前行已有内容，且加上本项（含 gap）会超宽 → 折到下一行
            if (lineHeight > 0 && x + hint.width() > box.right() + 1) {
                x = box.x();
                y += lineHeight + gap;
                lineHeight = 0;
            }

            if (!test) {
                it->setGeometry(QRect(QPoint(x, y), hint));
            }
            x += hint.width() + gap;
            lineHeight = std::max(lineHeight, hint.height());
        }

        return y + lineHeight + m.bottom();
    }

    QList<QLayoutItem*> items_;
    int spacing_ = 0;
};

// —— 需要「高度随宽度自动长」时的容器 ——
// QWidget 布局系统不会主动调 QLayout::heightForWidth()，所以直接把 FlowLayout
// 装进 QVBoxLayout 时容器高度不随换行变（内容被裁）。这个薄容器接管：自身高度
// 永远等于当前宽度下布局需要的高度，并按 MinimumExpanding 参与父布局分配。
class FlowContainer : public QWidget {
  public:
    explicit FlowContainer(QWidget* parent = nullptr) : QWidget(parent) {
        flow_ = new FlowLayout(this, 0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }

    [[nodiscard]] FlowLayout* Flow() const { return flow_; }

  protected:
    void resizeEvent(QResizeEvent* ev) override {
        QWidget::resizeEvent(ev);
        SyncHeight(ev->size().width());
    }

  private:
    void SyncHeight(int width) {
        if (width <= 0 || flow_ == nullptr) {
            return;
        }
        const int want = flow_->heightForWidth(width);
        if (want <= 0) {
            return;
        }
        // 直接改 minimumHeight（而不是 setFixedHeight）：父布局按 Minimum 策略
        // 分配空间即可，不必反复改自己的几何引发重排风暴。
        // ⚠️ 必须**无条件**同步而不是「只增不减」：窗口变窄时换行数增加、需要更多高度，
        // 变宽时行数减少、需要收回去。只增不减会让容器永远停在最宽那档的高度，
        // 收窄后底部留一大片空白。
        if (minimumHeight() != want) {
            setMinimumHeight(want);
            updateGeometry();
        }
    }

    FlowLayout* flow_ = nullptr; // 本对象的孩子（构造时挂上，析构随 QWidget 走）
};

} // namespace shine::util
