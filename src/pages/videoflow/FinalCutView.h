#pragma once
#include <QWidget>

#include <filesystem>
#include <vector>

class QLabel;
class QListWidget;

namespace shine::app {

class FinalCutView : public QWidget {
  public:
    explicit FinalCutView(QWidget* parent = nullptr);
    void SetVideos(std::vector<std::pair<std::int64_t, QString>> videos);
    bool ExportScene(const std::filesystem::path& output_dir) const;
    [[nodiscard]] QString Probe() const;

  private:
    std::vector<std::pair<std::int64_t, QString>> videos_;
    QListWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
