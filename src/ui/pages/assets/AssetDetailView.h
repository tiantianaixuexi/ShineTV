#pragma once
// P05-S2 单资产详情：四层形象产物、父子派生关系、缺层行动与整版导出。
// 数据只读 NovelVisual；生成/导入动作通过回调上交工作区，避免 UI 自己伪造生产结果。
#include "novel/NovelAssetPipeline.h"
#include "novel/NovelVisual.h"
#include "ui/pages/assets/AssetPolicyPanel.h"

#include <QWidget>
#include <QString>
#include <QStringList>


#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QWidget;

namespace shine::db::sqlite {
class Database;
} // namespace shine::db::sqlite

namespace shine::data {
class KeyValue;
} // namespace shine::data

namespace shine::app {

class AssetDetailView : public QWidget {
  public:
    using GenerateHandler = std::function<void(novelcore::AssetLayer)>;

    using GenerateAllHandler = std::function<void()>;

    explicit AssetDetailView(QWidget* parent = nullptr);

    // db / entityId 只读：关联时间线要按章取 visual_states 与 shots.character_ids_json，
    // 光有 NovelVisual 拿不到章节与镜头。
    bool ShowAsset(db::sqlite::Database& db, novelcore::NovelVisual& visual,
                   const novelcore::VisualAssetRow& asset, novelcore::RowId entityId,
                   const std::filesystem::path& projectDir, QString* error = nullptr);
    void Clear();
    void SetOnGenerate(GenerateHandler handler) { on_generate_ = std::move(handler); }
    void SetOnGenerateAll(GenerateAllHandler handler) { on_generate_all_ = std::move(handler); }
    void SetRuntimeState(QString phase, QString detail, QStringList history, QString layer,
                         bool active, bool degraded);
    [[nodiscard]] QString DetailProbe() const;
    [[nodiscard]] AssetPolicy Policy() const;
    void SetPolicy(const AssetPolicy& policy);
    [[nodiscard]] QString PolicyProbe() const;

  private:
    struct LayerData {
        novelcore::AssetLayer layer = novelcore::AssetLayer::Front;
        std::string key;
        QString title;
        std::string parent_key;
        std::optional<novelcore::VisualArtifactRow> artifact;
        bool legacy_front = false;
        bool ready = false;
        bool decoded = false;
    };

    // webui .tl .ev：一次按章的外观/出处事件。hot = 当前生效的基线（accent 针）。
    struct TimelineEvent {
        int chapter = 0; // 1 基；0 = 未定位到章，落在轴首
        QString label;
        QString tip;
        bool hot = false;
    };
    // webui .tl-below「绑定镜头」chip：一枚 = 一镜。
    struct BoundShot {
        int chapter = 0;
        int ord = 0;
        novelcore::RowId id = 0;
        QString image_rel;
    };

    void BuildUi();
    void Rebuild();
    void RebuildDerive();
    void RebuildTimeline();
    void RebuildKeyValue();
    void CollectTimeline(db::sqlite::Database& db, novelcore::NovelVisual& visual,
                         const novelcore::VisualAssetRow& asset, novelcore::RowId entityId);
    void ShowError(const QString& detail);
    void ExportSheet();
    [[nodiscard]] std::filesystem::path ResolvePath(std::string_view relative) const;
    static std::string ArtifactPath(const LayerData& layer);

    std::vector<LayerData> layers_;
    novelcore::VisualAssetRow asset_;
    novelcore::RowId entity_id_ = 0; // .kv「绑定实体」行
    QString entity_kind_;            // .kv「类别」行的实体 kind（中文标签由 KindLabelOf 给出）
    std::filesystem::path projectDir_;
    GenerateHandler on_generate_;
    GenerateAllHandler on_generate_all_;
    QString runtime_phase_;
    QString runtime_detail_;
    QString runtime_layer_;
    QStringList runtime_history_;
    bool runtime_active_ = false;
    bool runtime_degraded_ = false;

    // 关联时间线（webui .tl + .tl-below）
    int timeline_chapters_ = 0;
    std::vector<TimelineEvent> timeline_events_;
    std::vector<BoundShot> bound_shots_;
    std::vector<QString> ref_images_;

    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QPushButton* export_ = nullptr;
    QLabel* runtime_label_ = nullptr;
    shine::data::KeyValue* facts_ = nullptr; // webui .kv：设定集分区的两列键值
    QWidget* cards_ = nullptr;
    QGridLayout* cards_layout_ = nullptr;
    QPushButton* generate_all_ = nullptr;
    QWidget* derive_ = nullptr;       // webui .derive 派生链容器
    QHBoxLayout* derive_row_ = nullptr;
    QWidget* timeline_ = nullptr;     // webui .tl 事件轴容器
    QWidget* timeline_body_ = nullptr; // 轴 + .tl-below 的列容器
    QVBoxLayout* timeline_layout_ = nullptr;
    AssetPolicyPanel* policy_ = nullptr;
};

} // namespace shine::app
