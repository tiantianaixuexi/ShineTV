#include "ui/verify/gallery/GalleryWorkspace.h"

#include "core/Async.h"
#include "core/Settings.h"
#include "ui/kit/images/Grid.h"
#include "ui/kit/images/Viewer.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "media/FileActions.h"
#include "media/Gallery.h"
#include "media/GalleryModel.h"
#include "util/Encoding.h"
#include "media/ImageScanner.h"
#include "util/File.h"

#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMenu>
#include <QPointer>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace shine::app {
namespace {

[[nodiscard]] gallery::ImageInfo InfoAtRow(int row) {
    const auto& view = gallery::Model().View();
    if (row < 0 || row >= static_cast<int>(view.size())) {
        return {};
    }
    return view[static_cast<std::size_t>(row)];
}

} // namespace

GalleryWorkspace::GalleryWorkspace(QWidget* parent) : QWidget(parent) {
    BuildUi();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &GalleryWorkspace::Sync);
    timer->start(100);
    switch (gallery::State().source) {
        case gallery::SourceKind::Local:
            sources_->SetCurrent(0);
            break;
        case gallery::SourceKind::ComfyOutput:
            sources_->SetCurrent(1);
            break;
        case gallery::SourceKind::ComfyInput:
            sources_->SetCurrent(2);
            break;
    }
    Sync();
}

void GalleryWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[1]);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    auto* title = widgets::SectionTitle(QStringLiteral("全局图库"), header);
    header_layout->addWidget(title);
    sources_ = new widgets::Segmented(
        {QStringLiteral("本地"), QStringLiteral("Comfy 输出"), QStringLiteral("Comfy 输入")}, header);
    sources_->SetOnChanged([this](int index) {
        SelectSource(index == 0   ? gallery::SourceKind::Local
                     : index == 1 ? gallery::SourceKind::ComfyOutput
                                  : gallery::SourceKind::ComfyInput);
    });
    header_layout->addWidget(sources_, 1);
    auto* add_source = new widgets::Button(QStringLiteral("添加来源"),
                                           widgets::Button::Variant::Secondary,
                                           widgets::Button::Size::Sm, header);
    auto* refresh = new widgets::Button(QStringLiteral("重新扫描"),
                                        widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Sm, header);
    header_layout->addWidget(add_source);
    header_layout->addWidget(refresh);
    outer->addWidget(header);

    auto* filters = new QWidget(this);
    auto* filters_layout = new QHBoxLayout(filters);
    filters_layout->setContentsMargins(0, 0, 0, 0);
    search_ = new widgets::SearchBox(filters);
    search_->SetPlaceholder(QStringLiteral("搜索文件名…"));
    filters_layout->addWidget(search_, 1);
    sort_ = new widgets::Select(false, false, filters);
    sort_->SetItems({{QStringLiteral("时间：新→旧")},
                     {QStringLiteral("名称：A→Z")},
                     {QStringLiteral("尺寸：大→小")}});
    filters_layout->addWidget(sort_);
    outer->addWidget(filters);

    status_ = new QLabel(QStringLiteral("图库正在初始化…"), this);
    status_->setWordWrap(true);
    widgets::SetKind(status_, "statedetail");
    outer->addWidget(status_);

    grid_ = new images::ThumbGrid(this);
    grid_->SetTitleRole(true);
    grid_->SetCellSize(170, 196);
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_->SetThumbProvider([](int row, std::function<void(int, QImage)> done) {
        const gallery::ImageInfo info = InfoAtRow(row);
        if (info.id == 0) {
            return;
        }
        async::RunOnWorker([info, row, done = std::move(done)] {
            QImageReader reader(QString::fromStdString(util::PathToUtf8(info.path)));
            reader.setAutoTransform(true);
            QImage image = reader.read();
            image = image.scaled(156, 132, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            async::PostToUi([row, image = std::move(image), done = std::move(done)]() mutable {
                done(row, std::move(image));
            });
        });
    });
    titles_ = new QStringListModel({}, grid_);
    grid_->setModel(titles_);
    outer->addWidget(grid_, 1);

    auto* hint = new QLabel(
        QStringLiteral("双击放大；右键可复制路径、定位文件、设为工作流输入。缩略图按可视区虚拟加载。"),
        this);
    hint->setWordWrap(true);
    widgets::SetKind(hint, "statedetail");
    outer->addWidget(hint);

    connect(add_source, &widgets::Button::clicked, this, &GalleryWorkspace::ChooseDirectory);
    connect(refresh, &widgets::Button::clicked, this,
            [this] { SelectSource(gallery::State().source); });
    search_->SetOnSearch([this](const QString& text) { ApplySearch(text); });
    sort_->SetOnChanged([this] {
        const auto values = sort_->Checked();
        ApplySort(values.empty() ? QStringLiteral("时间：新→旧") : values.front());
    });
    connect(grid_, &QAbstractItemView::doubleClicked, this, [this] { ShowViewer(); });
    connect(grid_, &QWidget::customContextMenuRequested, this, &GalleryWorkspace::ShowContextMenu);
}

