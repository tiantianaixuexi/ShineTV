#pragma once
// shine::widgets —— 输入族（P02-S5，UI.md §2.1）：Field / TextInput / TextArea / Select /
// Slider / NumberInput / Toggle / Checkbox / Radio / SearchBox。
// 样式走全局 QSS（shineKind 选择器）；自绘（Toggle/Checkbox/Radio）读 theme::Current()。
#include "widget/controls/WidgetCommon.h"

#include <functional>
#include <vector>

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

class QSlider;
class QVBoxLayout;

namespace shine::widgets {

// Field —— 标签 左/上 + help 文案 + error 文案（error 态描边 status.danger）
class Field : public QFrame {
  public:
    enum class LabelPos { Left, Top };

    Field(const QString& label, LabelPos pos, QWidget* control, QWidget* parent = nullptr);

    void SetHelp(const QString& text);
    void SetError(const QString& text); // 空串 = 清除
    [[nodiscard]] bool HasError() const;
    [[nodiscard]] QWidget* Control() const { return control_; }

  private:
    QWidget* control_ = nullptr;
    QLabel* help_ = nullptr;
    QLabel* error_ = nullptr;
};

// TextInput —— 单行：placeholder、清空按钮、Esc 清空
class TextInput : public QFrame {
  public:
    explicit TextInput(QWidget* parent = nullptr);

    [[nodiscard]] QLineEdit* Edit() const { return edit_; }
    void SetText(const QString& t);
    [[nodiscard]] QString Text() const;
    void SetPlaceholder(const QString& p);
    void SetError(bool on); // error 态描边
    void SetOnChanged(std::function<void(const QString&)> cb) { on_changed_ = std::move(cb); }

  protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

  private:
    QLineEdit* edit_ = nullptr;
    QPushButton* clear_ = nullptr;
    std::function<void(const QString&)> on_changed_;
};

// TextArea —— 多行：字数计数、超限变 status.danger
class TextArea : public QFrame {
  public:
    explicit TextArea(int maxChars = 500, QWidget* parent = nullptr);

    [[nodiscard]] QPlainTextEdit* Edit() const { return edit_; }
    void SetText(const QString& t);
    [[nodiscard]] QString Text() const;
    void SetLimit(int n);
    [[nodiscard]] bool IsOverLimit() const;

  private:
    void RefreshCounter();

    QPlainTextEdit* edit_ = nullptr;
    QLabel* counter_ = nullptr;
    int limit_ = 500;
};

// Select —— 可搜索 / 不可搜索；分组、多选
class Select : public QFrame {
  public:
    struct Item {
        QString text;
        QString group; // 空 = 无分组
        bool checked = false;
    };

    Select(bool searchable, bool multi, QWidget* parent = nullptr);

    void SetItems(std::vector<Item> items);
    void SetPlaceholder(const QString& p);
    [[nodiscard]] std::vector<QString> Checked() const;
    void SetOnChanged(std::function<void()> cb) { on_changed_ = std::move(cb); }

  protected:
    void mousePressEvent(QMouseEvent* ev) override;

  private:
    void TogglePopup();
    void BuildPopup(const QString& filter);
    void SyncSummary();

    bool searchable_ = false;
    bool multi_ = false;
    QPushButton* summary_ = nullptr;
    std::vector<Item> items_;
    QWidget* popup_ = nullptr;
    std::function<void()> on_changed_;
};

// Slider —— 连续 / 刻度；显示当前值 + 单位
class Slider : public QFrame {
  public:
    Slider(bool withTicks, double min, double max, const QString& unit, QWidget* parent = nullptr);

    void SetValue(double v);
    [[nodiscard]] double Value() const;
    void SetOnChanged(std::function<void(double)> cb) { on_changed_ = std::move(cb); }
    [[nodiscard]] QSlider* Control() const { return slider_; }

  private:
    void RefreshLabel();

    QSlider* slider_ = nullptr;
    QLabel* value_ = nullptr;
    QString unit_;
    double scale_ = 1.0; // 滑轨整数化倍率
    std::function<void(double)> on_changed_;
};

// NumberInput —— 整数 / 浮点；增减按钮、范围提示
class NumberInput : public QFrame {
  public:
    NumberInput(bool isFloat, double min, double max, QWidget* parent = nullptr);

    void SetValue(double v);
    [[nodiscard]] double Value() const;
    void SetOnChanged(std::function<void(double)> cb) { on_changed_ = std::move(cb); }

  private:
    void Step(int dir);

    bool is_float_ = true;
    double min_ = 0.0, max_ = 100.0;
    QLineEdit* edit_ = nullptr;
    std::function<void(double)> on_changed_;
};

// Toggle —— 开关（自绘；knob 滑动 motion.fast，减少动效时静止）
class Toggle : public QWidget {
  public:
    explicit Toggle(QWidget* parent = nullptr);

    void SetChecked(bool on);
    [[nodiscard]] bool IsChecked() const { return checked_; }
    void SetOnToggled(std::function<void(bool)> cb) { on_toggled_ = std::move(cb); }

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;

  private:
    bool checked_ = false;
    double knob_t_ = 0.0; // 0..1
    std::function<void(bool)> on_toggled_;
};

// Checkbox —— 支持 partial（全选/半选）
class Checkbox : public QWidget {
  public:
    explicit Checkbox(const QString& text, QWidget* parent = nullptr);

    void SetChecked(bool on);
    [[nodiscard]] bool IsChecked() const { return checked_; }
    void SetPartial(bool on);
    [[nodiscard]] bool IsPartial() const { return partial_; }
    void SetOnToggled(std::function<void(bool)> cb) { on_toggled_ = std::move(cb); }

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;

  private:
    QString text_;
    bool checked_ = false;
    bool partial_ = false;
    std::function<void(bool)> on_toggled_;
};

// Radio —— 单选（成组由容器负责互斥）
class Radio : public QWidget {
  public:
    explicit Radio(const QString& text, QWidget* parent = nullptr);

    void SetChecked(bool on);
    [[nodiscard]] bool IsChecked() const { return checked_; }
    void SetOnToggled(std::function<void(bool)> cb) { on_toggled_ = std::move(cb); }

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;

  private:
    QString text_;
    bool checked_ = false;
    std::function<void(bool)> on_toggled_;
};

// SearchBox —— 防抖 200ms；前缀放大镜；Esc 清空
class SearchBox : public QFrame {
  public:
    explicit SearchBox(QWidget* parent = nullptr);

    void SetPlaceholder(const QString& p);
    void SetOnSearch(std::function<void(const QString&)> cb) { on_search_ = std::move(cb); }

  protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

  private:
    QLineEdit* edit_ = nullptr;
    QTimer debounce_;
    std::function<void(const QString&)> on_search_;
};

} // namespace shine::widgets
