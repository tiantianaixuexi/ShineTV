#include "ui/pages/assets/ConsistencyView.h"

#include "ui/pages/novel/WorldBoardShared.h"
#include "ui/kit/images/Viewer.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "util/Encoding.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QStringList>


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <yyjson.h>

namespace shine::app {
namespace {

[[nodiscard]] bool ShotHasEntity(std::string_view json, shine::novelcore::RowId entityId) {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        return false;
    }
    bool found = false;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_arr(root)) {
        std::size_t index = 0;
        yyjson_val* child = nullptr;
        while ((child = yyjson_arr_get(root, index++)) != nullptr) {
            if (yyjson_get_num(child) == static_cast<long long>(entityId)) {
                found = true;
                break;
            }
        }
    }
    yyjson_doc_free(doc);
    return found;
}

[[nodiscard]] QString EmotionSummary(std::string_view json) {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        return QStringLiteral("无效情绪 JSON");
    }
    QStringList parts;
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_obj_iter iter = yyjson_obj_iter_with(root);
    for (std::size_t index = 0; index < 4; ++index) {
        yyjson_val* key = yyjson_obj_iter_next(&iter);
        if (key == nullptr) {
            break;
        }
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        QString rendered;
        if (yyjson_is_num(value)) {
            rendered = QString::number(yyjson_get_num(value));
        } else if (yyjson_is_str(value)) {
            rendered = QString::fromUtf8(yyjson_get_str(value));
        } else if (yyjson_is_bool(value)) {
            rendered = yyjson_get_bool(value) ? QStringLiteral("是") : QStringLiteral("否");
        } else {
            continue;
        }
        const char* key_text = yyjson_get_str(key);
        if (key_text == nullptr) {
            continue;
        }
        parts.push_back(QString::fromUtf8(key_text) + QStringLiteral(":") + rendered);
    }
    yyjson_doc_free(doc);
    return parts.isEmpty() ? QStringLiteral("{}") : parts.join(QStringLiteral(" · "));
}

[[nodiscard]] double MeanAbsoluteDifference(const QImage& left, const QImage& right) {
    constexpr int kSide = 64;
    if (left.isNull() || right.isNull()) {
        return 1.0;
    }
    const QImage a = left.scaled(kSide, kSide, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                         .convertToFormat(QImage::Format_RGB888);
    const QImage b = right.scaled(kSide, kSide, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                         .convertToFormat(QImage::Format_RGB888);
    std::uint64_t sum = 0;
    for (int y = 0; y < kSide; ++y) {
        const auto* pa = reinterpret_cast<const std::uint8_t*>(a.constScanLine(y));
        const auto* pb = reinterpret_cast<const std::uint8_t*>(b.constScanLine(y));
        for (int x = 0; x < kSide; ++x) {
            for (int c = 0; c < 3; ++c) {
                sum += static_cast<std::uint64_t>(std::abs(static_cast<int>(pa[x * 3 + c]) -
                                                         static_cast<int>(pb[x * 3 + c])));
            }
        }
    }
    constexpr std::uint64_t kMax = static_cast<std::uint64_t>(kSide) * kSide * 3 * 255;
    return static_cast<double>(sum) / static_cast<double>(kMax);
}

} // namespace

ConsistencyView::ConsistencyView(QWidget* parent) : QWidget(parent) {
    BuildUi();
    Clear();
}

void ConsistencyView::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    title_ = widgets::SectionTitle(QStringLiteral("一致性 · 未选择资产"), this);
    outer->addWidget(title_);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    content_ = new QWidget(scroll);
    content_layout_ = new QVBoxLayout(content_);
    content_layout_->setContentsMargins(0, 0, 0, 0);
    content_layout_->setSpacing(theme::space::kSteps[2]);
    scroll->setWidget(content_);
    outer->addWidget(scroll, 1);

    auto* compare_title = SectionLabel(this, QStringLiteral("跨镜头对比"));
    outer->addWidget(compare_title);
    compare_mode_ = new widgets::Segmented(
        {QStringLiteral("并排"), QStringLiteral("滑动"), QStringLiteral("差异")}, this);
    compare_mode_->SetCurrent(0);
    outer->addWidget(compare_mode_);

    compare_ = new images::CompareView(this);
    compare_->setMinimumHeight(300);
    outer->addWidget(compare_);
    compare_result_ = new QLabel(QStringLiteral("选择至少两张同角色镜头图后开始对比。"), this);
    compare_result_->setWordWrap(true);
    widgets::SetKind(compare_result_, "statedetail");
    outer->addWidget(compare_result_);

    compare_mode_->SetOnChanged([this](int index) {
        compare_->SetMode(index == 0   ? images::CompareView::Mode::SideBySide
                          : index == 1 ? images::CompareView::Mode::Wipe
                                       : images::CompareView::Mode::Diff);
    });
}

