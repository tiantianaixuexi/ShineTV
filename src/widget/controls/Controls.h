#pragma once
// shine::widgets —— 基础控件（P02-S5，UI.md §2.1）：Button / IconButton / Card / Tag /
// Badge / Kbd / Segmented / Spinner。行为列照 UI.md 兑现；样式全走全局 QSS
//（QssBuilder kit 段，零内联色值）；自绘色值取 theme::Current()。
#include "widget/controls/WidgetCommon.h"

#include <functional>
#include <memory>

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

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

} // namespace shine::widgets
