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
class QLabel;
class QPushButton;
class QWidget;

namespace shine::app {

class AssetDetailView : public QWidget {
  public:
    using GenerateHandler = std::function<void(novelcore::AssetLayer)>;

    using GenerateAllHandler = std::function<void()>;

    explicit AssetDetailView(QWidget* parent = nullptr);

    bool ShowAsset(novelcore::NovelVisual& visual, const novelcore::VisualAssetRow& asset,
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

    void BuildUi();
    void Rebuild();
    void RebuildDerive();
    void ShowError(const QString& detail);
    void ExportSheet();
    [[nodiscard]] std::filesystem::path ResolvePath(std::string_view relative) const;
    static std::string ArtifactPath(const LayerData& layer);

    std::vector<LayerData> layers_;
    novelcore::VisualAssetRow asset_;
    std::filesystem::path projectDir_;
    GenerateHandler on_generate_;
    GenerateAllHandler on_generate_all_;
    QString runtime_phase_;
    QString runtime_detail_;
    QString runtime_layer_;
    QStringList runtime_history_;
    bool runtime_active_ = false;
    bool runtime_degraded_ = false;

    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QPushButton* export_ = nullptr;
    QLabel* runtime_label_ = nullptr;
    QWidget* cards_ = nullptr;
    QGridLayout* cards_layout_ = nullptr;
    QPushButton* generate_all_ = nullptr;
    QWidget* derive_ = nullptr;       // webui .derive 派生链容器
    QHBoxLayout* derive_row_ = nullptr;
    AssetPolicyPanel* policy_ = nullptr;
};

} // namespace shine::app