bool ConsistencyView::ShowAsset(shine::db::sqlite::Database& db, novelcore::NovelVisual& visual,
                                const novelcore::VisualAssetRow& asset,
                                novelcore::RowId entityId,
                                const std::filesystem::path& projectDir, QString* error) {
    Clear();
    if (asset.id <= 0 || entityId <= 0) {
        const QString detail = QStringLiteral("一致性视图需要有效的资产与实体。");
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }

    auto state_result = visual.ListStates(asset.id);
    if (!state_result) {
        const QString detail = QStringLiteral("读取视觉阶段失败：%1")
                                   .arg(QString::fromStdString(state_result.error().message));
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }
    novelcore::NovelGraph graph(db);
    auto status_result = graph.ListCharacterStatuses(entityId);
    if (!status_result) {
        const QString detail = QStringLiteral("读取角色状态失败：%1")
                                   .arg(QString::fromStdString(status_result.error().message));
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }

    asset_ = asset;
    entity_id_ = entityId;
    projectDir_ = projectDir;
    for (const auto& state : *state_result) {
        StateLine line{.state = state, .tone = "ok", .note = QStringLiteral("与基线一致")};
        if (!states_.empty()) {
            const auto& baseline = states_.front().state;
            const bool appearance_changed = state.appearance != baseline.appearance;
            const bool colors_changed = state.materials_colors != baseline.materials_colors;
            line.tone = appearance_changed && colors_changed ? "danger"
                           : appearance_changed || colors_changed ? "warn"
                                                                  : "ok";
            line.note = line.tone == "ok" ? QStringLiteral("与基线一致")
                        : line.tone == "warn"
                            ? QStringLiteral("颜色或单项外观变化")
                            : QStringLiteral("外观与颜色均显著变化");
        }
        states_.push_back(std::move(line));
    }
    for (const auto& status : *status_result) {
        emotions_.push_back({.status = status, .summary = EmotionSummary(status.emotion_json)});
    }

    auto chapters = graph.ListChapters(1000);
    std::unordered_map<novelcore::RowId, int> matched_shots;
    if (chapters) {
        for (const auto& chapter : *chapters) {
            auto shots = visual.ListShotsByChapter(chapter.id);
            if (!shots) {
                continue;
            }
            for (const auto& shot : *shots) {
                if (ShotHasEntity(shot.character_ids_json, entityId)) {
                    matched_shots[shot.id] = chapter.ord;
                }
            }
        }
    }
    shot_count_ = matched_shots.size();

    auto generated = novelcore::ListGeneratedImages(db, 5000);
    if (generated) {
        for (const auto& image : *generated) {
            if (image.source_kind != "shot" || matched_shots.contains(image.source_id) == false ||
                image.rel_path.empty()) {
                continue;
            }
            const std::filesystem::path relative = util::PathFromUtf8(image.rel_path);
            const std::filesystem::path path =
                relative.is_absolute() ? relative : projectDir_ / relative;
            QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
            reader.setAutoTransform(true);
            QImage loaded = reader.read();
            if (loaded.isNull()) {
                continue;
            }
            const int chapter_ord = matched_shots[image.source_id];
            frames_.push_back({.shot_id = image.source_id,
                               .chapter_ord = chapter_ord,
                               .image = std::move(loaded),
                               .label = QStringLiteral("第 %1 章 / 镜头 #%2")
                                            .arg(chapter_ord)
                                            .arg(image.source_id)});
        }
    }

    Rebuild();
    return true;
}

void ConsistencyView::Clear() {
    asset_ = {};
    entity_id_ = 0;
    projectDir_.clear();
    states_.clear();
    emotions_.clear();
    frames_.clear();
    shot_count_ = 0;
    difference_ = -1.0;
    if (title_ != nullptr) {
        title_->setText(QStringLiteral("一致性 · 未选择资产"));
    }
    if (content_layout_ != nullptr) {
        ClearLayout(content_layout_);
    }
    if (compare_ != nullptr) {
        compare_->SetPair({}, {});
    }
    if (compare_result_ != nullptr) {
        compare_result_->setText(QStringLiteral("选择至少两张同角色镜头图后开始对比。"));
    }
}

