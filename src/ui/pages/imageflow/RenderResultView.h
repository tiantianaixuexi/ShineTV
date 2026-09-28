#pragma once
// P07-S14：出图「结果」页签（webui/src/views/ImageFlow.jsx 的 ResultPanel）。
//
// 回答一个问题：当前镜头**出了什么、能不能接着用**。
//   成图缩略（真实文件在则显示，否则显式空态，不画假图）
//   → 关键信息（所属章节 / 场景 / 出图状态 / Prompt 版本）
//   → 关联产物（视频就绪与否、重跑入口）
//
// 图片解码按 ui-kit 纪律放在 media 层并由 worker 驱动；本视图只接收
// 已解码好的 QImage，不在 UI 线程同步读盘（见 AGENTS.md 线程纪律）。
#include <QImage>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

namespace shine::widgets {
class Button;
}

namespace shine::app {

class RenderResultView : public QWidget {
  public:
    explicit RenderResultView(QWidget* parent = nullptr);

    // 一条镜头结果。image 为空 = 尚未出图（显示空态，不伪造占位图）。
    struct Entry {
        QString code;      // 镜号，如 S05
        QString chapter;   // 所属章节
        QString scene;     // 场景
        QString status;    // 出图状态文案
        QString prompt;    // Prompt 版本
        QString imagePath; // 成图路径（空 = 未出图）
        QString videoPath; // 视频路径（空 = 未出片）
        QImage image;      // 已解码的成图（可为空）
    };

    void SetEntry(const Entry& entry);
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();

    Entry entry_;
    QLabel* thumb_ = nullptr;
    QLabel* facts_ = nullptr;
    QLabel* assets_ = nullptr;
    shine::widgets::Button* rerun_ = nullptr;
};

} // namespace shine::app
