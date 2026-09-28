#include "ui/pages/assets/AssetDetailView.h"

#include "ui/pages/assets/AssetPolicyPanel.h"
#include "ui/pages/novel/WorldBoardShared.h"
#include "ui/kit/data/Flow.h"
#include "ui/kit/images/Sheet.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "util/Encoding.h"

#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QStringList>


#include <string_view>
#include <array>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

namespace shine::app {
namespace {

struct LayerSpec {
    novelcore::AssetLayer layer;
    std::string_view key;
    std::string_view title;
    std::string_view parent;
};

constexpr std::array<LayerSpec, 4> kLayers{{
    {novelcore::AssetLayer::Front, "front", "正脸", ""},
    {novelcore::AssetLayer::Turnaround, "turnaround", "四视图", "front"},
    {novelcore::AssetLayer::BaseBody, "base_body", "基础身体", "turnaround"},
    {novelcore::AssetLayer::Wardrobe, "wardrobe", "服装", "base_body"},
}};

[[nodiscard]] QString ArtifactState(std::string_view status) {
    if (status == "DONE") return QStringLiteral("已就绪");
    if (status == "RUNNING") return QStringLiteral("生成中");
    if (status == "FAILED") return QStringLiteral("失败");
    return QStringLiteral("缺层");
}

[[nodiscard]] const char* ArtifactTone(std::string_view status) {
    if (status == "DONE") return "ok";
    if (status == "RUNNING") return "accent";
    if (status == "FAILED") return "danger";
    return "warn";
}


} // namespace
std::string AssetDetailView::ArtifactPath(const LayerData& layer) {
    return layer.artifact ? layer.artifact->rel_path : std::string{};
}

AssetDetailView::AssetDetailView(QWidget* parent) : QWidget(parent) {
    BuildUi();
    Clear();
}

void AssetDetailView::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(theme::space::kSteps[1]);

    auto* titles = new QWidget(header);
    auto* title_layout = new QVBoxLayout(titles);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(2);
    title_ = widgets::SectionTitle(QStringLiteral("资产详情 · 未选择"), titles);
    subtitle_ = new QLabel(QStringLiteral("选择资产后显示正脸、四视图、基础身体与服装。"), titles);
    subtitle_->setWordWrap(true);
    widgets::SetKind(subtitle_, "statedetail");
    title_layout->addWidget(title_);
    title_layout->addWidget(subtitle_);
    runtime_label_ = new QLabel(titles);
    runtime_label_->setWordWrap(true);
    widgets::SetKind(runtime_label_, "statedetail");
    runtime_label_->hide();
    title_layout->addWidget(runtime_label_);
    header_layout->addWidget(titles, 1);

    generate_all_ = new widgets::Button(QStringLiteral("生成完整链"),
                                        widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Sm, header);
    generate_all_->setToolTip(QStringLiteral("按正脸 → 四视图 → 基础身体 → 服装推进真实 V0 流水线"));
    generate_all_->setEnabled(false);
    header_layout->addWidget(generate_all_);

    export_ = new widgets::Button(QStringLiteral("导出整版"), widgets::Button::Variant::Secondary,
                                  widgets::Button::Size::Sm, header);
    export_->setToolTip(QStringLiteral("使用 SheetGrid 导出当前可用层与缺层占位"));
    export_->setEnabled(false);
    header_layout->addWidget(export_);
    outer->addWidget(header);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cards_ = new QWidget(scroll);
    cards_layout_ = new QGridLayout(cards_);
    cards_layout_->setContentsMargins(0, 0, 0, 0);
    cards_layout_->setHorizontalSpacing(theme::space::kSteps[2]);
    cards_layout_->setVerticalSpacing(theme::space::kSteps[2]);
    cards_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(cards_);
    outer->addWidget(scroll, 1);

    auto* chain_title = SectionLabel(this, QStringLiteral("派生链"));
    outer->addWidget(chain_title);
    flow_ = new data::StageFlow(this);
    flow_->setMinimumHeight(112);
    flow_->setToolTip(QStringLiteral("正脸 → 四视图 → 基础身体 → 服装；点击节点可查看对应产物"));
    outer->addWidget(flow_);

    connect(export_, &QPushButton::clicked, this, &AssetDetailView::ExportSheet);
    policy_ = new AssetPolicyPanel(this);
    outer->addWidget(policy_);
    connect(generate_all_, &QPushButton::clicked, this, [this] {
        if (on_generate_all_) {
            on_generate_all_();
        }
    });
}

