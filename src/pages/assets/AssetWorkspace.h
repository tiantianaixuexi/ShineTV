#pragma once
// P05-S1 视觉资产工作区骨架（PLAN §5 S1 / UI.md §2.1）：
//   左：人物 / 地点 / 物品 / 势力实体树（可按 kind 过滤）；
//   中：当前书 visual_assets 的卡片网格，卡片显示状态、资产名与实体绑定。
// P07 尚未接入实际出图；空态明确引导「从实体生成视觉资产」，不伪造生成成功。
#include "novel/NovelAssetPipeline.h"
#include "novel/NovelVisual.h"
#include "media/GalleryTypes.h"
#include "pages/assets/AssetPolicyPanel.h"
#include "visual/ReferenceChain.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

class QGridLayout;
class QStackedWidget;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace shine::db::sqlite {
class Database;
}

namespace shine::app {

class AssetDetailView;
class ConsistencyView;
class RefLibraryView;
class GalleryWorkspace;
class AssetWorkspace : public QWidget {
  public:
    explicit AssetWorkspace(QWidget* parent = nullptr);
    ~AssetWorkspace() override = default;

    // 打开当前书库。dbPath 是 novel.db；projectDir 是当前书根，用于解析
    // visual_assets.sheet_rel_path。error 非空时同时接收中文失败原因。
    bool OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                  QString* error = nullptr);
    void CloseBook() noexcept;
    bool RefreshAssets();

    // id=0 取消实体选择；非 0 时选择受支持 kind 下的实体并刷新卡片。
    bool SelectEntity(qint64 id);
    [[nodiscard]] QString AssetProbe() const;
    bool SelectAsset(qint64 id);
    [[nodiscard]] QString DetailProbe() const;
    bool StartAssetLayer(novelcore::AssetLayer layer);
    bool StartAssetPipeline();
    [[nodiscard]] QString StateProbe() const;
    void SetAssetPolicy(const AssetPolicy& policy);
    [[nodiscard]] QString PolicyProbe() const;
    [[nodiscard]] QString ConsistencyProbe() const;
    [[nodiscard]] QString RefProbe() const;
    std::size_t ImportReferences(const std::vector<std::filesystem::path>& paths);
    void RefreshReferences();
    [[nodiscard]] QString GlobalGalleryProbe() const;
    void SelectGlobalGallerySource(gallery::SourceKind source);
    bool SelectFirstGlobalGallery();
    [[nodiscard]] QStringList ReferenceUsageLabels() const;
    bool ActivateReferenceUsage(int index);
    void ShowDetailPage(int index);
    [[nodiscard]] GalleryWorkspace* GlobalGallery() const { return global_gallery_; }

  private:
    struct AssetEntry {
        novelcore::VisualAssetRow asset;
        novelcore::EntityRow entity;
        bool degraded = false;
    };
    struct RuntimeState {
        qint64 run_id = 0;
        QString phase;
        QString detail;
        QString layer;
        QStringList history;
        bool active = false;
        bool degraded = false;
    };
    void ShowSelectedAsset();
    std::vector<visual::ReferenceUsage> ResolveReferenceUsages(
        const std::filesystem::path& image_path) const;
    bool ActivateReferenceUsage(const visual::ReferenceUsage& usage);
    bool ShowShotReference(std::int64_t shot_id);

    void BuildUi();
    bool RefreshEntities();
    void RebuildEntityTree();
    void RebuildAssets();
    void ApplyKindFilter();
    void SetKindFilter(const QString& kind);
    void SyncKindButtons();
    void ResetForClosedBook() noexcept;
    void ShowGenerationNextStep() const;
    bool StartAssetRun(std::optional<novelcore::AssetLayer> layer);
    void PushRuntimeToDetail();
    void BeginRunChecking(qint64 assetId, qint64 runId, QString detail, QString finalPhase,
                          bool degraded);
    void FinishAssetRun(qint64 assetId, qint64 runId, QString phase, QString detail,
                        bool degraded);

    std::unique_ptr<db::sqlite::Database> db_;
    std::filesystem::path dbPath_;
    std::filesystem::path projectDir_;
    QString bookName_;
    QString openError_;
    QString kindFilter_;
    qint64 selectedEntityId_ = 0;
    qint64 selectedAssetId_ = 0;

    std::vector<novelcore::EntityRow> entities_;
    std::vector<AssetEntry> assets_;
    QHash<qint64, QTreeWidgetItem*> entityItems_;
    QHash<qint64, RuntimeState> runtime_;
    qint64 runSerial_ = 0;

    QLabel* bookLabel_ = nullptr;
    QLabel* assetCount_ = nullptr;
    QTreeWidget* entityTree_ = nullptr;
    QWidget* assetsHost_ = nullptr;
    QGridLayout* assetsGrid_ = nullptr;
    AssetDetailView* detail_ = nullptr;
    GalleryWorkspace* global_gallery_ = nullptr;
    QStackedWidget* detail_stack_ = nullptr;
    ConsistencyView* consistency_ = nullptr;
    RefLibraryView* references_ = nullptr;
    std::array<QPushButton*, 5> kindButtons_{};
};

} // namespace shine::app
