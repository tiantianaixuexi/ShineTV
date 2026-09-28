#include "ui/pages/assets/RefLibraryView.h"

#include "core/Async.h"
#include "ui/kit/images/Grid.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "util/Encoding.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QStringListModel>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <system_error>
#include <utility>

namespace shine::app {
namespace {

[[nodiscard]] QString JoinMarkers(const std::vector<std::string>& markers) {
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(markers.size()));
    for (const std::string& marker : markers) {
        parts.push_back(QString::fromStdString(marker));
    }
    return parts.join(QStringLiteral("、"));
}

} // namespace

RefLibraryView::RefLibraryView(QWidget* parent) : QWidget(parent) {
    BuildUi();
    Clear();
}

RefLibraryView::~RefLibraryView() = default;

void RefLibraryView::BuildUi() {
    setAcceptDrops(true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[1]);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    auto* title = widgets::SectionTitle(QStringLiteral("项目参考图 · assets/refs/"), header);
    header_layout->addWidget(title, 1);
    auto* choose = new widgets::Button(QStringLiteral("导入图片"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, header);
    auto* refresh = new widgets::Button(QStringLiteral("刷新"), widgets::Button::Variant::Secondary,
                                        widgets::Button::Size::Sm, header);
    header_layout->addWidget(choose);
    header_layout->addWidget(refresh);
    outer->addWidget(header);

    auto* hint = new QLabel(QStringLiteral("拖拽图片到此处导入；EXIF 方向会在 worker 解码时校正。"), this);
    hint->setWordWrap(true);
    widgets::SetKind(hint, "statedetail");
    outer->addWidget(hint);

    grid_ = new images::ThumbGrid(this);
    grid_->SetTitleRole(true);
    grid_->SetCellSize(170, 196);
    grid_->SetThumbProvider([this](int row, std::function<void(int, QImage)> done) {
        if (library_ == nullptr || row < 0 || row >= static_cast<int>(library_->Images().size())) {
            return;
        }
        const std::filesystem::path path = library_->Resolve(library_->Images()[row].rel_path);
        async::RunOnWorker([path, row, done = std::move(done)] {
            QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
            reader.setAutoTransform(true);
            QImage image = reader.read();
            image = image.scaled(156, 132, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            async::PostToUi([row, image = std::move(image), done = std::move(done)]() mutable {
                done(row, std::move(image));
            });
        });
    });
    outer->addWidget(grid_, 1);

    auto* editor = new QWidget(this);
    auto* editor_layout = new QHBoxLayout(editor);
    editor_layout->setContentsMargins(0, 0, 0, 0);
    marker_input_ = new widgets::TextInput(editor);
    marker_input_->SetPlaceholder(QStringLiteral("标记，逗号分隔（例如：正脸, 灰风衣）"));
    editor_layout->addWidget(marker_input_, 1);
    auto* save_marker = new widgets::Button(QStringLiteral("保存标记"),
                                            widgets::Button::Variant::Secondary,
                                            widgets::Button::Size::Sm, editor);
    bind_ = new widgets::Button(QStringLiteral("绑定当前实体"), widgets::Button::Variant::Primary,
                                widgets::Button::Size::Sm, editor);
    remove_ = new widgets::Button(QStringLiteral("移除"), widgets::Button::Variant::Danger,
                                  widgets::Button::Size::Sm, editor);
    editor_layout->addWidget(save_marker);
    editor_layout->addWidget(bind_);
    editor_layout->addWidget(remove_);
    outer->addWidget(editor);

    detail_ = new QLabel(QStringLiteral("未选择参考图。"), this);
    detail_->setWordWrap(true);
    widgets::SetKind(detail_, "statedetail");
    outer->addWidget(detail_);

    connect(choose, &widgets::Button::clicked, this, &RefLibraryView::ChooseFiles);
    connect(refresh, &widgets::Button::clicked, this, &RefLibraryView::Reload);
    connect(save_marker, &widgets::Button::clicked, this, &RefLibraryView::SaveMarker);
    connect(bind_, &widgets::Button::clicked, this, &RefLibraryView::BindSelected);
    connect(remove_, &widgets::Button::clicked, this, &RefLibraryView::RemoveSelected);
    connect(grid_, &QAbstractItemView::clicked, this, [this] { UpdateSelection(); });
}

bool RefLibraryView::SetProjectRoot(const std::filesystem::path& root, QString* error) {
    project_root_ = root;
    library_ = std::make_unique<visual::ReferenceLibrary>(root);
    if (auto loaded = library_->Load(); !loaded) {
        if (error != nullptr) {
            *error = QString::fromStdString(loaded.error().message);
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    RebuildModel();
    UpdateSelection();
    return true;
}

void RefLibraryView::SetEntityContext(std::int64_t entity_id, QString entity_name,
                                      std::int64_t asset_id) {
    entity_id_ = entity_id;
    entity_name_ = std::move(entity_name);
    asset_id_ = asset_id;
    if (bind_ != nullptr) {
        bind_->setEnabled(entity_id_ > 0);
        bind_->setText(entity_id_ > 0
                           ? QStringLiteral("绑定 %1").arg(entity_name_)
                           : QStringLiteral("绑定当前实体"));
    }
    UpdateSelection();
}

void RefLibraryView::Clear() {
    if (marker_input_ != nullptr) {
        marker_input_->SetText({});
    }
    if (grid_ != nullptr) {
        grid_->setModel(new QStringListModel({}, grid_));
    }
    if (detail_ != nullptr) {
        detail_->setText(QStringLiteral("未打开项目或还没有参考图。"));
    }
    if (bind_ != nullptr) {
        bind_->setEnabled(false);
    }
}

std::size_t RefLibraryView::ImportPaths(const std::vector<std::filesystem::path>& paths) {

    if (busy_ || project_root_.empty() || paths.empty()) {
        return 0;
    }
    busy_ = true;
    const std::filesystem::path root = project_root_;
    const std::int64_t entity_id = entity_id_;
    const std::int64_t asset_id = asset_id_;
    QPointer<RefLibraryView> guard(this);
    async::RunOnWorker([guard, root, entity_id, asset_id, paths] {
        visual::ReferenceLibrary library(root);
        std::string error;
        if (auto loaded = library.Load(); !loaded) {
            error = loaded.error().message;
        }
        std::size_t imported = 0;
        if (error.empty()) {
            for (const auto& path : paths) {
                if (library.Import(path, entity_id, asset_id)) {
                    ++imported;
                }
            }
        }
        async::PostToUi([guard, imported, error] {
            if (!guard) {
                return;
            }
            guard->busy_ = false;
            guard->Reload();
            if (!error.empty()) {
                widgets::Toast::Show(QString::fromStdString(error), widgets::Toast::Tone::Error);
            } else if (imported > 0) {
                widgets::Toast::Show(QStringLiteral("已导入 %1 张项目参考图。").arg(imported),
                                     widgets::Toast::Tone::Success);
            }
        });
    });
    return paths.size();
}

void RefLibraryView::RefreshReferences() {
    Reload();
}

void RefLibraryView::Reload() {
    if (library_ == nullptr) {
        return;
    }
    if (auto loaded = library_->Load(); !loaded) {
        widgets::Toast::Show(QString::fromStdString(loaded.error().message),
                             widgets::Toast::Tone::Error);
        return;
    }
    RebuildModel();
    UpdateSelection();
}

void RefLibraryView::RebuildModel() {
    if (grid_ == nullptr || library_ == nullptr) {
        return;
    }
    QStringList titles;
    titles.reserve(static_cast<qsizetype>(library_->Images().size()));
    for (const auto& image : library_->Images()) {
        titles.push_back(QString::fromStdString(image.original_name));
    }
    auto* model = new QStringListModel(titles, grid_);
    grid_->setModel(model);
    grid_->PrefetchVisible();
}

const visual::ReferenceImage* RefLibraryView::SelectedImage() const {
    if (library_ == nullptr || grid_ == nullptr || !grid_->currentIndex().isValid()) {
        return nullptr;
    }
    const int row = grid_->currentIndex().row();
    if (row < 0 || row >= static_cast<int>(library_->Images().size())) {
        return nullptr;
    }
    return &library_->Images()[row];
}

void RefLibraryView::UpdateSelection() {
    const visual::ReferenceImage* image = SelectedImage();
    const bool has_selection = image != nullptr;
    if (marker_input_ != nullptr) {
        marker_input_->setEnabled(has_selection);
        if (has_selection) {
            marker_input_->SetText(JoinMarkers(image->markers));
        }
    }
    if (remove_ != nullptr) {
        remove_->setEnabled(has_selection && !busy_);
    }
    if (bind_ != nullptr) {
        bind_->setEnabled(has_selection && entity_id_ > 0 && !busy_);
    }
    if (detail_ == nullptr) {
        return;
    }
    if (!has_selection) {
        detail_->setText(library_ == nullptr || library_->Images().empty()
                             ? QStringLiteral("还没有参考图；拖拽图片或点「导入图片」。")
                             : QStringLiteral("选择一张参考图查看方向、尺寸与绑定。"));
        return;
    }
    detail_->setText(QStringLiteral("%1 · EXIF %2 · 原始 %3×%4 → 显示 %5×%6 · 标记：%7 · 绑定实体 #%8")
                        .arg(QString::fromStdString(image->original_name),
                             QString::fromStdString(image->orientation))
                        .arg(image->raw_width)
                        .arg(image->raw_height)
                        .arg(image->display_width)
                        .arg(image->display_height)
                        .arg(JoinMarkers(image->markers))
                        .arg(image->entity_id));
}

void RefLibraryView::ChooseFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("导入项目参考图"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.avif);;所有文件 (*)"));
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<std::size_t>(files.size()));
    for (const QString& file : files) {
        paths.push_back(util::PathFromUtf8(file.toStdString()));
    }
    (void)ImportPaths(paths);
}

void RefLibraryView::SaveMarker() {
    const visual::ReferenceImage* selected = SelectedImage();
    if (selected == nullptr || library_ == nullptr) {
        return;
    }
    const QString input = marker_input_ == nullptr ? QString{} : marker_input_->Text();
    QString normalized = input;
    normalized.replace(QStringLiteral("，"), QStringLiteral(","));
    const QStringList parts = normalized.split(QLatin1Char(','), Qt::SkipEmptyParts);
    std::vector<std::string> markers;
    markers.reserve(static_cast<std::size_t>(parts.size()));
    for (const QString& part : parts) {
        markers.push_back(part.trimmed().toStdString());
    }
    if (auto saved = library_->SetMarkers(selected->id, std::move(markers)); !saved) {
        widgets::Toast::Show(QString::fromStdString(saved.error().message),
                             widgets::Toast::Tone::Error);
    }
    Reload();
}

void RefLibraryView::BindSelected() {
    const visual::ReferenceImage* selected = SelectedImage();
    if (selected == nullptr || library_ == nullptr || entity_id_ <= 0) {
        return;
    }
    if (auto bound = library_->Bind(selected->id, entity_id_, asset_id_); !bound) {
        widgets::Toast::Show(QString::fromStdString(bound.error().message),
                             widgets::Toast::Tone::Error);
    }
    Reload();
}

void RefLibraryView::RemoveSelected() {
    const visual::ReferenceImage* selected = SelectedImage();
    if (selected == nullptr || library_ == nullptr) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("移除参考图"),
                              QStringLiteral("从项目 assets/refs/ 移除「%1」？")
                                  .arg(QString::fromStdString(selected->original_name))) !=
        QMessageBox::Yes) {
        return;
    }
    if (auto removed = library_->Remove(selected->id); !removed) {
        widgets::Toast::Show(QString::fromStdString(removed.error().message),
                             widgets::Toast::Tone::Error);
    }
    Reload();
}

