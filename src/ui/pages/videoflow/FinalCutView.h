#pragma once
#include <QWidget>

#include <filesystem>
#include <vector>

class QLabel;
class QVBoxLayout;

namespace shine::app {

class FinalCutView : public QWidget {
  public:
    explicit FinalCutView(QWidget* parent = nullptr);
    void SetVideos(std::vector<std::pair<std::int64_t, QString>> videos);
    bool ExportScene(const std::filesystem::path& output_dir) const;
    [[nodiscard]] QString Probe() const;

  private:
    std::vector<std::pair<std::int64_t, QString>> videos_;
    // 镜头视频密集行列表（webui .dlist）
    QWidget* list_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
    // 概览卡 KV（webui CutPanel 的可连播 / 编码 / 缺失）
    QLabel* kv_playable_ = nullptr;
    QLabel* kv_codec_ = nullptr;
    QLabel* kv_missing_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
