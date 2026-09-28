#pragma once
// P05-S6 项目参考图：拖拽/选择导入、标记、绑定当前实体；缩略图在 worker 解码并按 EXIF 摆正。
#include "visual/ReferenceLibrary.h"

#include <QWidget>
#include <QString>


#include <filesystem>
#include <cstdint>
#include <string>

#include <memory>
#include <vector>

class QLabel;
class QLineEdit;

class QDragEnterEvent;
class QDropEvent;

namespace shine::images {
class ThumbGrid;
}
namespace shine::widgets {
class Button;
class TextInput;
}

namespace shine::app {

class RefLibraryView : public QWidget {
  public:
    explicit RefLibraryView(QWidget* parent = nullptr);
    ~RefLibraryView() override;

    bool SetProjectRoot(const std::filesystem::path& root, QString* error = nullptr);
    void SetEntityContext(std::int64_t entity_id, QString entity_name, std::int64_t asset_id);
    void Clear();
    void RefreshReferences();
    std::size_t ImportPaths(const std::vector<std::filesystem::path>& paths);
    [[nodiscard]] bool IsBusy() const noexcept { return busy_; }
    [[nodiscard]] QString RefProbe() const;

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private:
    void BuildUi();
    void Reload();
    void RebuildModel();
    void UpdateSelection();
    void ChooseFiles();
    void SaveMarker();
    void BindSelected();
    void RemoveSelected();
    [[nodiscard]] const visual::ReferenceImage* SelectedImage() const;

    std::unique_ptr<visual::ReferenceLibrary> library_;
    std::filesystem::path project_root_;
    std::int64_t entity_id_ = 0;
    std::int64_t asset_id_ = 0;
    QString entity_name_;
    bool busy_ = false;

    images::ThumbGrid* grid_ = nullptr;
    widgets::TextInput* marker_input_ = nullptr;
    QLabel* count_ = nullptr; // views.css:149 .vw-sub：真实张数
    QLabel* detail_ = nullptr;
    widgets::Button* bind_ = nullptr;
    widgets::Button* remove_ = nullptr;
};

} // namespace shine::app
