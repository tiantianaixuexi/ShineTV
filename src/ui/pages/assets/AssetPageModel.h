#pragma once
// ui/pages/assets/AssetPageModel —— 资产页的 C++→QML 数据桥（QML 迁移）
//
// 定位：把 AssetWorkspace 原本直接喂给 QWidget 的数据，原样投影给 QML 页。
// **零新增真值**：字段、状态词表、tone 映射全部沿用 Widgets 侧原有的那一份
// （kAssetEntityKinds / StatusLabel / StatusTone），QML 只是换个渲染层。
//
// ============================ 为什么全是 Q_PROPERTY ============================
// 与 ThemeBridge 同一纪律：**随变化重算的量一律属性 + NOTIFY**，方法只留给
// 「带参数的动作」。QML 绑定只对属性读建立依赖；方法调用在依赖图里是空的，
// 求值一次就永久缓存，之后发多少 changed() 都不会重算。
//
// 复合值统一用 QVariantList（of QVariantMap）：QML 侧直接就是 JS 的
// 数组 + 对象字面量，页面里 `entities[i].name` 这种写法一行都不用改。
// 换成 QAbstractListModel 的收益是增删动画，代价是要重写页面里全部
// Repeater 绑定 —— 收益不足以抵消首轮迁移的风险，故 v1 用属性。
// =========================================================================
//
// 线程：全部在 GUI 线程同步完成（与 Widgets 侧 RefreshEntities /
// RefreshAssets 同样的取数方式），本类不做 IO 之外的跨线程通信。
#include "novel/NovelAssetPipeline.h"
#include "novel/NovelTypes.h"
#include "novel/NovelVisual.h"
#include "ui/pages/assets/AssetPolicyPanel.h"
#include "ui/pages/assets/AssetVisualData.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace shine::db::sqlite {
class Database;
}

namespace shine::app {

class AssetPageModel : public QObject {
    Q_OBJECT

  public:
    explicit AssetPageModel(QObject* parent = nullptr);
    ~AssetPageModel() override;

    // —— 生命周期：与 AssetWorkspace 同名同义 ——
    bool OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                  QString* error = nullptr);
    void CloseBook() noexcept;
    bool RefreshAssets();
    bool RefreshEntities();

    // —— 暴露给 QML 的数据（全部带 NOTIFY changed）——
    [[nodiscard]] Q_PROPERTY(QVariantList entityGroups READ entityGroups NOTIFY changed)
    QVariantList entityGroups() const;

    [[nodiscard]] Q_PROPERTY(QVariantList assets READ assets NOTIFY changed)
    QVariantList assets() const;

    [[nodiscard]] Q_PROPERTY(QVariantList kinds READ kinds NOTIFY changed)
    QVariantList kinds() const;

    [[nodiscard]] Q_PROPERTY(QVariantList deriveChain READ deriveChain NOTIFY changed)
    QVariantList deriveChain() const;

    // ① 形象层真数据（visual_artifacts + 旧单图回退 + 落盘存在性）
    // 每项 {key,title,parent,relPath,absPath,status,ready,legacyFront}
    [[nodiscard]] Q_PROPERTY(QVariantList layers READ layers NOTIFY changed)
    QVariantList layers() const;

    // ② 一致性真数据。空态也带全键（同 runtime 的纪律）：
    //   {states:[{chapterOrd,where,stage,appearance,colors,note,tone}],
    //    emotions:[{chapterOrd,where,summary}],
    //    frames:[{shotId,chapterOrd,label,absPath,hasImage}],
    //    shotCount, severity, verdict, firstLabel, lastLabel,
    //    hasStates, hasFrames, canCompare, diffPct}
    [[nodiscard]] Q_PROPERTY(QVariantMap consistency READ consistency NOTIFY changed)
    QVariantMap consistency() const;

    // ③ 关联时间线真数据：
    //   {chapterCount, events:[{chapterOrd,label,hot,shotId,isShot}],
    //    shots:[{id,label,refs:[]}], refImages:[absPath], hasData}
    [[nodiscard]] Q_PROPERTY(QVariantMap timeline READ timeline NOTIFY changed)
    QVariantMap timeline() const;

    // 一致性右列的「对比对象 / 基线帧 / 当前帧 / 检测项」四行，页面直接喂 Kv
    [[nodiscard]] Q_PROPERTY(QVariantList compareKv READ compareKv NOTIFY changed)
    QVariantList compareKv() const;

    // 逐段差异行 [{key, changed}]，外观/色彩分开判
    [[nodiscard]] Q_PROPERTY(QVariantList diffRows READ diffRows NOTIFY changed)
    QVariantList diffRows() const;

    // 设定集事实行（实体 id / 层就绪数 / 基线段数 / 镜头数）
    [[nodiscard]] Q_PROPERTY(QVariantList sheetKv READ sheetKv NOTIFY changed)
    QVariantList sheetKv() const;

    [[nodiscard]] Q_PROPERTY(QVariantMap runtime READ runtime NOTIFY changed)
    QVariantMap runtime() const;

