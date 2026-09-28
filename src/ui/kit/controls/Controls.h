#pragma once
// shine::widgets —— 基础控件（P02-S5，UI.md §2.1）：Button / IconButton / Card / Tag /
// Badge / Kbd / Segmented / Spinner。行为列照 UI.md 兑现；样式全走全局 QSS
//（QssBuilder kit 段，零内联色值）；自绘色值取 theme::Current()。
#include "ui/kit/controls/WidgetCommon.h"

#include <functional>
#include <memory>

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <string>
#include <vector>

class QHBoxLayout;
class QVBoxLayout;

namespace shine::widgets {

// Spinner —— 转圈指示（sm/md/lg）；「减少动效」时静态弧
class Spinner : public QWidget {
  public:
    enum class Size { Sm, Md, Lg };
    explicit Spinner(Size s = Size::Md, QWidget* parent = nullptr);

    void SetRunning(bool on);
    [[nodiscard]] bool IsRunning() const { return timer_.isActive(); }
    [[nodiscard]] int DiameterPx() const;

  protected:
    void paintEvent(QPaintEvent* ev) override;

  private:
    Size size_;
    QTimer timer_;
    double angle_ = 0.0; // 角度制
};

// Button —— primary/secondary/ghost/danger × sm/md/lg
// loading 态 = 转圈 + 禁点 + 保持宽度（不跳动）
class Button : public QPushButton {
  public:
    enum class Variant { Primary, Secondary, Ghost, Danger };
    enum class Size { Sm, Md, Lg };

    Button(const QString& text, Variant v = Variant::Secondary, Size s = Size::Md,
           QWidget* parent = nullptr);

    void SetLoading(bool on);
    [[nodiscard]] bool IsLoading() const { return loading_; }

  protected:
    void resizeEvent(QResizeEvent* ev) override;

  private:
    void PlaceSpinner();

    bool loading_ = false;
    int fixed_w_ = 0;
    QString saved_text_;
    Spinner* spinner_ = nullptr;
};

// IconButton —— 字符图标钮（P03 换真图标）；必带 tooltip；active 高亮（活动栏）
class IconButton : public QPushButton {
  public:
    enum class Size { Sm, Md };
    IconButton(const QString& iconText, const QString& tooltip, Size s = Size::Md,
               QWidget* parent = nullptr);

    void SetActive(bool on);
    [[nodiscard]] bool IsActive() const { return active_; }

  private:
    bool active_ = false;
};

// Card —— flat/outlined/elevated + 可选 accent 左条；hover 抬升 1px（motion.fast）
class Card : public QFrame {
  public:
    enum class Variant { Flat, Outlined, Elevated };

    explicit Card(Variant v = Variant::Outlined, QWidget* parent = nullptr);

    void SetAccent(bool on); // 左侧 3px accent 条
    [[nodiscard]] bool HasAccent() const { return accent_ != nullptr; }
    // 内容排版（竖排 + 16px 内距，space.s4）
    [[nodiscard]] QVBoxLayout* BodyLayout();
    void SetOnClick(std::function<void()> cb) { on_click_ = std::move(cb); }

  protected:
    void enterEvent(QEnterEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;

  private:
    void Lift(int dy);

    QFrame* accent_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    bool lifted_ = false;
    std::function<void()> on_click_;
};

// Tag —— 可删 / 不可删；色来自 status.* / accent.*（tone 属性）
class Tag : public QFrame {
  public:
    Tag(const QString& text, const char* tone = "", bool removable = false, QWidget* parent = nullptr);

    void SetOnRemove(std::function<void()> cb) { on_remove_ = std::move(cb); }
    [[nodiscard]] QString Text() const { return label_ != nullptr ? label_->text() : QString{}; }

  private:
    QLabel* label_ = nullptr;
    std::function<void()> on_remove_;
};

// Badge —— 数字 / 点；超 99 显示 99+
class Badge : public QLabel {
  public:
    explicit Badge(QWidget* parent = nullptr);

    void SetCount(int n); // <0 → 隐藏；>99 → "99+"
    void SetDot(bool on);
    void SetTone(const char* tone); // ""（danger 默认）/ accent
};

// Kbd —— 快捷键标注（菜单 / 命令面板）
class Kbd : public QLabel {
  public:
    explicit Kbd(const QString& keys, QWidget* parent = nullptr);
};

// Segmented —— 2–4 段视图模式切换
class Segmented : public QFrame {
  public:
    explicit Segmented(const QStringList& items, QWidget* parent = nullptr);

    void SetCurrent(int index);
    [[nodiscard]] int Current() const { return current_; }
    void SetOnChanged(std::function<void(int)> cb) { on_changed_ = std::move(cb); }

  private:
    void Select(int index);

    QHBoxLayout* row_ = nullptr;
    std::vector<QPushButton*> items_;
    int current_ = 0;
    std::function<void(int)> on_changed_;
};

// Chip —— 筛选药丸（webui .chip：h26 / r-pill / f12 w600；选中 = accent 底 + accent 边）。
// 与 Tag 的区别：Tag 是只读状态标记，Chip 是**可点**的筛选项（checked 态 + 回调）。
// 用途：资产页类型筛选、分镜连续性 C1–C12 清单。
class Chip : public QPushButton {
  public:
    // tone：""（中性）/ ok / warn / danger / idle —— 只影响描边与文字色，不改几何
    Chip(const QString& text, const char* tone = "", QWidget* parent = nullptr);

    void SetOn(bool on);
    [[nodiscard]] bool IsOn() const { return on_; }
    void SetOnToggled(std::function<void(bool)> cb) { on_toggled_ = std::move(cb); }

    // 只换标签、不动计数（计数单独调 SetCount）
    void SetBaseText(const QString& text);
    // 尾部计数（webui .chip .cnt：独立小胶囊 / f10.5→11px / p0 6 / r-pill /
    // fill-muted 底 / text-muted 字；负数 = 不显示）。
    // 实现：按钮自身文字置空，改由内部 label_ + count_ 两个 QLabel 承载
    // （webui .chip 是 inline-flex + gap 6 的双元素结构）。按钮自绘文字会与
    // 子控件抢位置，必须走「空文字 + 子标签」这一条路。
    void SetCount(int n);

  private:
    void Apply();

    // 按钮自绘文字已清空，QPushButton::sizeHint 只按「无文字 + focus 框」算，
    // 完全不含内部布局的两个标签 —— 直接用会把 chip 压得只剩边框
    // （截图里「全[8]」标签被裁、计数压住文字）。必须交回布局尺寸 + 左右留白。
    [[nodiscard]] QSize SizeHintFromContent() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    class QHBoxLayout* row_ = nullptr;
    class QLabel* label_ = nullptr;
    class QLabel* count_label_ = nullptr;
    bool on_ = false;
    int count_ = -1;
    QString base_text_;
    std::function<void(bool)> on_toggled_;
};

} // namespace shine::widgets
