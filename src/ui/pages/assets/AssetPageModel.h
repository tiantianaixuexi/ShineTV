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
#include "ui/pages/assets/AssetPolicy.h"
#include "ui/pages/assets/AssetVisualData.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
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

    // ④ 项目参考库真数据（assets/refs/，只读真库 refs.json + 文件 stat）
    //   {id,originalName,absPath,orientation,rawWidth,rawHeight,
    //    displayWidth,displayHeight,entityId,markers:[…]}
    // 空态也带全零值键（同 EmptyRuntimeMap 的纪律）。
    [[nodiscard]] Q_PROPERTY(QVariantList refImages READ refImages NOTIFY changed)
    QVariantList refImages() const;

    // 当前选中的参考图 id（空串 = 没选）。**选中态必须是属性、不能是方法**：
    // QML 侧的高亮与标记回填都是绑定，方法调用在依赖图里是空的、只求值一次，
    // 首屏之后再也跟不上选中变化（面板只能自己在本地镜像一份）。
    [[nodiscard]] Q_PROPERTY(QString selectedRefId READ selectedRefId NOTIFY changed)
    QString selectedRefId() const { return refSelectedId_; }

    // 依赖策略：allowDegrade / suspendTimeoutMs 两个真实开关。
    // {allowDegrade,timeoutMinutes,strict,summary,phase,layer,active,degraded}
    [[nodiscard]] Q_PROPERTY(QVariantMap policy READ policy NOTIFY changed)
    QVariantMap policy() const;

    // 导出整版设定集：最近一次导出的结果（空 = 从没导过）
    //   {busy,lastPath,lastBytes,count,ok,message}
    [[nodiscard]] Q_PROPERTY(QVariantMap exportState READ exportState NOTIFY changed)
    QVariantMap exportState() const;

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
    Q_INVOKABLE QString entityNameById(const QString& id) const;

    // 尚未迁 QML 的能力（导出整版 / 参考库导入…）按下时给明确提示，
    // 而不是让按钮点了没反应、或谎称成功。
    Q_INVOKABLE void exportingUnsupported(const QString& what);

    // —— ① 导出整版设定集 PNG（迁自 AssetDetailView::ExportSheet）——
    // 收集就绪层 → SheetGrid 2×360×280 合成 → 存盘。
    // target 为空时走 QFileDialog 选路径（离屏/验收走显式 target）。
    // 解码 + 合成 + 写盘全在 worker，结果经 async::PostToUi 回填 exportState。
    Q_INVOKABLE void exportSheet(const QString& target = {});
    // 供验收等结果：合成 + 写盘是否已收敛（不阻塞事件循环）
    Q_INVOKABLE bool exportFinished() const { return !exportBusy_; }

    // —— ② 参考库：导入 / 标记 / 绑定 / 删除（迁自 RefLibraryView）——
    // 导入是异步的（worker 解码 + 写回 refs.json），返回提交数；
    // 实际落库数看 refImages 与 refProbe。
    Q_INVOKABLE int importReferences(const QStringList& paths);
    Q_INVOKABLE void chooseReferenceFiles();          // QFileDialog 多选
    Q_INVOKABLE void selectReference(const QString& id);
    Q_INVOKABLE void setReferenceMarkers(const QString& markers); // 逗号/顿号分隔
    Q_INVOKABLE void bindReferenceToEntity();          // 绑到当前选中实体 + 资产
    Q_INVOKABLE void removeReference();
    Q_INVOKABLE void refreshReferences();
    [[nodiscard]] Q_INVOKABLE bool referencesBusy() const { return refBusy_; }
    // 验收等导入收敛用
    Q_INVOKABLE bool waitReferences(int timeoutMs = 10000);

    // —— ③ 依赖策略面板（迁自 AssetPolicyPanel）——
    Q_INVOKABLE void setAllowDegrade(bool allow);
    Q_INVOKABLE void setSuspendMinutes(int minutes);

    void SetAssetPolicy(const AssetPolicy& policy) { policy_ = policy; }
    [[nodiscard]] const AssetPolicy& Policy() const { return policy_; }

    // 验收探针（与 Widgets 侧同名同格式，供 P05 取证比对）
    //
    // ⚠️ 口径纪律：**空态也必须给出真实字段**。原来 Widgets 侧 AssetProbe 有
    // `empty=1`（QML 侧漏了，只能靠 Assets.qml 的 Empty 覆盖层看出来），
    // 结果 S1 的「空态」断言对着 QML 跑必然失败。空态是**真实状态**，
    // 不该只存在于视图层 —— 现在由 assetsView_.isEmpty() 直接算出。
    [[nodiscard]] QString AssetProbe() const;
    [[nodiscard]] QString StateProbe() const;
    [[nodiscard]] QString DetailProbe() const;
    [[nodiscard]] QString PolicyProbe() const;
    [[nodiscard]] QString ConsistencyProbe() const;
    [[nodiscard]] QString RefProbe() const;

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
    void RebuildRefs();
    void ProjectRefs(const std::vector<AssetRefFact>& refs);
    void ProjectPolicy();
    bool StartRun(std::optional<novelcore::AssetLayer> layer);
    [[nodiscard]] QVariantMap AssetToMap(const AssetEntry& entry) const;
    [[nodiscard]] QVariantMap RuntimeToMap(qint64 assetId) const;
    [[nodiscard]] std::optional<novelcore::AssetLayer> LayerFromKey(const QString& key) const;
    [[nodiscard]] const AssetEntry* SelectedAsset() const;
    [[nodiscard]] QString SelectedAssetName() const;

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
    QVariantList refView_;
    QVariantMap policyView_;
    QVariantMap exportView_;

    // —— 参考库（④）——
    // 取数一律经 visual::ReferenceLibrary + AssetVisualData.h 的 AssetCollectRefs，
    // **不在 UI 层另写一份映射**（原来只在 RefLibraryView.cpp 里）。
    std::unique_ptr<visual::ReferenceLibrary> refs_;
    QString refSelectedId_;
    std::vector<AssetRefFact> refFacts_;
    bool refBusy_ = false;
    // 导入在 worker 上跑；本对象是它的存活性守卫（worker 不碰 QObject）
    std::shared_ptr<std::atomic<bool>> refBusyFlag_;

    // —— 导出整版（①）——
    bool exportBusy_ = false;
};

} // namespace shine::app
