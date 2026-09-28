#pragma once
// P05-S7 全局图库：复用 shine::media 三来源扫描/模型/查看器与 kit 虚拟缩略图网格。
#include "media/GalleryTypes.h"
#include "visual/ReferenceChain.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>
#include <utility>

class QDialog;
class QLabel;
class QStringListModel;

class QPoint;
namespace shine::images {
class ImageViewer;
class ThumbGrid;
}
namespace shine::widgets {
class SearchBox;
class Segmented;
class Select;
}

namespace shine::app {

class GalleryWorkspace : public QWidget {
  public:
    explicit GalleryWorkspace(QWidget* parent = nullptr);

    void SelectSource(gallery::SourceKind source);
    bool SelectFirst();
    void OpenViewerForCurrent();
    [[nodiscard]] QWidget* ViewerWidget() const;
    [[nodiscard]] bool HasDecodedVisible() const;
    void SetLocalDirectory(const std::filesystem::path& directory);
    void Sync();
    bool SetSelectedAsWorkflowInput();
    [[nodiscard]] QString GalleryProbe() const;
    using UsageResolver =
        std::function<std::vector<visual::ReferenceUsage>(const std::filesystem::path&)>;
    using UsageActivator = std::function<bool(const visual::ReferenceUsage&)>;
    void SetUsageResolver(UsageResolver resolver) { usage_resolver_ = std::move(resolver); }
    void SetUsageActivator(UsageActivator activator) { usage_activator_ = std::move(activator); }
    [[nodiscard]] QStringList UsageLabelsForSelected() const;
    bool ActivateUsage(int index);

  private:
    void BuildUi();
    void ChooseDirectory();
    void ShowViewer();
    void ApplySearch(const QString& text);
    void ApplySort(const QString& label);
    void ShowContextMenu(const QPoint& point);
    [[nodiscard]] gallery::ImageId SelectedId() const;

    images::ThumbGrid* grid_ = nullptr;
    widgets::SearchBox* search_ = nullptr;
    widgets::Select* sort_ = nullptr;
    widgets::Segmented* sources_ = nullptr;
    QLabel* status_ = nullptr;
    QStringListModel* titles_ = nullptr;
    std::uint64_t generation_ = 0;
    bool last_scanning_ = true;
    QDialog* viewer_dialog_ = nullptr;
    images::ImageViewer* viewer_ = nullptr;
    UsageResolver usage_resolver_;
    UsageActivator usage_activator_;
};

} // namespace shine::app