bool AssetDetailView::ShowAsset(novelcore::NovelVisual& visual,
                                const novelcore::VisualAssetRow& asset,
                                const std::filesystem::path& projectDir, QString* error) {
    Clear();
    if (error != nullptr) {
        error->clear();
    }
    if (asset.id <= 0) {
        const QString detail = QStringLiteral("资产 id 无效。");
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }

    auto listed = visual.ListArtifacts(asset.id);
    if (!listed) {
        const QString detail = QStringLiteral("读取资产 #%1 的形象层失败：%2")
                                  .arg(asset.id)
                                  .arg(QString::fromStdString(listed.error().message));
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }

    asset_ = asset;
    projectDir_ = projectDir;
    layers_.clear();
    layers_.reserve(kLayers.size());
    for (const LayerSpec& spec : kLayers) {
        LayerData data;
        data.layer = spec.layer;
        data.key = std::string{spec.key};
        data.title = QString::fromUtf8(spec.title.data(), static_cast<int>(spec.title.size()));
        data.parent_key = std::string{spec.parent};
        for (const novelcore::VisualArtifactRow& artifact : *listed) {
            if (artifact.layer == data.key) {
                data.artifact = artifact;
                break;
            }
        }
        if (!data.artifact && data.key == "front" && !asset.sheet_rel_path.empty()) {
            novelcore::VisualArtifactRow legacy;
            legacy.asset_id = asset.id;
            legacy.layer = data.key;
            legacy.rel_path = asset.sheet_rel_path;
            legacy.status = "DONE";
            legacy.note = "兼容旧工程：visual_assets.sheet_rel_path 作为正脸层";
            data.artifact = std::move(legacy);
            data.legacy_front = true;
        }

        const std::filesystem::path path = ResolvePath(ArtifactPath(data));
        std::error_code ec;
        data.ready = data.artifact && data.artifact->status == "DONE" &&
                     !data.artifact->rel_path.empty() && std::filesystem::is_regular_file(path, ec);
        layers_.push_back(std::move(data));
    }

    Rebuild();
    return true;
}

void AssetDetailView::Clear() {
    asset_ = {};
    projectDir_.clear();
    layers_.clear();
    runtime_phase_.clear();
    runtime_detail_.clear();
    runtime_layer_.clear();
    runtime_history_.clear();
    runtime_active_ = false;
    runtime_degraded_ = false;
    if (title_ != nullptr) {
        title_->setText(QStringLiteral("资产详情 · 未选择"));
    }
    if (subtitle_ != nullptr) {
        subtitle_->setText(QStringLiteral("选择资产后显示正脸、四视图、基础身体与服装。"));
    }
    if (export_ != nullptr) {
        export_->setEnabled(false);
    }
    if (runtime_label_ != nullptr) {
        runtime_label_->clear();
        runtime_label_->hide();
    }
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(false);
    }
    if (cards_layout_ != nullptr) {
        ClearLayout(cards_layout_);
    }
    if (policy_ != nullptr) {
        policy_->SetRuntimeState({}, {}, {}, false, false);
    }
    if (flow_ != nullptr) {
        flow_->SetGraph({}, {});
    }
}

AssetPolicy AssetDetailView::Policy() const {
    return policy_ == nullptr ? AssetPolicy{} : policy_->Policy();
}

void AssetDetailView::SetPolicy(const AssetPolicy& policy) {
    if (policy_ != nullptr) {
        policy_->SetPolicy(policy);
    }
}

QString AssetDetailView::PolicyProbe() const {
    return policy_ == nullptr ? QStringLiteral("policy=unavailable") : policy_->PolicyProbe();
}

void AssetDetailView::SetRuntimeState(QString phase, QString detail, QStringList history,
                                      QString layer, bool active, bool degraded) {
    runtime_phase_ = std::move(phase);
    runtime_detail_ = std::move(detail);
    runtime_history_ = std::move(history);
    runtime_layer_ = std::move(layer);
    runtime_active_ = active;
    runtime_degraded_ = degraded;
    if (runtime_label_ != nullptr) {
        if (runtime_history_.isEmpty()) {
            runtime_label_->clear();
            runtime_label_->hide();
        } else {
            runtime_label_->setText(QStringLiteral("状态机：%1%2%3")
                                        .arg(runtime_history_.join(QLatin1String(" → ")),
                                             runtime_degraded_ ? QStringLiteral(" · ⚠ 降级") : QString{},
                                             runtime_detail_.isEmpty()
                                                 ? QString{}
                                                 : QStringLiteral(" · %1").arg(runtime_detail_)));
            runtime_label_->show();
        }
    }
    if (policy_ != nullptr) {
        policy_->SetRuntimeState(runtime_layer_, runtime_phase_, runtime_detail_, runtime_active_,
                                  runtime_degraded_);
    }
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(asset_.id > 0 && asset_.status != "READY" && !runtime_active_);
        generate_all_->setText(asset_.status == "READY" ? QStringLiteral("已完成")
                                                         : QStringLiteral("生成完整链"));
    }
}

