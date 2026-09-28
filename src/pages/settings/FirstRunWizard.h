#pragma once
// shine::app —— 首启引导（P03-S10）：选主题 / Comfy 地址 / LLM Key 三步，只出现一次。
// asSettings=true 时作为「设置」复用（预填当前值，标题为 设置）。
#include <QDialog>
#include <QString>

#include <vector>

class QLabel;
class QStackedWidget;

namespace shine::widgets {
class TextInput;
class Select;
class Button;
}

namespace shine::app {

class FirstRunWizard : public QDialog {
  public:
    explicit FirstRunWizard(QWidget* parent = nullptr, bool asSettings = false);

    // 自检 / 截图辅助
    void GoToStep(int step);
    [[nodiscard]] int CurrentStep() const { return step_; }
    // 完成动作（「完成」按钮与自动化判定共用同路径）：写 Settings + firstRun=false + SaveSettings
    void Finish();

  private:
    void ShowStep(int step);

    bool as_settings_ = false;
    int step_ = 0;
    QStackedWidget* stack_ = nullptr;
    QLabel* theme_pick_ = nullptr;
    shine::widgets::TextInput* comfy_edit_ = nullptr;
    shine::widgets::Select* provider_ = nullptr;
    shine::widgets::TextInput* key_edit_ = nullptr;
    shine::widgets::Button* next_btn_ = nullptr;
    shine::widgets::Button* back_btn_ = nullptr;
    std::vector<shine::widgets::Button*> theme_chips_;
};

} // namespace shine::app
