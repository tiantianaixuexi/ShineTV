#pragma once
// P08-S15：底部连播胶片条（webui/src/views/VideoFlow.jsx 的 float-strip）。
//
// 把「本章有哪些镜头、各自出片到哪一步」摊平成一条横排：
// 每格 = 缩略位 + 镜号 + 就绪态。未出片的格子画显式空态（「待出片」），
// 不复用上一镜的图冒充 —— 成片列表最怕的就是看错镜头。
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QHBoxLayout;
class QScrollArea;

namespace shine::app {

class FilmStrip : public QWidget {
  public:
    explicit FilmStrip(QWidget* parent = nullptr);

    struct Cell {
        QString code;      // 镜号
        QString duration;  // 时长/帧率说明（如 48fps）；空 = 未出片
        bool ready = false;      // 视频是否就绪
        QString videoPath;       // 视频路径（ready 时非空）
        QImage thumb;            // 首帧缩略（可为空 → 显示空态）
    };

    void SetCells(std::vector<Cell> cells);
    [[nodiscard]] int ReadyCount() const;
    [[nodiscard]] QString Probe() const;
    // 点击某格（仅就绪格会回调）。用回调而不是 Qt signal：FilmStrip 不需要
    // moc，也不进 QObject 事件体系，少一层构建依赖。
    void SetOnCellPicked(std::function<void(int)> cb) { on_picked_ = std::move(cb); }

  private:
    void Rebuild();

    std::vector<Cell> cells_;
    // 横向滚动容器（webui .float-strip 的 overflow-x: auto）
    QScrollArea* scroll_ = nullptr;
    QHBoxLayout* row_ = nullptr;
    std::function<void(int)> on_picked_;
};

} // namespace shine::app