void AssetDetailView::Rebuild() {
    if (cards_layout_ == nullptr || flow_ == nullptr) {
        return;
    }
    ClearLayout(cards_layout_);

    title_->setText(QString::fromStdString(asset_.name));
    subtitle_->setText(QStringLiteral("资产 #%1 · %2 · 状态 %3")
                           .arg(asset_.id)
                           .arg(QString::fromStdString(asset_.kind),
                                QString::fromStdString(asset_.status)));

    std::vector<data::StageFlow::Node> nodes;
    std::vector<data::StageFlow::Link> links;
    int ready_count = 0;
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        LayerData& data = layers_[i];
        const std::string status = data.artifact ? data.artifact->status : std::string{"PENDING"};
        if (data.ready) {
            ++ready_count;
        }

        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, cards_);
        card->setMinimumWidth(230);
        QVBoxLayout* body = card->BodyLayout();

        auto* head = new QWidget(card);
        auto* head_layout = new QHBoxLayout(head);
        head_layout->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(data.title, head);
        widgets::SetSemibold(name, true);
        head_layout->addWidget(name);
        head_layout->addStretch(1);
        head_layout->addWidget(new widgets::Tag(ArtifactState(status), ArtifactTone(status), false, head));
        body->addWidget(head);

        auto* preview = new QLabel(card);
        preview->setFixedHeight(176);
        preview->setAlignment(Qt::AlignCenter);
        preview->setWordWrap(true);
        widgets::SetKind(preview, "field");
        if (data.ready) {
            const std::filesystem::path path = ResolvePath(ArtifactPath(data));
            QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (image.isNull()) {
                preview->setText(QStringLiteral("图像读取失败\n请检查文件或重新导入"));
            } else {
                data.decoded = true;
                preview->setPixmap(QPixmap::fromImage(
                    image.scaled(218, 164, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
        } else if (data.artifact && data.artifact->status == "RUNNING") {
            preview->setText(QStringLiteral("生成中…\n完成后自动刷新"));
        } else if (data.artifact && !data.artifact->rel_path.empty()) {
            preview->setText(QStringLiteral("产物文件缺失\n可重试该层"));
        } else {
            preview->setText(QStringLiteral("缺层\n下一步：生成这一层"));
        }
        body->addWidget(preview);

        QString parent_text = data.parent_key.empty()
                                  ? QStringLiteral("派生起点：文生图")
                                  : QStringLiteral("派生自：%1")
                                        .arg(QString::fromStdString(data.parent_key));
        if (data.artifact) {
            parent_text += QStringLiteral(" · artifact #%1").arg(data.artifact->id);
        }
        if (data.legacy_front) {
            parent_text += QStringLiteral(" · 旧单图兼容");
        }
        auto* parent = new QLabel(parent_text, card);
        parent->setWordWrap(true);
        widgets::SetKind(parent, "statedetail");
        body->addWidget(parent);

        if (!data.ready) {
            const QString action_text = status == "FAILED" ? QStringLiteral("重试")
                                                           : QStringLiteral("生成");
            auto* action = new widgets::Button(action_text, widgets::Button::Variant::Primary,
                                               widgets::Button::Size::Sm, card);
            action->setToolTip(QStringLiteral("为「%1」启动真实形象层任务；依赖与降级按 V0 策略处理")
                                   .arg(data.title));
            connect(action, &QPushButton::clicked, this, [this, layer = data.layer] {
                if (on_generate_) {
                    on_generate_(layer);
                }
            });
            body->addWidget(action);
        } else if (!data.artifact->note.empty()) {
            auto* note = new QLabel(QString::fromStdString(data.artifact->note), card);
            note->setWordWrap(true);
            body->addWidget(note);
        }
        body->addStretch(1);
        cards_layout_->addWidget(card, static_cast<int>(i / 2), static_cast<int>(i % 2));

        data::StageFlow::Node node;
        node.id = QString::fromStdString(data.key);
        node.title = data.title;
        node.state = data.ready ? data::StageFlow::NodeState::Done
                     : data.artifact && data.artifact->status == "RUNNING"
                         ? data::StageFlow::NodeState::Running
                     : data.artifact && data.artifact->status == "FAILED"
                         ? data::StageFlow::NodeState::Failed
                         : data::StageFlow::NodeState::Todo;
        nodes.push_back(std::move(node));
        if (!data.parent_key.empty()) {
            links.push_back({QString::fromStdString(data.parent_key), QString::fromStdString(data.key)});
        }
    }
    flow_->SetGraph(std::move(nodes), std::move(links));
    export_->setEnabled(ready_count > 0);
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(asset_.status != "READY" && !runtime_active_);
        generate_all_->setText(asset_.status == "READY" ? QStringLiteral("已完成")
                                                         : QStringLiteral("生成完整链"));
    }
}

void AssetDetailView::ShowError(const QString& detail) {
    if (cards_layout_ == nullptr) {
        return;
    }
    ClearLayout(cards_layout_);
    auto* error = new widgets::ErrorState(QStringLiteral("读取资产详情失败"), detail, cards_);
    cards_layout_->addWidget(error, 0, 0, 1, 2);
    if (subtitle_ != nullptr) {
        subtitle_->setText(detail);
    }
    if (export_ != nullptr) {
        export_->setEnabled(false);
    }
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(false);
    }
}