void GalleryWorkspace::SelectSource(gallery::SourceKind source) {
    const bool started = gallery::RequestScan(source);
    if (!started) {
        status_->setText(QString::fromStdString(gallery::State().error));
        widgets::Toast::Show(QString::fromStdString(gallery::State().error),
                             widgets::Toast::Tone::Warning);
    }
    Sync();
}

void GalleryWorkspace::SetLocalDirectory(const std::filesystem::path& directory) {
    Settings().galleryLocalDir = util::PathToUtf8(directory);
    SaveSettings();
    SelectSource(gallery::SourceKind::Local);
}

void GalleryWorkspace::Sync() {
    const std::uint64_t generation = gallery::ItemsGeneration();
    gallery::GalleryState& state = gallery::State();
    if (generation == generation_ && state.scanning == last_scanning_) {
        return;
    }
    generation_ = generation;
    last_scanning_ = state.scanning;

    QStringList titles;
    const auto& view = gallery::Model().View();
    titles.reserve(static_cast<qsizetype>(view.size()));
    for (const gallery::ImageInfo& info : view) {
        titles.push_back(QString::fromStdString(util::FileNameToUtf8(info.path.filename())));
    }
    titles_->setStringList(titles);
    grid_->PrefetchVisible();
    status_->setText(QString::fromStdString(state.message.empty()
                                                ? std::string{"图库为空；添加来源后重新扫描。"}
                                                : state.message));
}

void GalleryWorkspace::ApplySearch(const QString& text) {
    gallery::GalleryFilter filter;
    filter.nameContains = text.toStdString();
    gallery::Model().SetFilter(std::move(filter));
    generation_ = 0;
    last_scanning_ = !gallery::State().scanning;
    Sync();
}

void GalleryWorkspace::ApplySort(const QString& label) {
    if (label.startsWith(QStringLiteral("名称"))) {
        gallery::Model().SortBy(gallery::SortKey::Name, true);
    } else if (label.startsWith(QStringLiteral("尺寸"))) {
        gallery::Model().SortBy(gallery::SortKey::Size, false);
    } else {
        gallery::Model().SortBy(gallery::SortKey::Modified, false);
    }
    generation_ = 0;
    last_scanning_ = !gallery::State().scanning;
    Sync();
}

bool GalleryWorkspace::SelectFirst() {
    Sync();
    if (grid_ == nullptr || titles_ == nullptr || titles_->rowCount() == 0) {
        return false;
    }
    grid_->setCurrentIndex(titles_->index(0, 0));
    grid_->scrollTo(grid_->currentIndex(), QAbstractItemView::EnsureVisible);
    return true;
}

bool GalleryWorkspace::HasDecodedVisible() const {
    return grid_ != nullptr && grid_->HasThumb(0);
}

void GalleryWorkspace::OpenViewerForCurrent() {
    ShowViewer();
}

QWidget* GalleryWorkspace::ViewerWidget() const {
    return viewer_;
}


gallery::ImageId GalleryWorkspace::SelectedId() const {
    if (grid_ == nullptr || !grid_->currentIndex().isValid()) {
        return 0;
    }
    return InfoAtRow(grid_->currentIndex().row()).id;
}

QStringList GalleryWorkspace::UsageLabelsForSelected() const {
    QStringList labels;
    if (grid_ == nullptr || !grid_->currentIndex().isValid() || !usage_resolver_) {
        return labels;
    }
    const gallery::ImageInfo info = InfoAtRow(grid_->currentIndex().row());
    if (info.id == 0) {
        return labels;
    }
    for (const auto& usage : usage_resolver_(info.path)) {
        labels.push_back(QString::fromStdString(usage.label));
    }
    return labels;
}

bool GalleryWorkspace::ActivateUsage(int index) {
    if (grid_ == nullptr || !grid_->currentIndex().isValid() || !usage_resolver_ ||
        !usage_activator_) {
        return false;
    }
    const gallery::ImageInfo info = InfoAtRow(grid_->currentIndex().row());
    if (info.id == 0) {
        return false;
    }
    const std::vector<visual::ReferenceUsage> usages = usage_resolver_(info.path);
    if (index < 0 || index >= static_cast<int>(usages.size())) {
        return false;
    }
    return usage_activator_(usages[static_cast<std::size_t>(index)]);
}

