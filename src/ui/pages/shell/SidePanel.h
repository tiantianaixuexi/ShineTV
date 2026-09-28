#pragma once
// shine::app —— 侧栏（P03-S7）：QStackedWidget 六页，随活动栏切换。
// P03 只搭舞台：每页是「工作区占位卡」，内容由 P04–P08 各自填充。
#include <QFrame>
#include <QStringList>

class QLabel;
class QStackedWidget;

namespace shine::app {

class SidePanel : public QFrame {
  public:
    explicit SidePanel(QWidget* parent = nullptr);

    void SetWorkspace(int index); // 0..5，与 ActivityRail 同步
    [[nodiscard]] int Workspace() const { return current_; }

    // 六个工作区名（总控/小说/视觉资产/分镜/出图/出片）
    [[nodiscard]] static const QStringList& WorkspaceNames();

  private:
    QLabel* title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    int current_ = 0;
};

} // namespace shine::app
