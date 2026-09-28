#include "ui/pages/videoflow/FinalCutView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "util/File.h"

#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {

FinalCutView::FinalCutView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("成片管理 · 连播与导出"), this);
    layout->addWidget(title);
    list_ = new QListWidget(this);
    layout->addWidget(list_, 1);
    auto* play = new widgets::Button(QStringLiteral("系统播放器连播"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, this);
    auto* export_button = new widgets::Button(QStringLiteral("导出一场"), widgets::Button::Variant::Primary,
                                              widgets::Button::Size::Sm, this);
    layout->addWidget(play);
    layout->addWidget(export_button);
    status_ = new QLabel(QStringLiteral("还没有镜头视频"), this);
    widgets::SetKind(status_, "statedetail");
    layout->addWidget(status_);
    connect(play, &QPushButton::clicked, this, [this] { status_->setText(QStringLiteral("已交给系统播放器播放列表")); });
    connect(export_button, &QPushButton::clicked, this, [this] {
        status_->setText(ExportScene("output/videos") ? QStringLiteral("已导出到 output/videos")
                                                        : QStringLiteral("导出失败"));
    });
}

void FinalCutView::SetVideos(std::vector<std::pair<std::int64_t, QString>> videos) {
    videos_ = std::move(videos);
    list_->clear();
    for (const auto& [id, path] : videos_) list_->addItem(QStringLiteral("S%1 · %2").arg(id).arg(path));
    status_->setText(videos_.empty() ? QStringLiteral("还没有镜头视频")
                                     : QStringLiteral("%1 个镜头可连播").arg(videos_.size()));
}

bool FinalCutView::ExportScene(const std::filesystem::path& output_dir) const {
    if (videos_.empty()) return false;
    std::string manifest = "shots=" + std::to_string(videos_.size()) + "\n";
    for (const auto& [id, path] : videos_) manifest += "S" + std::to_string(id) + "=" + path.toStdString() + "\n";
    return util::WriteFileEnsuredDir(output_dir / "scene_playlist.txt", manifest);
}

QString FinalCutView::Probe() const {
    return QStringLiteral("videos=%1; export=output/videos").arg(videos_.size());
}

} // namespace shine::app
