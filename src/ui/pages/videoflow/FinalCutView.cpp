#include "ui/pages/videoflow/FinalCutView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "util/File.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include "ui/layout/QtLayout.h"

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=finalView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:290–320 与 VideoFlow.jsx CutPanel：
//   .drow    p8 2 + 底部发丝线 + hover fill.hover
//   .kv .k   text-muted 11px；.kv .v text-primary 12.5px(→12)
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#finalView *[shineKind=\"drow\"]:hover { background-color: %1; }\n"
               "QWidget#finalView QLabel[rowRole=\"code\"] { color: %2; font-size: 11px; font-weight: 700; }\n"
               "QWidget#finalView QLabel[rowRole=\"dim\"] { color: %3; font-size: 11px; }\n"
               "QWidget#finalView QLabel[kvRole=\"k\"] { color: %3; font-size: 11px; }\n"
               "QWidget#finalView QLabel[kvRole=\"v\"] { color: %4; font-size: 12px; }\n")
        .arg(shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.accentPrimary),
             shine::widget::CssRgb(t.textMuted), shine::widget::CssRgb(t.textPrimary));
}


} // namespace

// 一行 KV（webui .kv：左键名 muted、右值 primary）；value 标签单独回传，便于运行期刷新
QWidget* MakeKvRow(const QString& key, QWidget* parent, QLabel** value_out) {
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(theme::space::kSteps[2]);
    auto* k = new QLabel(key, row);
    k->setProperty("kvRole", QStringLiteral("k"));
    k->setFixedWidth(48);
    auto* v = new widgets::ElidedLabel(QStringLiteral("—"), row);
    v->setProperty("kvRole", QStringLiteral("v"));
    v->SetExpandable(false);
    lay->addWidget(k, 0);
    lay->addWidget(v, 1);
    if (value_out != nullptr) *value_out = v;
    return row;
}

FinalCutView::FinalCutView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("finalView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[3]);
    // webui CutPanel：按钮行在最上（系统播放器连播 / 导出一场）
    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(theme::space::kSteps[1]);
    auto* play = new widgets::Button(QStringLiteral("系统播放器连播"), widgets::Button::Variant::Primary,
                                     widgets::Button::Size::Sm, this);
    auto* export_button = new widgets::Button(QStringLiteral("导出一场"), widgets::Button::Variant::Secondary,
                                              widgets::Button::Size::Sm, this);
    actions->addWidget(play);
    actions->addWidget(export_button);
    actions->addStretch(1);
    layout->addLayout(actions);

    // 概览卡（webui CutPanel 的 .card + .kv）
    auto* card = new widgets::Card(widgets::Card::Variant::Outlined, this);
    auto* kv = card->BodyLayout();
    kv->setContentsMargins(12, 12, 12, 12);
    kv->setSpacing(4);
    kv->addWidget(MakeKvRow(QStringLiteral("可连播"), card, &kv_playable_));
    kv->addWidget(MakeKvRow(QStringLiteral("编码"), card, &kv_codec_));
    kv->addWidget(MakeKvRow(QStringLiteral("缺失"), card, &kv_missing_));
    layout->addWidget(card);

    // 镜头视频列表（密集行，不用 QListWidget 的整行选中）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_ = new QWidget(scroll);
    list_lay_ = new QVBoxLayout(list_);
    list_lay_->setContentsMargins(0, 0, 0, 0);
    list_lay_->setSpacing(0);
    list_lay_->addStretch(1);
    scroll->setWidget(list_);
    layout->addWidget(scroll, 1);

    status_ = new QLabel(QStringLiteral("还没有镜头视频"), this);
    widgets::SetKind(status_, "statemeta");
    layout->addWidget(status_);
    connect(play, &QPushButton::clicked, this, [this] { status_->setText(QStringLiteral("已交给系统播放器播放列表")); });
    connect(export_button, &QPushButton::clicked, this, [this] {
        status_->setText(ExportScene("output/videos") ? QStringLiteral("已导出到 output/videos")
                                                        : QStringLiteral("导出失败"));
    });
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
}

void FinalCutView::SetVideos(std::vector<std::pair<std::int64_t, QString>> videos) {
    videos_ = std::move(videos);
    util::ClearLayout(list_lay_, 1); // 末尾常驻 addStretch(1)，保留
    for (std::size_t i = 0; i < videos_.size(); ++i) {
        const auto& [id, path] = videos_[i];
        auto* row = new QWidget(list_);
        widgets::SetKind(row, "drow");
        if (i + 1 == videos_.size()) row->setProperty("shineKind", QString{});
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        lay->setSpacing(theme::space::kSteps[2]);
        auto* code = new QLabel(QStringLiteral("S%1").arg(id), row);
        code->setProperty("rowRole", QStringLiteral("code"));
        code->setFixedWidth(40);
        auto* path_label = new widgets::ElidedLabel(path, row);
        path_label->setProperty("rowRole", QStringLiteral("dim"));
        path_label->SetExpandable(false);
        auto* tag = new widgets::Tag(QStringLiteral("已出片"), "ok", false, row);
        lay->addWidget(code, 0);
        lay->addWidget(path_label, 1);
        lay->addWidget(tag, 0);
        list_lay_->insertWidget(list_lay_->count() - 1, row);
    }
    // KV 三行只写真实数据：数量来自 videos_，编码 / 缺失无来源就留「—」，不猜
    if (kv_playable_ != nullptr) {
        kv_playable_->setText(videos_.empty() ? QStringLiteral("—")
                                              : QStringLiteral("%1 个镜头").arg(videos_.size()));
    }
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
