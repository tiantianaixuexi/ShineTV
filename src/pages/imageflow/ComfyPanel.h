#pragma once
// P07-S1：ComfyUI 连接、健康、忙碌、队列与控制面板。
#include <QWidget>
#include <QString>

class QLabel;

namespace shine::app {

class ComfyPanel : public QWidget {
  public:
    explicit ComfyPanel(QWidget* parent = nullptr);
    void SetBaseUrl(const QString& url);
    void Refresh();
    [[nodiscard]] QString Probe() const;
    void SetDemoBusy(bool busy);

  private:
    void Update();

    QLabel* state_ = nullptr;
    QLabel* detail_ = nullptr;
    QLabel* queue_ = nullptr;
    QString base_url_;
    bool demo_busy_ = false;
};

} // namespace shine::app
