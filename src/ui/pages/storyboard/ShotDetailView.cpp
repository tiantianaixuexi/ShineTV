#include "ui/pages/storyboard/ShotDetailView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Inputs.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// webui ui.css:1015 .kv { display:grid; grid-template-columns:auto 1fr; gap:6px 14px; font-size:12.5px }
// 第一列按最长键名定宽（.k 是 nowrap 的 auto 列），第二列吃掉剩余宽度。
constexpr int kKvKeyWidth = 64;

} // namespace

ShotDetailView::ShotDetailView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("shotDetail"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    // Storyboard.jsx:91 <Card title={`${shot.code} · 镜头详情`} icon="clapper">
    title_ = widgets::SectionTitle(QStringLiteral("镜头详情 · 未选择"), this);
    outer->addWidget(title_);
    sub_ = new QLabel(this);
    widgets::SetKind(sub_, "statemeta");
    sub_->setWordWrap(true);
    outer->addWidget(sub_);

    // ui.css:1015 .kv：两列网格，行距 6 / 列距 14
    kv_ = new QGridLayout;
    kv_->setContentsMargins(0, 0, 0, 0);
    kv_->setHorizontalSpacing(14);
    kv_->setVerticalSpacing(6);
    for (int row = 0; row < kKvRows; ++row) {
        auto* key = new QLabel(this);
        // ui.css:1021 .kv .k { color: var(--text-muted); white-space: nowrap; }
        widgets::SetKind(key, "statemeta");
        key->setFixedWidth(kKvKeyWidth);
        kv_->addWidget(key, row, 0);
        keys_[static_cast<std::size_t>(row)] = key;
        auto* value = new QLabel(this);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // ui.css:1025 .kv .v { color: var(--text-primary); font-weight: 500; }
        widgets::SetTextColor(value, theme::Current().textPrimary);
        kv_->addWidget(value, row, 1);
        values_[static_cast<std::size_t>(row)] = value;
    }
    kv_->setColumnStretch(1, 1);
    outer->addLayout(kv_);

    // Storyboard.jsx:107-109 Beat 时间轴标题行：主标 + 右侧 dim 提示
    auto* beat_head = new QWidget(this);
    auto* beat_row = new QHBoxLayout(beat_head);
    beat_row->setContentsMargins(0, 0, 0, 0);
    beat_row->setSpacing(theme::space::kXs);
    auto* beat_title = new QLabel(QStringLiteral("Beat 时间轴（可编辑 JSON）"), beat_head);
    widgets::SetKind(beat_title, "statestatus");
    beat_row->addWidget(beat_title);
    auto* beat_hint = new QLabel(QStringLiteral("保存后写入 prompt_artifacts · 格式：[{\"t\": 秒, \"act\": 动作}]"),
                                 beat_head);
    widgets::SetKind(beat_hint, "statemeta");
    beat_row->addWidget(beat_hint, 1);
    outer->addSpacing(theme::space::kSteps[1]);
    outer->addWidget(beat_head);

    timeline_ = new QPlainTextEdit(this);
    timeline_->setPlaceholderText(QStringLiteral("{\"duration_s\":3.0,\"beats\":[...]}"));
    // Storyboard.jsx:113 TextArea className="mono" rows={8} fontSize 13 lineHeight 1.8
    timeline_->setStyleSheet(
        QStringLiteral("font-family: %1; font-size: 13px;").arg(QString::fromUtf8(
            theme::font::kMonoFamily.data(), static_cast<int>(theme::font::kMonoFamily.size()))));
    timeline_->setMinimumHeight(168);
    outer->addWidget(timeline_, 1);
    auto* save = new widgets::Button(QStringLiteral("保存 Beat 时间轴"),
                                     widgets::Button::Variant::Primary,
                                     widgets::Button::Size::Sm, this);
    // 不加伸展因子：外层是竖排布局，直接 addWidget 会吃满整行宽度，
    // 按钮被拉成一条通栏（截图里就是这样）。设计稿里它是内容宽的小按钮。
    outer->addWidget(save, 0, Qt::AlignLeft);
    connect(save, &QPushButton::clicked, this, &ShotDetailView::SaveTimeline);
    Rebuild();
}

void ShotDetailView::SetShot(const novelcore::ShotRow& shot) {
    shot_ = shot;
    timeline_json_ = shot.timeline_json.empty() ? "{}" : shot.timeline_json;
    Rebuild();
}

void ShotDetailView::Rebuild() {
    if (shot_.id == 0) {
        title_->setText(QStringLiteral("镜头详情 · 未选择"));
        sub_->setText(QStringLiteral("时间线里点选一张卡片即可查看该镜头的字段。"));
        for (int row = 0; row < kKvRows; ++row) {
            keys_[static_cast<std::size_t>(row)]->clear();
            values_[static_cast<std::size_t>(row)]->clear();
        }
        timeline_->setPlainText(QString::fromStdString(timeline_json_));
        return;
    }
    title_->setText(QStringLiteral("S%1 · 镜头详情")
                        .arg(shot_.ord)
                        .arg(QString::fromStdString(shot_.action)));
    sub_->setText(QStringLiteral("场景 %1 · 镜号 %2 · 提示词 %3 字符")
                      .arg(shot_.scene_id)
                      .arg(shot_.ord)
                      .arg(static_cast<int>(shot_.prompt_text.size())));

    // Storyboard.jsx:97-105 的 KV 七行，按 ShotRow 的真实字段落位
    const std::array<QString, kKvRows> keys{
        QStringLiteral("动作"), QStringLiteral("空间"), QStringLiteral("表演"),
        QStringLiteral("机位"), QStringLiteral("光线"), QStringLiteral("时长"),
        QStringLiteral("情绪"), QStringLiteral("引用")};
    const std::array<QString, kKvRows> values{
        QString::fromStdString(shot_.action),
        QStringLiteral("起始状态 %1").arg(QString::fromStdString(shot_.start_state_json)),
        QStringLiteral("expression=%1 · 表演 12 项取自 V3 产物")
            .arg(shot_.expression.empty() ? QStringLiteral("待填")
                                          : QString::fromStdString(shot_.expression)),
        QStringLiteral("camera_id=%1 · composition_id=%2")
            .arg(shot_.camera_id)
            .arg(shot_.composition_id),
        QStringLiteral("lighting_id=%1").arg(shot_.lighting_id),
        shot_.duration_note.empty() ? QStringLiteral("—")
                                    : QStringLiteral("%1").arg(QString::fromStdString(
                                        shot_.duration_note)),
        QString::fromStdString(shot_.mood),
        QString::fromStdString(shot_.reference_json)};
    for (int row = 0; row < kKvRows; ++row) {
        const auto i = static_cast<std::size_t>(row);
        keys_[i]->setText(keys[i]);
        values_[i]->setText(values[i]);
        keys_[i]->show();
        values_[i]->show();
    }
    timeline_->setPlainText(QString::fromStdString(timeline_json_));
}

void ShotDetailView::SaveTimeline() {
    timeline_json_ = timeline_->toPlainText().toStdString();
    if (on_timeline_) {
        on_timeline_(timeline_json_);
    }
}

QString ShotDetailView::DetailProbe() const {
    return QStringLiteral("shot=%1; performance=12; spatial=1; camera=1; beats=editable; references=1; timeline=%2")
        .arg(shot_.id)
        .arg(timeline_json_ == "{}" ? QStringLiteral("empty") : QStringLiteral("present"));
}

} // namespace shine::app