QString RefLibraryView::RefProbe() const {
    const std::size_t count = library_ == nullptr ? 0 : library_->Images().size();
    QString first = QStringLiteral("none");
    if (count > 0) {
        const auto& image = library_->Images().front();
        first = QStringLiteral("%1|orientation=%2|display=%3x%4|markers=%5|entity=%6")
                    .arg(QString::fromStdString(image.original_name),
                         QString::fromStdString(image.orientation))
                    .arg(image.display_width)
                    .arg(image.display_height)
                    .arg(JoinMarkers(image.markers))
                    .arg(image.entity_id);
    }
    return QStringLiteral("refs=%1; busy=%2; drops=%3; first=%4")
        .arg(static_cast<int>(count))
        .arg(busy_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(testAttribute(Qt::WA_AcceptDrops) ? QStringLiteral("1") : QStringLiteral("0"), first);
}

void RefLibraryView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void RefLibraryView::dropEvent(QDropEvent* event) {
    std::vector<std::filesystem::path> paths;
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        paths.reserve(static_cast<std::size_t>(urls.size()));
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                paths.push_back(util::PathFromUtf8(url.toLocalFile().toStdString()));
            }
        }
    }
    if (!paths.empty() && ImportPaths(paths) > 0) {
        event->acceptProposedAction();
    }
}

} // namespace shine::app