void ConsistencyView::Rebuild() {
    if (content_layout_ == nullptr || compare_ == nullptr) {
        return;
    }
    ClearLayout(content_layout_);
    title_->setText(QStringLiteral("一致性 · %1").arg(QString::fromStdString(asset_.name)));

    content_layout_->addWidget(SectionLabel(content_, QStringLiteral("按章外观基线")));
    if (states_.empty()) {
        auto* empty = new widgets::EmptyState(
            QStringLiteral("◇"), QStringLiteral("还没有跨章变化"),
            QStringLiteral("在 visual_states 登记外观阶段后，这里会按章显示基线与差异。"),
            QString{}, content_);
        content_layout_->addWidget(empty);
    } else {
        for (const auto& line : states_) {
            auto* card = new widgets::Card(widgets::Card::Variant::Outlined, content_);
            auto* body = card->BodyLayout();
            auto* head = new QWidget(card);
            auto* head_layout = new QHBoxLayout(head);
            head_layout->setContentsMargins(0, 0, 0, 0);
            auto* title = new QLabel(QStringLiteral("第 %1 章 · %2")
                                          .arg(line.state.from_chapter)
                                          .arg(QString::fromStdString(line.state.stage_label)),
                                      head);
            widgets::SetSemibold(title, true);
            head_layout->addWidget(title, 1);
            head_layout->addWidget(new widgets::Tag(line.note, line.tone, false, head));
            body->addWidget(head);
            auto* appearance = new QLabel(QStringLiteral("外观：%1\n颜色：%2")
                                               .arg(QString::fromStdString(line.state.appearance),
                                                    QString::fromStdString(line.state.materials_colors)),
                                           card);
            appearance->setWordWrap(true);
            body->addWidget(appearance);
            content_layout_->addWidget(card);
        }
    }

    content_layout_->addWidget(SectionLabel(content_, QStringLiteral("表情基线")));
    if (emotions_.empty()) {
        auto* hint = new QLabel(QStringLiteral("character_status.emotion_json 暂无记录。"), content_);
        widgets::SetKind(hint, "statedetail");
        content_layout_->addWidget(hint);
    } else {
        for (const auto& line : emotions_) {
            auto* row = new QWidget(content_);
            auto* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);
            row_layout->addWidget(new QLabel(QStringLiteral("第 %1 章").arg(line.status.chapter_id),
                                             row));
            row_layout->addWidget(new widgets::Tag(line.summary, "accent", false, row), 1);
            content_layout_->addWidget(row);
        }
    }

    if (frames_.size() >= 2) {
        compare_->SetPair(frames_[0].image, frames_[1].image);
        difference_ = MeanAbsoluteDifference(frames_[0].image, frames_[1].image);
        const QString severity = difference_ <= 0.02   ? QStringLiteral("一致")
                                 : difference_ <= 0.12 ? QStringLiteral("轻微差异")
                                                      : QStringLiteral("显著差异");
        compare_result_->setText(QStringLiteral("%1 ↔ %2 · %3 · 像素差异 %4%")
                                     .arg(frames_[0].label, frames_[1].label, severity)
                                     .arg(difference_ * 100.0, 0, 'f', 1));
    } else {
        compare_->SetPair({}, {});
        compare_result_->setText(QStringLiteral("同角色镜头图不足两张；已找到 %1 张。")
                                     .arg(static_cast<int>(frames_.size())));
    }
}

void ConsistencyView::ShowError(const QString& detail) {
    if (content_layout_ == nullptr) {
        return;
    }
    ClearLayout(content_layout_);
    content_layout_->addWidget(
        new widgets::ErrorState(QStringLiteral("读取一致性失败"), detail, content_));
    if (compare_result_ != nullptr) {
        compare_result_->setText(detail);
    }
}
QString ConsistencyView::ConsistencyProbe() const {
    const QString severity = difference_ < 0 ? QStringLiteral("none")
                               : difference_ <= 0.02 ? QStringLiteral("same")
                               : difference_ <= 0.12 ? QStringLiteral("minor")
                                                     : QStringLiteral("significant");
    return QStringLiteral("asset=%1; states=%2; emotions=%3; shots=%4; images=%5; compared=%6; diff=%7; severity=%8")
        .arg(asset_.id)
        .arg(static_cast<int>(states_.size()))
        .arg(static_cast<int>(emotions_.size()))
        .arg(static_cast<int>(shot_count_))
        .arg(static_cast<int>(frames_.size()))
        .arg(frames_.size() >= 2 ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(difference_ < 0 ? QStringLiteral("none")
                             : QString::number(difference_ * 100.0, 'f', 1),
             severity);
}

} // namespace shine::app
