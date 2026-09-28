#pragma once
// shine::app —— 状态栏（P03-S8）：Comfy 状态 / LLM 状态 / 队列 N / 阶段进度 / 主题名 / 版本。
// 状态变化即时（setter 直刷）；每项可点开详情（回调给 MainWindow 弹 Drawer/菜单）。
#include <QFrame>
#include <QString>

#include <functional>

class QLabel;
class QPushButton;

namespace shine::widgets {
class ProgressBar;
}

namespace shine::app {

enum class StatusItem { Comfy, Llm, Queue, Progress, Theme, Version };

class StatusBar : public QFrame {
  public:
    explicit StatusBar(QWidget* parent = nullptr);

    // tone："ok" / "warn" / "danger" / "busy" / "idle"（映射 status.* token）
    void SetComfy(const QString& text, const char* tone);
    void SetLlm(const QString& text, const char* tone);
    void SetQueue(int n);
    void SetProgress(int current, int total, const QString& text); // total<=0 → 不显示进度条
    void SetThemeName(const QString& name);
    void SetVersion(const QString& version);

    void SetOnPick(std::function<void(StatusItem)> cb) { on_pick_ = std::move(cb); }

    // 自检/截图读回
    [[nodiscard]] QString ComfyText() const;
    [[nodiscard]] QString LlmText() const;
    [[nodiscard]] int QueueCount() const;
    [[nodiscard]] QString ThemeName() const;

  private:
    QPushButton* MakeItem(const QString& text, StatusItem item);

    class Dot;
    Dot* comfy_dot_ = nullptr;
    Dot* llm_dot_ = nullptr;
    QPushButton* comfy_btn_ = nullptr;
    QPushButton* llm_btn_ = nullptr;
    QPushButton* queue_btn_ = nullptr;
    QPushButton* progress_btn_ = nullptr;
    shine::widgets::ProgressBar* progress_ = nullptr;
    QPushButton* theme_btn_ = nullptr;
    QPushButton* version_btn_ = nullptr;
    std::function<void(StatusItem)> on_pick_;
};

} // namespace shine::app
