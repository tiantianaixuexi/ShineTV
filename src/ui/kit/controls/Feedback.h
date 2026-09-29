#pragma once
// shine::widgets —— 反馈/状态容器（P02-S5，UI.md §2.1）：ProgressBar / EmptyState / ErrorState
// （容器三态里的 Empty / Error 两态就从这里出）。
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <functional>

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QString>
#include <QTimer>
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

    // .prog.thin（webui ui.css:452）：4px 细条。几何走 QSS 的 shineSize="thin"。
    void SetThin(bool on);
    [[nodiscard]] bool IsThin() const { return thin_; }

    // .prog.run（webui ui.css:444-451）：chunk 上叠一层循环微光。
    // QSS 没有 keyframes，这条只能自绘 —— 用 QTimer 推进相位 + paintEvent 叠加
    // 一条斜向渐变高光（CSS 是 background-size 200% + shimmer 1.4s linear）。
    void SetShimmer(bool on);
    [[nodiscard]] bool IsShimmering() const { return timer_.isActive(); }

  protected:
    // 覆写以叠加微光：先让基类按 QSS 画完（胶囊底 + chunk），再补高光。
    // 不覆写 sizeHint / geometry —— QSS 已把高度钉死。
    void paintEvent(QPaintEvent* ev) override;

  private:
    // chunk 实际像素宽（= 高光作用范围）。indeterminate 时返回整条宽度。
    [[nodiscard]] int ChunkWidthPx() const;

    bool indeterminate_ = false;
    bool thin_ = false;
    QTimer timer_;
    double shimmer_phase_ = 0.0; // 0→1 一个 shimmer 周期
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