void AssetDetailView::ExportSheet() {
    images::SheetGrid sheet(2, QSize(360, 280));
    for (LayerData& data : layers_) {
        if (!data.ready) {
            continue;
        }
        const std::filesystem::path path = ResolvePath(ArtifactPath(data));
        QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            sheet.Add(image, data.title);
        }
    }
    if (sheet.Count() == 0) {
        widgets::Toast::Show(QStringLiteral("没有可导出的形象层；请先生成至少一层。"),
                             widgets::Toast::Tone::Warning);
        return;
    }

    const QString suggested = QString::fromStdString(asset_.name) + QStringLiteral("-角色设定集.png");
    const QString target = QFileDialog::getSaveFileName(this, QStringLiteral("导出整版设定集"),
                                                        suggested, QStringLiteral("PNG 图像 (*.png)"));
    if (target.isEmpty()) {
        return;
    }
    const std::filesystem::path path = util::PathFromUtf8(target.toStdString());
    if (sheet.ExportPng(path)) {
        widgets::Toast::Show(QStringLiteral("设定集已导出：%1").arg(target),
                             widgets::Toast::Tone::Success);
    } else {
        widgets::Toast::Show(QStringLiteral("导出失败，请检查目标路径是否可写。"),
                             widgets::Toast::Tone::Error);
    }
}

std::filesystem::path AssetDetailView::ResolvePath(std::string_view relative) const {
    const std::filesystem::path path = util::PathFromUtf8(relative);
    return path.is_absolute() ? path : projectDir_ / path;
}

QString AssetDetailView::DetailProbe() const {
    int ready = 0;
    int decoded = 0;
    int actions = 0;
    QStringList states;
    for (const LayerData& data : layers_) {
        const bool is_ready = data.ready;
        ready += is_ready ? 1 : 0;
        decoded += data.decoded ? 1 : 0;
        actions += is_ready ? 0 : 1;
        states.push_back(QStringLiteral("%1=%2")
                             .arg(QString::fromStdString(data.key),
                                  data.artifact && !data.artifact->status.empty()
                                      ? QString::fromStdString(data.artifact->status)
                                      : QStringLiteral("PENDING")));
    }
    return QStringLiteral("asset=#%1:%2; layers=%3; ready=%4; decoded=%5; missing=%6; actions=%7; links=3; chain=%8; states=%9; runtime=%10; history=%11; active=%12; degraded=%13")
        .arg(asset_.id)
        .arg(QString::fromStdString(asset_.name))
        .arg(static_cast<int>(layers_.size()))
        .arg(ready)
        .arg(decoded)
        .arg(static_cast<int>(layers_.size()) - ready)
        .arg(actions)
        .arg(QStringLiteral("front>turnaround>base_body>wardrobe"))
        .arg(states.join(QLatin1Char('>')))
        .arg(runtime_phase_.isEmpty() ? QStringLiteral("none") : runtime_phase_)
        .arg(runtime_history_.isEmpty() ? QStringLiteral("none")
                                        : runtime_history_.join(QLatin1Char('>')))
        .arg(runtime_active_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(runtime_degraded_ ? QStringLiteral("1") : QStringLiteral("0"));
}


} // namespace shine::app