    [[nodiscard]] Q_PROPERTY(QString bookName READ bookName NOTIFY changed)
    QString bookName() const;

    [[nodiscard]] Q_PROPERTY(QString openError READ openError NOTIFY changed)
    QString openError() const;

    [[nodiscard]] Q_PROPERTY(QString kindFilter READ kindFilter NOTIFY changed)
    QString kindFilter() const;

    [[nodiscard]] Q_PROPERTY(QString selectedEntityId READ selectedEntityId NOTIFY changed)
    QString selectedEntityId() const;

    [[nodiscard]] Q_PROPERTY(QString selectedAssetId READ selectedAssetId NOTIFY changed)
    QString selectedAssetId() const;

    [[nodiscard]] Q_PROPERTY(bool hasBook READ hasBook NOTIFY changed)
    bool hasBook() const;

    [[nodiscard]] Q_PROPERTY(bool busy READ busy NOTIFY changed)
    bool busy() const;

    // 帧图像素差异是否已算完。首轮 RebuildVisualFacts 时帧图在 worker 上解码，
    // 差异是异步回填的 —— 抓图/断言前应等这个为 true，否则会读到「未比对」的中间态。
    [[nodiscard]] Q_PROPERTY(bool visualsReady READ visualsReady NOTIFY visualsChanged)
    bool visualsReady() const;

    // —— 动作（Q_INVOKABLE：带参数的操作，不参与绑定）——
    Q_INVOKABLE void setKindFilter(const QString& kind);
    Q_INVOKABLE void selectEntity(const QString& id);
    Q_INVOKABLE void selectAsset(const QString& id);
    Q_INVOKABLE bool startLayer(const QString& layer); // "front" / "turnaround" / "body" / "wardrobe"
    Q_INVOKABLE bool startPipeline();
    Q_INVOKABLE bool refreshAssets();
    // 尚未迁 QML 的能力（导出整版 / 参考库导入…）按下时给明确提示，
    // 而不是让按钮点了没反应、或谎称成功。
    Q_INVOKABLE void exportingUnsupported(const QString& what);
    Q_INVOKABLE QString entityNameById(const QString& id) const;

    void SetAssetPolicy(const AssetPolicy& policy) { policy_ = policy; }
    [[nodiscard]] const AssetPolicy& Policy() const { return policy_; }

    // 验收探针（与 Widgets 侧同名同格式，供 P05 取证比对）
    [[nodiscard]] QString AssetProbe() const;
    [[nodiscard]] QString StateProbe() const;

    // 由运行器回调：推进选中资产的运行时状态
    void BeginRunChecking(qint64 assetId, qint64 runId, QString detail, QString finalPhase,
                          bool degraded);
    void FinishAssetRun(qint64 assetId, qint64 runId, QString phase, QString detail, bool degraded);

  signals:
    // QML 绑定的唯一重算入口
    void changed();
    // 视觉事实（帧图 + 像素差异）异步就绪时单独发一次，
    // 让取证/断言能等它而不必在 UI 侧硬 sleep。
    void visualsChanged();
    // 需要外壳/宿主动作时用（当前留给后续的出图确认流）
    void pipelineRequested();

  private:
    struct AssetEntry;
    struct RuntimeState;
    void RebuildDerived();
    void RefreshKindCounts();
    void RebuildVisualFacts();
    void ApplyDifferenceAsync(double difference);
    void ProjectConsistency(const AssetConsistencyFact& fact);
    void ProjectTimeline(const AssetTimelineFact& fact);
    bool StartRun(std::optional<novelcore::AssetLayer> layer);
    [[nodiscard]] QVariantMap AssetToMap(const AssetEntry& entry) const;
    [[nodiscard]] QVariantMap RuntimeToMap(qint64 assetId) const;
    [[nodiscard]] std::optional<novelcore::AssetLayer> LayerFromKey(const QString& key) const;
    [[nodiscard]] const AssetEntry* SelectedAsset() const;

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
    QHash<qint64, RuntimeState> runtime_;
    qint64 runSerial_ = 0;
    // 帧图像素差异的迟到回填守卫：每次 RebuildVisualFacts 自增
    std::uint64_t visualToken_ = 0;
    // 有帧图在 worker 上待算时为 false，算完（或无需算）为 true
    bool visualsPending_ = false;
    AssetPolicy policy_;
    AssetConsistencyFact consistency_; // 保留原始事实，供异步算完差异后重投影

    QVariantList entityGroups_; // [{key,label,count,items:[{id,name,summary}]}]
    QVariantList assetsView_;   // 过滤后的卡片数据
    QVariantList kinds_;        // [{key,label,count,on}]
    QVariantList deriveChain_;
    QVariantList layersView_;
    QVariantList compareKv_;
    QVariantList diffRows_;
    QVariantList sheetKv_;
    QVariantMap runtimeView_;
    QVariantMap consistencyView_;
    QVariantMap timelineView_;
};

} // namespace shine::app
