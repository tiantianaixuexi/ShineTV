#pragma once
// P05-S5 角色外观一致性：按章 visual_states、emotion_json 基线与跨镜头 CompareView。
#include "novel/NovelVisual.h"

#include <QImage>
#include <QString>

#include <QWidget>

#include <filesystem>
#include <optional>
#include <cstddef>
#include <vector>

class QLabel;
class QVBoxLayout;
class QWidget;

namespace shine::db::sqlite {
class Database;
}
namespace shine::images {
class CompareView;
}
namespace shine::widgets {
class Segmented;
}

namespace shine::app {

class ConsistencyView : public QWidget {
  public:
    explicit ConsistencyView(QWidget* parent = nullptr);

    bool ShowAsset(shine::db::sqlite::Database& db, novelcore::NovelVisual& visual,
                   const novelcore::VisualAssetRow& asset, novelcore::RowId entityId,
                   const std::filesystem::path& projectDir, QString* error = nullptr);
    void Clear();
    [[nodiscard]] QString ConsistencyProbe() const;

  private:
    struct StateLine {
        novelcore::VisualStateRow state;
        const char* tone = "ok";
        QString note;
    };
    struct EmotionLine {
        novelcore::CharacterStatusRow status;
        QString summary;
    };
    struct ShotFrame {
        novelcore::RowId shot_id = 0;
        int chapter_ord = 0;
        QImage image;
        QString label;
    };

    void BuildUi();
    void Rebuild();
    void ShowError(const QString& detail);

    novelcore::VisualAssetRow asset_;
    novelcore::RowId entity_id_ = 0;
    std::filesystem::path projectDir_;
    std::vector<StateLine> states_;
    std::vector<EmotionLine> emotions_;
    std::vector<ShotFrame> frames_;
    std::size_t shot_count_ = 0;
    double difference_ = -1.0;

    QLabel* title_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* content_layout_ = nullptr;
    images::CompareView* compare_ = nullptr;
    widgets::Segmented* compare_mode_ = nullptr;
    QLabel* compare_result_ = nullptr;
};

} // namespace shine::app