void GalleryWorkspace::ShowContextMenu(const QPoint& point) {
    const QModelIndex index = grid_->indexAt(point);
    if (!index.isValid()) {
        return;
    }
    grid_->setCurrentIndex(index);
    const gallery::ImageInfo info = InfoAtRow(index.row());
    if (info.id == 0) {
        return;
    }
    QMenu menu(this);
    QAction* open = menu.addAction(QStringLiteral("放大看"));
    QAction* copy_path = menu.addAction(QStringLiteral("复制路径"));
    QAction* copy_name = menu.addAction(QStringLiteral("复制文件名"));
    QAction* reveal = menu.addAction(QStringLiteral("在资源管理器中显示"));
    menu.addSeparator();
    QAction* workflow = menu.addAction(QStringLiteral("设为工作流输入"));
    QMenu* references = menu.addMenu(QStringLiteral("被谁引用"));
    const std::vector<visual::ReferenceUsage> usages =
        usage_resolver_ ? usage_resolver_(info.path) : std::vector<visual::ReferenceUsage>{};
    for (const visual::ReferenceUsage& usage : usages) {
        QAction* action = references->addAction(QString::fromStdString(usage.label));
        connect(action, &QAction::triggered, this, [this, usage] {
            if (!usage_activator_ || !usage_activator_(usage)) {
                widgets::Toast::Show(QStringLiteral("引用目标已变化，无法跳转。"),
                                     widgets::Toast::Tone::Warning);
            }
        });
    }
    references->setEnabled(!usages.empty() && static_cast<bool>(usage_activator_));
    QAction* chosen = menu.exec(grid_->viewport()->mapToGlobal(point));
    if (chosen == open) {
        ShowViewer();
    } else if (chosen == copy_path) {
        gallery::file_actions::CopyPaths({info.path});
    } else if (chosen == copy_name) {
        gallery::file_actions::CopyNames({info.path});
    } else if (chosen == reveal) {
        std::string error;
        if (!gallery::file_actions::RevealInExplorer(info.path, &error)) {
            widgets::Toast::Show(QString::fromStdString(error), widgets::Toast::Tone::Error);
        }
    } else if (chosen == workflow) {
        (void)SetSelectedAsWorkflowInput();
    }
}

bool GalleryWorkspace::SetSelectedAsWorkflowInput() {
    const gallery::ImageInfo info = InfoAtRow(grid_->currentIndex().row());
    if (info.id == 0) {
        return false;
    }
    gallery::SetLastGraphDropPath(util::PathToUtf8(info.path));
    gallery::UploadToComfyInput(info.path);
    widgets::Toast::Show(QStringLiteral("已设为工作流输入：%1")
                             .arg(QString::fromStdString(util::FileNameToUtf8(info.path.filename()))),
                         widgets::Toast::Tone::Success);
    return true;
}

void GalleryWorkspace::ChooseDirectory() {
    const QString selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("添加本地图库来源"),
        QString::fromStdString(Settings().galleryLocalDir));
    if (!selected.isEmpty()) {
        SetLocalDirectory(util::PathFromUtf8(selected.toStdString()));
    }
}

void GalleryWorkspace::ShowViewer() {
    const int row = grid_->currentIndex().isValid() ? grid_->currentIndex().row() : 0;
    const gallery::ImageInfo selected = InfoAtRow(row);
    if (selected.id == 0) {
        return;
    }
    if (viewer_dialog_ == nullptr) {
        viewer_dialog_ = new QDialog(this);
        viewer_dialog_->setWindowTitle(QStringLiteral("图库查看器"));
        viewer_dialog_->resize(1100, 800);
        auto* layout = new QVBoxLayout(viewer_dialog_);
        viewer_ = new images::ImageViewer(viewer_dialog_);
        layout->addWidget(viewer_);
    }
    std::vector<std::filesystem::path> paths;
    const auto& view = gallery::Model().View();
    const int begin = std::max(0, row - 1);
    const int end = std::min(static_cast<int>(view.size()), row + 2);
    for (int i = begin; i < end; ++i) {
        paths.push_back(view[static_cast<std::size_t>(i)].path);
    }
    QPointer<GalleryWorkspace> guard(this);
    async::RunOnWorker([guard, paths, selectedRow = row - begin] {
        std::vector<QImage> images;
        images.reserve(paths.size());
        for (const auto& path : paths) {
            QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
            reader.setAutoTransform(true);
            images.push_back(reader.read());
        }
        async::PostToUi([guard, images = std::move(images), selectedRow] {
            if (!guard || !guard->viewer_) {
                return;
            }
            guard->viewer_->SetGallery(std::move(images), selectedRow);
            guard->viewer_->ZoomFit();
            guard->viewer_dialog_->show();
            guard->viewer_dialog_->raise();
            guard->viewer_dialog_->activateWindow();
            QTimer::singleShot(0, guard->viewer_, [guard] {
                if (guard && guard->viewer_) {
                    guard->viewer_->ZoomFit();
                }
            });
        });
    });
}

QString GalleryWorkspace::GalleryProbe() const {
    gallery::GalleryState& state = gallery::State();
    return QStringLiteral("source=%1; items=%2; scanning=%3; sources=3; virtual=1; viewerMin=%4; viewerMax=%5; workflow=%6")
        .arg(QString::fromStdString(gallery::SourceLabel(state.source)))
        .arg(static_cast<qulonglong>(gallery::Model().Count()))
        .arg(state.scanning ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(images::ImageViewer::kMinZoom, 0, 'f', 1)
        .arg(images::ImageViewer::kMaxZoom, 0, 'f', 1)
        .arg(QString::fromStdString(gallery::LastGraphDropPath()));
}

} // namespace shine::app
