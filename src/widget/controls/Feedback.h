#pragma once
// shine::widgets —— 反馈/状态容器（P02-S5，UI.md §2.1）：ProgressBar / EmptyState / ErrorState
// （容器三态里的 Empty / Error 两态就从这里出）。
#include "widget/controls/Controls.h"
#include "widget/controls/WidgetCommon.h"

#include <functional>

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace shine::widgets {

// ProgressBar —— determinate / indeterminate；可内嵌文字（如 "3/12 章"）；数字不闪烁
class ProgressBar : public QProgressBar {
  public:
    explicit ProgressBar(QWidget* parent = nullptr);

    void SetIndeterminate(bool on);
    [[nodiscard]] bool IsIndeterminate() const { return indeterminate_; }
    void SetInlineText(const QString& t); // 内嵌文字（替代默认百分比）
    void SetState(const char* state);     // "" / "ok" / "error"

  private:
    bool indeterminate_ = false;
};

// EmptyState —— 图标 + 主文案 + 副文案 + 主行动按钮（必须给出下一步）
class EmptyState : public QFrame {
  public:
    EmptyState(const QString& icon, const QString& title, const QString& subtitle,
               const QString& actionText, QWidget* parent = nullptr);

    void SetOnAction(std::function<void()> cb) { on_action_ = std::move(cb); }

  private:
    std::function<void()> on_action_;
};

// ErrorState —— 说明 + 「重试」+「查看详情」（折叠 Error.detail）
class ErrorState : public QFrame {
  public:
    ErrorState(const QString& title, const QString& detail, QWidget* parent = nullptr);

    void SetOnRetry(std::function<void()> cb) { on_retry_ = std::move(cb); }

  private:
    std::function<void()> on_retry_;
};

} // namespace shine::widgets
