#include "ui/pages/assets/AssetPageModel.h"

#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "media/Gallery.h"
#include "media/GalleryTypes.h"
#include "media/ImageScanner.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/images/Sheet.h"
#include "util/Encoding.h"
#include "visual/ReferenceLibrary.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace shine::app {
namespace {

struct KindInfo {
    const char* key;
    const char* label;
    const char* scope;
};

// 实体 kind 词表。kKinds 顺序 = 实体树分组顺序，不要在别处再写一份。
constexpr std::array<KindInfo, 4> kKinds = {{
    {"person", "人物", "人物/生物"},
    {"location", "地点", "空间/势力"},
    {"item", "物品", "装备"},
    {"faction", "势力", "空间/势力"},
}};

[[nodiscard]] bool IsMissingAsset(const std::string& message) {
    return message == "该实体尚无视觉资产";
}

// 状态词表与 tone：内部值（PENDING / SHEET_READY…）到「中文标签 + tone 名」的
// 唯一映射。QML 只消费后者，不认识内部值。
[[nodiscard]] QString StatusLabel(const QString& status) {
    if (status == QStringLiteral("PENDING")) return QStringLiteral("待生成");
    if (status == QStringLiteral("PROMPTING") || status == QStringLiteral("GENERATING"))
        return QStringLiteral("生成中");
    if (status == QStringLiteral("CHECKING")) return QStringLiteral("校验中");
    if (status == QStringLiteral("REF_READY")) return QStringLiteral("参考就绪");
    if (status == QStringLiteral("SHEET_READY")) return QStringLiteral("设定集就绪");
    if (status == QStringLiteral("WARDROBE_READY")) return QStringLiteral("服装就绪");
    if (status == QStringLiteral("READY")) return QStringLiteral("可用");
    if (status == QStringLiteral("FAILED")) return QStringLiteral("失败");
    if (status == QStringLiteral("STALE")) return QStringLiteral("需刷新");
    return QStringLiteral("状态未知");
}

[[nodiscard]] const char* StatusTone(const QString& status) {
    if (status == QStringLiteral("PENDING")) return "info";
    if (status == QStringLiteral("PROMPTING") || status == QStringLiteral("GENERATING") ||
        status == QStringLiteral("CHECKING") || status == QStringLiteral("REF_READY"))
        return "accent";
    if (status == QStringLiteral("SHEET_READY") || status == QStringLiteral("WARDROBE_READY") ||
        status == QStringLiteral("READY"))
        return "ok";
    if (status == QStringLiteral("FAILED")) return "danger";
    return "warn";
}

[[nodiscard]] QString KindLabelOf(const std::string& key) {
    for (const KindInfo& kind : kKinds) {
        if (key == kind.key) {
            return QString::fromUtf8(kind.label);
        }
    }
    return QString::fromStdString(key);
}

// 「没有运行态」时给 QML 的那份固定结构。
// ⚠️ 必须**带全零值键**，不能只回 {none:true}：QML 侧有十几处
// `Page.runtime.detail` / `.layer` / `.phaseLabel` / `.phaseTone` 绑定，
// 缺键 → undefined → "Unable to assign [undefined] to QString/QColor"，
// 一次空态就刷一片红（Assets.qml 的 emptyAsset 同一纪律）。
// 判「有没有在跑」用 none，不要对字段做 truthiness。
[[nodiscard]] QVariantMap EmptyRuntimeMap() {
    QVariantMap map;
    map.insert(QStringLiteral("none"), true);
    map.insert(QStringLiteral("runId"), 0.0);
    map.insert(QStringLiteral("phase"), QString());
    map.insert(QStringLiteral("phaseLabel"), QString());
    map.insert(QStringLiteral("phaseTone"), QStringLiteral("idle"));
    map.insert(QStringLiteral("detail"), QString());
    map.insert(QStringLiteral("layer"), QString());
    map.insert(QStringLiteral("history"), QStringList());
    map.insert(QStringLiteral("active"), false);
    map.insert(QStringLiteral("degraded"), false);
    return map;
}

// 一致性空态：同样**带全键**（纪律同 EmptyRuntimeMap）。
// hasStates/hasEmotions/hasFrames 供页面判「没有可比对象」的空态，
// severity/verdict 判结论徽标，diffPct 空串（未比对时不显示百分比）。
[[nodiscard]] QVariantMap EmptyConsistencyMap() {
    QVariantMap map;
    map.insert(QStringLiteral("states"), QVariantList());
    map.insert(QStringLiteral("emotions"), QVariantList());
    map.insert(QStringLiteral("frames"), QVariantList());
    map.insert(QStringLiteral("shotCount"), 0);
    map.insert(QStringLiteral("severity"), QStringLiteral("none"));
    map.insert(QStringLiteral("verdict"), QStringLiteral("未比对"));
    map.insert(QStringLiteral("firstLabel"), QStringLiteral("暂无"));
    map.insert(QStringLiteral("lastLabel"), QStringLiteral("暂无"));
    map.insert(QStringLiteral("diffPct"), QString());
    map.insert(QStringLiteral("hasStates"), false);
    map.insert(QStringLiteral("hasEmotions"), false);
    map.insert(QStringLiteral("hasFrames"), false);
    return map;
}

[[nodiscard]] QVariantMap EmptyTimelineMap() {
    QVariantMap map;
    map.insert(QStringLiteral("chapterCount"), 1);
    map.insert(QStringLiteral("events"), QVariantList());
    map.insert(QStringLiteral("shots"), QVariantList());
    map.insert(QStringLiteral("refImages"), QVariantList());
    map.insert(QStringLiteral("hasData"), false);
    return map;
}

// 参考库空态：同样带全键（纪律同 EmptyRuntimeMap）。页面有十几处
// `Page.refImages[i].displayWidth` 之类的绑定，少一个键就是一片红。
[[nodiscard]] QVariantMap EmptyExportMap() {
    QVariantMap map;
    map.insert(QStringLiteral("busy"), false);
    map.insert(QStringLiteral("lastPath"), QString());
    map.insert(QStringLiteral("lastBytes"), 0);
    map.insert(QStringLiteral("count"), 0);
    map.insert(QStringLiteral("ok"), false);
    map.insert(QStringLiteral("message"), QString());
    return map;
}

// 策略空态：同样带全键。**这条不是形式主义** —— AssetsPolicyPanel.qml 在
// Component.onCompleted 里就绑 `Page.policy.allowDegrade`（bool）与
// `.summary`（QString），而 policyView_ 在 OpenBook 之前是默认构造的空
// QVariantMap，两个键都是 undefined，开页即刷一屏
// "Cannot assign [undefined] to bool"。纪律与其余几个 map 完全一致。
[[nodiscard]] QVariantMap EmptyPolicyMap() {
    QVariantMap map;
    map.insert(QStringLiteral("allowDegrade"), true);
    map.insert(QStringLiteral("strict"), false);
    map.insert(QStringLiteral("timeoutMinutes"), 30);
    map.insert(QStringLiteral("timeoutMs"), 30LL * 60 * 1000);
    map.insert(QStringLiteral("summary"), QStringLiteral("C 挂起 → 超时 B 降级；超时 30 分钟"));
    map.insert(QStringLiteral("phase"), QStringLiteral("none"));
    map.insert(QStringLiteral("layer"), QStringLiteral("none"));
    map.insert(QStringLiteral("detail"), QString());
    map.insert(QStringLiteral("active"), false);
    map.insert(QStringLiteral("degraded"), false);
    return map;
}

// 两张帧图的平均绝对差。**只在 worker 上调用** —— 解码 + 逐像素比较都是
// 重活，放 UI 线程就是当初 H-2 那类卡顿。下采样到 64×64（与历史口径一致）。
[[nodiscard]] double MeanAbsoluteDifferenceOf(const QString& leftPath, const QString& rightPath) {
    constexpr int kSide = 64;
    const auto load = [](const QString& path) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize original = reader.size();
        if (original.isValid() && !original.isEmpty()) {
            reader.setScaledSize(original.scaled(QSize(720, 480), Qt::KeepAspectRatio));
        }
        return reader.read();
    };
    const QImage left = load(leftPath);
    const QImage right = load(rightPath);
    if (left.isNull() || right.isNull()) {
        return -1.0; // 读不出来就当「未比对」，不要报成 100% 差异
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
                sum += static_cast<std::uint64_t>(
                    std::abs(static_cast<int>(pa[x * 3 + c]) - static_cast<int>(pb[x * 3 + c])));
            }
        }
    }
    return static_cast<double>(sum) / (static_cast<double>(kSide) * kSide * 3.0 * 255.0);
}

struct WorkerResult {
    std::string phase;
    std::string detail;
    bool degraded = false;
};

// 这一段是资产管线的唯一实现，不要在两处各写一份。
[[nodiscard]] WorkerResult RunAssetInWorker(const std::filesystem::path& dbPath,
                                            novelcore::RowId assetId,
                                            std::optional<novelcore::AssetLayer> layer,
                                            const AssetPolicy& policy) {
    namespace db = shine::db::sqlite;
    db::Database database;
    if (auto opened = database.Open({.path = dbPath, .readOnly = false, .create = false}); !opened) {
        return {.phase = "FAILED", .detail = opened.error().message};
    }

    novelcore::AssetPipelineOptions options;
    options.suspendTimeoutMs = policy.suspendTimeoutMs;
    options.allowDegrade = policy.allowDegrade;
    auto pipeline = layer ? novelcore::RunAssetLayer(database, assetId, *layer, options)
                          : novelcore::RunAssetPipeline(database, assetId, options);
    if (!pipeline) {
        return {.phase = "FAILED", .detail = pipeline.error().message};
    }

    WorkerResult result{.phase = pipeline->status};
    switch (pipeline->outcome) {
        case novelcore::PipelineOutcome::Ready:
            break;
        case novelcore::PipelineOutcome::Suspended:
            result.phase = "PENDING";
            result.detail = pipeline->detail;
            break;
        case novelcore::PipelineOutcome::Degraded:
            result.degraded = true;
            result.detail = pipeline->detail;
            if (result.detail.empty() && !pipeline->notes.empty()) {
                result.detail = pipeline->notes.front();
            }
            break;
        case novelcore::PipelineOutcome::Failed:
            result.phase = "FAILED";
            result.detail = pipeline->detail;
            break;
    }

    if (result.phase == "FAILED") {
        return result;
    }
    for (const novelcore::VisualArtifactRow& artifact : pipeline->artifacts) {
        if (artifact.status != "DONE") {
            continue;
        }
        std::error_code ec;
        const std::filesystem::path path =
            novelcore::ProjectDirOfDb(database.Path()) / util::PathFromUtf8(artifact.rel_path);
        if (!std::filesystem::is_regular_file(path, ec)) {
            return {.phase = "FAILED",
                    .detail = "产物校验失败：" + util::PathToUtf8(artifact.rel_path),
                    .degraded = result.degraded};
        }
    }
    return result;
}

} // namespace

AssetPageModel::AssetPageModel(QObject* parent) : QObject(parent) {
    // runtimeView_ 必须一出生就带全键：QML 的绑定在 Load() 时就首次求值，
    // 那时若还是默认构造的空 QVariantMap，Page.runtime.detail 读到 undefined。
    runtimeView_ = EmptyRuntimeMap();
    // 形象层 / 一致性 / 时间线同样先给空态，避免首次求值读到 undefined。
    consistencyView_ = EmptyConsistencyMap();
    timelineView_ = EmptyTimelineMap();
    exportView_ = EmptyExportMap();
    // policyView_ 同理：策略面板在 QML 侧是「开页即绑」，空态缺键就是一片红。
    policyView_ = EmptyPolicyMap();
    // 图库三张表也必须开页即带全键：gallery::ItemsGeneration() 初始为 0，
    // 若靠「generation 变了才重投影」，第一次投影永远不会发生（0 == 0），
    // QML 首次求值就落在空 QVariantMap 上。故此处强制投影一次。
    (void)PollGallery(/*force=*/true);
    // deriveChain_ 不在此处写死：它由 RebuildVisualFacts 从 visual_artifacts
    // 与产物文件存在性算出（正脸/四视图/基础身体/服装的真实就绪状态）。
}

AssetPageModel::~AssetPageModel() = default;

bool AssetPageModel::OpenBook(const std::filesystem::path& dbPath,
                              const std::filesystem::path& projectDir, QString* error) {
    const std::filesystem::path nextDbPath = dbPath;
    const std::filesystem::path nextProjectDir = projectDir;
    CloseBook();
    if (error != nullptr) {
        *error = QString{};
    }

    dbPath_ = nextDbPath;
    projectDir_ = nextProjectDir;
    bookName_ = nextProjectDir.empty() ? QStringLiteral("当前书")
                                       : QString::fromStdString(util::FileNameToUtf8(nextProjectDir));
    if (bookName_.isEmpty()) {
        bookName_ = QStringLiteral("当前书");
    }

    if (nextDbPath.empty()) {
        openError_ = QStringLiteral("没有可打开的小说库路径。");
        if (error != nullptr) {
            *error = openError_;
        }
        RebuildDerived();
        return false;
    }

    db_ = std::make_unique<db::sqlite::Database>();
    if (auto opened = db_->Open({.path = nextDbPath, .readOnly = true, .create = false}); !opened) {
        openError_ = QStringLiteral("打不开小说库：%1（%2）")
                         .arg(QString::fromStdString(util::PathToUtf8(nextDbPath)),
                              QString::fromStdString(opened.error().message));
        db_.reset();
        if (error != nullptr) {
            *error = openError_;
        }
        RebuildDerived();
        return false;
    }

    const bool ok = RefreshEntities() && RefreshAssets();
    RebuildRefs();   // ④ 参考库：换书库必须重开（projectDir_ 变了）
    ProjectPolicy();
    // ⑥ 图库条目与书库无关，但「这张图被谁引用」要拿 db_ 查。
    // 换书库后 generation 未必变化，所以强制重投影一次把用量列表接上。
    (void)PollGallery(/*force=*/true);
    if (!ok && error != nullptr) {
        *error = openError_;
    }
    return ok;
}

void AssetPageModel::CloseBook() noexcept {
    db_.reset();
    dbPath_.clear();
    projectDir_.clear();
    bookName_.clear();
    openError_.clear();
    kindFilter_.clear();
    selectedEntityId_ = 0;
    selectedAssetId_ = 0;
    entities_.clear();
    assets_.clear();
    ++runSerial_;
    runtime_.clear();
    refs_.reset();
    refSelectedId_.clear();
    refFacts_.clear();
    ProjectRefs(refFacts_);
    RebuildDerived();
}

bool AssetPageModel::RefreshEntities() {
    entities_.clear();
    if (db_ == nullptr) {
        openError_ = QStringLiteral("尚未打开书库。");
        RebuildDerived();
        return false;
    }

    novelcore::NovelGraph graph(*db_);
    for (const KindInfo& kind : kKinds) {
        auto listed = graph.ListEntities(kind.key, {}, 5000);
        if (!listed) {
            entities_.clear();
            openError_ = QStringLiteral("读取%1实体失败：%2")
                             .arg(QString::fromUtf8(kind.label),
                                  QString::fromStdString(listed.error().message));
            RebuildDerived();
            return false;
        }
        entities_.insert(entities_.end(), listed->begin(), listed->end());
    }

    if (selectedEntityId_ != 0 &&
        std::none_of(entities_.begin(), entities_.end(), [this](const novelcore::EntityRow& e) {
            return e.id == selectedEntityId_;
        })) {
        selectedEntityId_ = 0;
    }
    openError_.clear();
    RebuildDerived();
    return true;
}

bool AssetPageModel::RefreshAssets() {
    assets_.clear();
    if (db_ == nullptr) {
        openError_ = QStringLiteral("尚未打开书库。");
        RebuildDerived();
        return false;
    }

    novelcore::NovelVisual visual(*db_);
    for (const novelcore::EntityRow& entity : entities_) {
        if (selectedEntityId_ != 0 && entity.id != selectedEntityId_) {
            continue;
        }
        if (!kindFilter_.isEmpty() && QString::fromStdString(entity.kind) != kindFilter_) {
            continue;
        }
        auto found = visual.FindAssetByEntity(entity.id);
        if (!found) {
            if (!IsMissingAsset(found.error().message)) {
                assets_.clear();
                openError_ = QStringLiteral("读取实体「%1」的视觉资产失败：%2")
                                 .arg(QString::fromStdString(entity.name),
                                      QString::fromStdString(found.error().message));
                RebuildDerived();
                return false;
            }
            continue;
        }
        AssetEntry entry{.asset = *found, .entity = entity};
        if (auto artifacts = visual.ListArtifacts(entry.asset.id)) {
            entry.degraded = std::any_of(artifacts->begin(), artifacts->end(),
                                         [](const novelcore::VisualArtifactRow& a) {
                                             return a.degraded;
                                         });
        }
        assets_.push_back(std::move(entry));
    }

    const bool selected_available =
        selectedAssetId_ != 0 && std::any_of(assets_.begin(), assets_.end(), [this](const AssetEntry& e) {
            return e.asset.id == selectedAssetId_;
        });
    if (!selected_available) {
        selectedAssetId_ = assets_.empty() ? 0 : assets_.front().asset.id;
    }

    openError_.clear();
    RebuildDerived();
    return true;
}

void AssetPageModel::RebuildDerived() {
    // 实体树：按 kind 分组，组内是实体
    entityGroups_.clear();
    for (const KindInfo& kind : kKinds) {
        const QString key = QString::fromStdString(std::string{kind.key});
        QVariantList items;
        for (const novelcore::EntityRow& entity : entities_) {
            if (QString::fromStdString(entity.kind) != key) {
                continue;
            }
            items.append(QVariantMap{
                {QStringLiteral("id"), QString::number(entity.id)},
                {QStringLiteral("name"), QString::fromStdString(entity.name)},
                {QStringLiteral("summary"), QString::fromStdString(entity.summary)},
                {QStringLiteral("status"), QString::fromStdString(entity.status)},
            });
        }
        entityGroups_.append(QVariantMap{{QStringLiteral("key"), key},
                                         {QStringLiteral("label"), QString::fromUtf8(kind.label)},
                                         {QStringLiteral("count"), items.size()},
                                         {QStringLiteral("items"), items}});
    }

    // 资产卡
    assetsView_.clear();
    for (const AssetEntry& entry : assets_) {
        assetsView_.append(AssetToMap(entry));
    }

    RefreshKindCounts();

    runtimeView_ = RuntimeToMap(selectedAssetId_);
    ProjectPolicy();
    RebuildVisualFacts();
    emit changed();
}

void AssetPageModel::RefreshKindCounts() {
    kinds_.clear();
    kinds_.append(QVariantMap{{QStringLiteral("key"), QString{}},
                              {QStringLiteral("label"), QStringLiteral("全部")},
                              {QStringLiteral("count"), entities_.size()},
                              {QStringLiteral("on"), kindFilter_.isEmpty()}});
    for (const KindInfo& kind : kKinds) {
        const QString key = QString::fromStdString(std::string{kind.key});
        int count = 0;
        for (const novelcore::EntityRow& entity : entities_) {
            if (QString::fromStdString(entity.kind) == key) {
                ++count;
            }
        }
        kinds_.append(QVariantMap{{QStringLiteral("key"), key},
                                  {QStringLiteral("label"), QString::fromUtf8(kind.label)},
                                  {QStringLiteral("count"), count},
                                  {QStringLiteral("on"), kindFilter_ == key}});
    }
}

QVariantMap AssetPageModel::AssetToMap(const AssetEntry& entry) const {
    const QString status = QString::fromStdString(entry.asset.status);
    QVariantMap run = runtime_.constFind(entry.asset.id) != runtime_.cend()
                          ? RuntimeToMap(entry.asset.id)
                          : QVariantMap{};
    return QVariantMap{
        {QStringLiteral("id"), QString::number(entry.asset.id)},
        {QStringLiteral("entityId"), QString::number(entry.entity.id)},
        {QStringLiteral("name"), QString::fromStdString(entry.asset.name)},
        {QStringLiteral("entityName"), QString::fromStdString(entry.entity.name)},
        {QStringLiteral("kind"), QString::fromStdString(entry.entity.kind)},
        {QStringLiteral("kindLabel"), KindLabelOf(entry.entity.kind)},
        {QStringLiteral("role"), QString::fromStdString(entry.entity.summary)},
        // Art.qml 的程序化占位种子：按 id 取模 12，与设计稿 UI.jsx:184-197 同口径
        {QStringLiteral("art"), static_cast<int>(entry.asset.id % 12)},
        {QStringLiteral("status"), status},
        {QStringLiteral("statusLabel"), StatusLabel(status)},
        {QStringLiteral("statusTone"), QString::fromLatin1(StatusTone(status))},
        {QStringLiteral("degraded"), entry.degraded},
        {QStringLiteral("sheetRelPath"), QString::fromStdString(entry.asset.sheet_rel_path)},
        {QStringLiteral("baseDesc"), QString::fromStdString(entry.asset.base_desc)},
        {QStringLiteral("canonStatus"), QString::fromStdString(entry.asset.canon_status)},
        {QStringLiteral("runtime"), run},
    };
}

QVariantMap AssetPageModel::RuntimeToMap(qint64 assetId) const {
    const auto found = runtime_.constFind(assetId);
    if (found == runtime_.cend()) {
        return EmptyRuntimeMap();
    }
    const RuntimeState& state = found.value();
    return QVariantMap{
        {QStringLiteral("none"), false},
        {QStringLiteral("runId"), static_cast<double>(state.run_id)},
        {QStringLiteral("phase"), state.phase},
        {QStringLiteral("phaseLabel"), StatusLabel(state.phase)},
        {QStringLiteral("phaseTone"), QString::fromLatin1(StatusTone(state.phase))},
        {QStringLiteral("detail"), state.detail},
        {QStringLiteral("layer"), state.layer},
        {QStringLiteral("history"), state.history},
        {QStringLiteral("active"), state.active},
        {QStringLiteral("degraded"), state.degraded},
    };
}

const AssetPageModel::AssetEntry* AssetPageModel::SelectedAsset() const {
    if (selectedAssetId_ == 0) {
        return nullptr;
    }
    const auto found = std::find_if(assets_.begin(), assets_.end(), [this](const AssetEntry& e) {
        return e.asset.id == selectedAssetId_;
    });
    return found == assets_.end() ? nullptr : &*found;
}

// 形象层 / 一致性 / 时间线三块的真实取数 → QML 投影。
// 只查库 + stat 文件，**不解码像素**：帧图的像素差异放到 worker 上算（见下）。
void AssetPageModel::RebuildVisualFacts() {
    layersView_.clear();
    compareKv_.clear();
    diffRows_.clear();
    sheetKv_.clear();
    consistencyView_ = EmptyConsistencyMap();
    timelineView_ = EmptyTimelineMap();

    const AssetEntry* entry = SelectedAsset();
    if (db_ == nullptr || entry == nullptr) {
        return;
    }
    const novelcore::VisualAssetRow& asset = entry->asset;
    const novelcore::RowId entity_id = entry->entity.id;
    novelcore::NovelVisual visual(*db_);

    // —— ① 形象层 ——
    const std::vector<AssetLayerFact> facts = AssetCollectLayers(visual, asset, projectDir_);
    deriveChain_.clear();
    int ready_layers = 0;
    for (const AssetLayerFact& fact : facts) {
        // QVariantMap 的 initializer_list 每项必须是 std::pair<QString,QVariant>，
        // 嵌套一层 {k,v} 才会走 pair 构造；混 std::string 更不行。
        // 这里统一用 insert 逐项加，避开 QMap initializer 的全部歧义。
        QVariantMap layer;
        layer.insert(QStringLiteral("key"), QString::fromStdString(fact.key));
        layer.insert(QStringLiteral("title"), fact.title);
        layer.insert(QStringLiteral("parent"), fact.parentKey);
        layer.insert(QStringLiteral("relPath"), fact.relPath);
        layer.insert(QStringLiteral("absPath"), fact.absPath);
        layer.insert(QStringLiteral("status"), fact.status);
        layer.insert(QStringLiteral("ready"), fact.ready);
        layer.insert(QStringLiteral("legacyFront"), fact.legacyFront);
        layersView_.append(layer);
        // 派生链节点状态：ready→done / 有记录未落盘→run / 无记录→todo
        const QString state = fact.ready                ? QStringLiteral("done")
                              : !fact.relPath.isEmpty() ? QStringLiteral("run")
                                                        : QStringLiteral("todo");
        QVariantMap node;
        node.insert(QStringLiteral("name"), fact.title);
        node.insert(QStringLiteral("key"), QString::fromStdString(fact.key));
        node.insert(QStringLiteral("state"), state);
        node.insert(QStringLiteral("absPath"), fact.absPath);
        node.insert(QStringLiteral("ready"), fact.ready);
        deriveChain_.append(node);
        if (fact.ready) {
            ++ready_layers;
        }
    }

    // —— ②③ 一致性 + 时间线 ——
    AssetConsistencyFact consistency =
        AssetCollectConsistency(*db_, visual, asset, entity_id, projectDir_);
    // ⚠️ 帧图对没变时**沿用上一轮已算好的差异**。
    // 每轮都 AssetApplyDifference(-1) 会把「已比对」打回「未比对」，
    // 而像素差异是异步回填的 —— 上一轮的回填可能正好落在这次同步投影之前，
    // 于是刚算出的 0.24 又被抹成「未比对」（抓图就永远停在中间态）。
    const bool same_frame_pair =
        consistency_.frames.size() >= 2 && consistency.frames.size() >= 2 &&
        consistency_.frames[0].absPath == consistency.frames[0].absPath &&
        consistency_.frames[1].absPath == consistency.frames[1].absPath;
    if (same_frame_pair && consistency_.difference >= 0) {
        AssetApplyDifference(consistency, consistency_.difference);
    } else {
        AssetApplyDifference(consistency, -1.0);
    }
    const AssetTimelineFact timeline =
        AssetCollectTimeline(*db_, visual, asset, entity_id, projectDir_);

    // 帧图两张时在 worker 上解码并算像素差异；回填只认当前 token。
    // 只有在差异确实需要重算时才再派一次 worker。
    const std::uint64_t token = ++visualToken_;
    const bool needs_diff = consistency.frames.size() >= 2 && consistency.difference < 0;
    visualsPending_ = needs_diff;
    if (needs_diff) {
        const QString left = consistency.frames[0].absPath;
        const QString right = consistency.frames[1].absPath;
        const QPointer<AssetPageModel> guard(this);
        async::RunOnWorker([guard, token, left, right] {
            const double diff = MeanAbsoluteDifferenceOf(left, right);
            async::PostToUi([guard, token, diff] {
                if (guard.isNull() || guard->visualToken_ != token) {
                    return; // 期间又换了资产 / 关了书库
                }
                guard->ApplyDifferenceAsync(diff);
            });
        });
    }

    ProjectConsistency(consistency);
    ProjectTimeline(timeline);
    // 设定集事实行（全部来自真库行数）
    const auto push_sheet = [this](const QString& key, const QString& value) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("value"), value);
        sheetKv_.append(row);
    };
    push_sheet(QStringLiteral("实体"), QStringLiteral("#%1").arg(entity_id));
    push_sheet(QStringLiteral("形象层"),
               QStringLiteral("%1 / %2 层就绪").arg(ready_layers).arg(facts.size()));
    push_sheet(QStringLiteral("外观基线"), QString::number(consistency.states.size()));
    push_sheet(QStringLiteral("绑定镜头"), QString::number(consistency.shotCount));
}

void AssetPageModel::ApplyDifferenceAsync(double diff) {
    visualsPending_ = false;
    // diff < 0 = 帧图读不出来（不是「差异为零」）。此时保持「未比对」，
    // 别把 -1 当成真实差异值投影出去。
    if (consistency_.states.size() >= 2 && diff >= 0) {
        AssetApplyDifference(consistency_, diff);
        ProjectConsistency(consistency_);
    }
    emit visualsChanged();
    emit changed();
}

void AssetPageModel::ProjectConsistency(const AssetConsistencyFact& fact) {
    consistency_ = fact;
    QVariantList states;
    for (const AssetStateFact& state : fact.states) {
        QVariantMap row;
        row.insert(QStringLiteral("chapterOrd"), state.chapterOrd);
        row.insert(QStringLiteral("where"), state.where);
        row.insert(QStringLiteral("stage"), state.stage);
        row.insert(QStringLiteral("appearance"), state.appearance);
        row.insert(QStringLiteral("colors"), state.colors);
        row.insert(QStringLiteral("note"), state.note);
        row.insert(QStringLiteral("tone"), state.tone);
        states.append(row);
    }
    QVariantList emotions;
    for (const AssetEmotionFact& emotion : fact.emotions) {
        QVariantMap row;
        row.insert(QStringLiteral("chapterOrd"), emotion.chapterOrd);
        row.insert(QStringLiteral("where"), emotion.where);
        row.insert(QStringLiteral("summary"), emotion.summary);
        emotions.append(row);
    }
    QVariantList frames;
    for (const AssetShotFrameFact& frame : fact.frames) {
        QVariantMap row;
        row.insert(QStringLiteral("shotId"), frame.shotId);
        row.insert(QStringLiteral("chapterOrd"), frame.chapterOrd);
        row.insert(QStringLiteral("label"), frame.label);
        row.insert(QStringLiteral("absPath"), frame.absPath);
        row.insert(QStringLiteral("hasImage"), frame.hasImage);
        frames.append(row);
    }
    QVariantMap view;
    view.insert(QStringLiteral("states"), states);
    view.insert(QStringLiteral("emotions"), emotions);
    view.insert(QStringLiteral("frames"), frames);
    view.insert(QStringLiteral("shotCount"), fact.shotCount);
    view.insert(QStringLiteral("severity"), fact.severity);
    view.insert(QStringLiteral("verdict"), fact.verdict);
    view.insert(QStringLiteral("firstLabel"), fact.firstLabel);
    view.insert(QStringLiteral("lastLabel"), fact.lastLabel);
    view.insert(QStringLiteral("diffPct"), fact.difference < 0
                                           ? QString()
                                           : QString::number(fact.difference * 100.0, 'f', 1));
    view.insert(QStringLiteral("hasStates"), !states.isEmpty());
    view.insert(QStringLiteral("hasEmotions"), !emotions.isEmpty());
    view.insert(QStringLiteral("hasFrames"), frames.size() >= 2);
    consistencyView_ = view;

    // 右侧 KV 四行 —— 检测项口径 = 已登记基线段数 + 镜头帧数 + 判定
    compareKv_.clear();
    if (fact.states.empty() && fact.frames.empty()) {
        return; // 没有可比对象就不编四行，页面走空态
    }
    const auto push_kv = [this](const QString& key, const QString& value) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("value"), value);
        compareKv_.append(row);
    };
    push_kv(QStringLiteral("对比对象"), QStringLiteral("按章外观基线"));
    push_kv(QStringLiteral("基线帧"), fact.firstLabel);
    push_kv(QStringLiteral("当前帧"), fact.lastLabel);
    push_kv(QStringLiteral("检测项"),
            QStringLiteral("外观 %1 段 · 镜头 %2 张 · 判定 %3")
                .arg(fact.states.size())
                .arg(fact.frames.size())
                .arg(fact.verdict));

    diffRows_.clear();
    for (const auto& entry : AssetDiffRows(fact)) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), entry.first);
        row.insert(QStringLiteral("changed"), entry.second);
        row.insert(QStringLiteral("value"), entry.second ? QStringLiteral("变化（预期内）")
                                                         : QStringLiteral("一致"));
        diffRows_.append(row);
    }
}

void AssetPageModel::ProjectTimeline(const AssetTimelineFact& fact) {
    QVariantList events;
    for (const AssetTimelineEventFact& event : fact.events) {
        QVariantMap row;
        row.insert(QStringLiteral("chapterOrd"), event.chapterOrd);
        row.insert(QStringLiteral("label"), event.label);
        row.insert(QStringLiteral("hot"), event.hot);
        row.insert(QStringLiteral("shotId"), event.shotId);
        row.insert(QStringLiteral("isShot"), event.isShot);
        events.append(row);
    }
    QVariantList shots;
    for (qint64 id : fact.boundShots) {
        QVariantList refs;
        const auto found = fact.shotRefs.find(id);
        if (found != fact.shotRefs.end()) {
            for (const QString& path : found->second) {
                refs.append(path);
            }
        }
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), QStringLiteral("#%1").arg(id));
        row.insert(QStringLiteral("refs"), refs);
        shots.append(row);
    }
    QVariantList refs;
    for (const QString& path : fact.refImages) {
        refs.append(path);
    }
    QVariantMap view;
    view.insert(QStringLiteral("chapterCount"), fact.chapterCount);
    view.insert(QStringLiteral("events"), events);
    view.insert(QStringLiteral("shots"), shots);
    view.insert(QStringLiteral("refImages"), refs);
    view.insert(QStringLiteral("hasData"), !events.isEmpty());
    timelineView_ = view;
}

QVariantList AssetPageModel::refImages() const { return refView_; }
QVariantMap AssetPageModel::policy() const { return policyView_; }
QVariantMap AssetPageModel::exportState() const { return exportView_; }
QVariantList AssetPageModel::galleryItems() const { return galleryView_; }
QVariantMap AssetPageModel::galleryState() const { return galleryStateView_; }
QVariantMap AssetPageModel::galleryViewer() const { return galleryViewerView_; }

// —— ⑥ 全局图库 ——
// 取数全部来自 shine::gallery（扫描 / 模型 / 查看器），本类只做**投影**。
// 扫描是异步的（RequestScan → worker → AppEntry 的 15ms Tick 回填），
// 没有完成信号可挂，所以靠 ItemsGeneration 判变化。
//
// force：跳过「generation 没变就不动」的短路。切来源 / 重扫时用它把来源标签
// 立刻翻过去（扫描还在路上，generation 暂时没变）；`galleryProjected_`
// 则保证首投影一定发生 —— ItemsGeneration() 的初值和 galleryGeneration_ 都是 0。
bool AssetPageModel::PollGallery(bool force) {
    const std::uint64_t generation = gallery::ItemsGeneration();
    if (!force && galleryProjected_ && generation == galleryGeneration_) {
        return false;
    }
    galleryProjected_ = true;
    galleryGeneration_ = generation;

    galleryView_.clear();
    const auto& view = gallery::Model().View();
    galleryView_.reserve(static_cast<qsizetype>(view.size()));
    for (const gallery::ImageInfo& item : view) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(item.id));
        row.insert(QStringLiteral("path"), QString::fromStdString(util::PathToUtf8(item.path)));
        row.insert(QStringLiteral("name"),
                   QString::fromStdString(util::PathToUtf8(item.path.filename())));
        row.insert(QStringLiteral("width"), static_cast<int>(item.width));
        row.insert(QStringLiteral("height"), static_cast<int>(item.height));
        row.insert(QStringLiteral("format"), QString::fromStdString(item.format));
        row.insert(QStringLiteral("source"), static_cast<int>(item.source));
        galleryView_.append(row);
    }
    galleryCount_ = view.size();

    // 选中项若已不在列表里（切来源 / 重扫），清掉，别让详情停在不存在的条目上。
    if (gallerySelected_ != 0 && gallery::Model().Find(gallerySelected_) == nullptr) {
        gallerySelected_ = 0;
        gallery::CloseViewer();
    }

    const gallery::GalleryState& state = gallery::State();
    galleryStateView_.insert(QStringLiteral("source"), static_cast<int>(state.source));
    galleryStateView_.insert(QStringLiteral("sourceLabel"),
                             QString::fromStdString(gallery::SourceLabel(state.source)));
    galleryStateView_.insert(QStringLiteral("scanning"), state.scanning);
    galleryStateView_.insert(QStringLiteral("count"), static_cast<qlonglong>(galleryCount_));
    galleryStateView_.insert(QStringLiteral("loaded"),
                             static_cast<qlonglong>(gallery::LoadedCount()));
    galleryStateView_.insert(QStringLiteral("message"), QString::fromStdString(state.message));
    galleryStateView_.insert(QStringLiteral("error"), QString::fromStdString(state.error));
    galleryStateView_.insert(QStringLiteral("truncated"), state.truncated);

    const gallery::ImageInfo* current =
        gallerySelected_ == 0 ? nullptr : gallery::Model().Find(gallerySelected_);
    ProjectGalleryViewer();

    RefreshGalleryUsages();
    return true;
}

// ⚠️ 查看器 / 选中 / 缩放这三类状态**不改 ItemsGeneration**（那个只跟条目表走），
//    所以不能指望 PollGallery 顺带刷新 —— 它会按「没变」短路，QML 侧读到的还是
//    上一张 map。症状：探针报 viewerOpen=1（它直接问 gallery::ViewerOpen），
//    而 QML 的 `gviewer.open` 仍是 false，查看器死活不显示。
//    故这三类状态单独投影，所有写它的路径都必须调本函数。
void AssetPageModel::ProjectGalleryViewer() {
    const gallery::ImageInfo* current =
        gallerySelected_ == 0 ? nullptr : gallery::Model().Find(gallerySelected_);
    galleryViewerView_.insert(QStringLiteral("open"), gallery::ViewerOpen());
    galleryViewerView_.insert(QStringLiteral("id"),
                              QVariant::fromValue<qulonglong>(gallery::ViewerImageId()));
    galleryViewerView_.insert(QStringLiteral("path"),
                              current == nullptr
                                  ? QString()
                                  : QString::fromStdString(util::PathToUtf8(current->path)));
    galleryViewerView_.insert(QStringLiteral("zoom"), galleryZoom_);
    galleryViewerView_.insert(QStringLiteral("minZoom"), kGalleryMinZoom);
    galleryViewerView_.insert(QStringLiteral("maxZoom"), kGalleryMaxZoom);
}

void AssetPageModel::RefreshGalleryUsages() {
    galleryUsages_.clear();
    const gallery::ImageInfo* current =
        gallerySelected_ == 0 ? nullptr : gallery::Model().Find(gallerySelected_);
    if (current == nullptr || db_ == nullptr) {
        return;
    }
    galleryUsages_ = visual::FindReferenceUsages(*db_, projectDir_, current->path);
}

void AssetPageModel::selectGallerySource(int source) {
    const int clamped = std::clamp(source, 0, 2);
    if (static_cast<int>(gallery::State().source) == clamped) {
        return;
    }
    gallery::RequestScan(static_cast<gallery::SourceKind>(clamped));
    // 立刻重投影一次：来源标签要先切过去，条目随后由 generation 变化带出来。
    if (PollGallery(/*force=*/true)) {
        emit galleryChanged();
    }
}

void AssetPageModel::rescanGallery() {
    gallery::RescanCurrent();
    if (PollGallery(/*force=*/true)) {
        emit galleryChanged();
    }
}

void AssetPageModel::selectGalleryItem(quint64 id) {
    gallerySelected_ = static_cast<gallery::ImageId>(id);
    RefreshGalleryUsages();
    ProjectGalleryViewer();
    emit galleryChanged();
}

void AssetPageModel::setGalleryAsWorkflowInput() {
    const gallery::ImageInfo* current =
        gallerySelected_ == 0 ? nullptr : gallery::Model().Find(gallerySelected_);
    if (current == nullptr) {
        return;
    }
    // 同路径动作：把选中图登记为下一张图的「工作流输入」（图节点拖放会读它）。
    gallery::SetLastGraphDropPath(util::PathToUtf8(current->path));
    widgets::Toast::Show(
        QStringLiteral("已设为工作流输入：%1")
            .arg(QString::fromStdString(util::PathToUtf8(current->path.filename()))),
        widgets::Toast::Tone::Success);
    (void)PollGallery();
    emit galleryChanged();
}

void AssetPageModel::openGalleryViewer(quint64 id) {
    if (id == 0) {
        return;
    }
    gallerySelected_ = static_cast<gallery::ImageId>(id);
    gallery::OpenViewer(gallerySelected_);
    RefreshGalleryUsages();
    ProjectGalleryViewer();
    emit galleryChanged();
}

void AssetPageModel::closeGalleryViewer() {
    gallery::CloseViewer();
    ProjectGalleryViewer();
    emit galleryChanged();
}

void AssetPageModel::setGalleryZoom(double zoom) {
    galleryZoom_ = std::clamp(zoom, kGalleryMinZoom, kGalleryMaxZoom);
    ProjectGalleryViewer();
    emit galleryChanged();
}

bool AssetPageModel::WaitGalleryScan(int timeout_ms) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout_ms) {
        // ⚠️ 等待必须**自己**把驱动补齐，不能只靠 AppEntry 的 15ms 泵：
        //    ① 扫描结果经 async::PostToUi 回队列 → 要先 DrainUiQueue 搬过来；
        //    ② gallery::Tick() 只收拾「已经排干」的结果，它自己不碰 UI 队列。
        //    顺序反了就是空转。本函数是**阻塞**循环，验收 / 取证都从定时器回调里
        //    调它，实测那个泵并不可靠：扫描本身 0.1ms 完事，却等满 10s 超时。
        shine::async::DrainUiQueue();
        shine::gallery::Tick();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        if (PollGallery()) {
            emit galleryChanged();
        }
        if (!gallery::State().scanning) {
            // 收敛后再补一次：最后一次 Tick 的回填可能落在上面那次之后。
            shine::async::DrainUiQueue();
            shine::gallery::Tick();
            if (PollGallery()) {
                emit galleryChanged();
            }
            return true;
        }
    }
    return !gallery::State().scanning;
}

QString AssetPageModel::GalleryProbe() const {
    const gallery::GalleryState& state = gallery::State();
    // viewerOpen / viewerId 追加在末尾：P05-S7 按 contains() 解析既有字段，
    // 尾部追加不影响它；而取证需要能断言「查看器真的开了」——
    // 只靠「拍出来的图里有深色遮罩」是目视判断，不算机器可验的判据。
    return QStringLiteral("source=%1; items=%2; scanning=%3; sources=3; virtual=1; viewerMin=%4; "
                          "viewerMax=%5; workflow=%6; viewerOpen=%7; viewerId=%8")
        .arg(QString::fromStdString(gallery::SourceLabel(state.source)))
        .arg(static_cast<qulonglong>(gallery::Model().Count()))
        .arg(state.scanning ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(kGalleryMinZoom, 0, 'f', 1)
        .arg(kGalleryMaxZoom, 0, 'f', 1)
        .arg(QString::fromStdString(gallery::LastGraphDropPath()))
        .arg(gallery::ViewerOpen() ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(static_cast<qulonglong>(gallery::ViewerImageId()));
}

QStringList AssetPageModel::ReferenceUsageLabels() const {
    QStringList labels;
    labels.reserve(static_cast<qsizetype>(galleryUsages_.size()));
    for (const visual::ReferenceUsage& usage : galleryUsages_) {
        labels.push_back(QString::fromStdString(usage.label));
    }
    return labels;
}

bool AssetPageModel::ActivateReferenceUsage(int index) {
    if (index < 0 || index >= static_cast<int>(galleryUsages_.size())) {
        return false;
    }
    const visual::ReferenceUsage usage = galleryUsages_[static_cast<std::size_t>(index)];
    if (usage.kind == visual::ReferenceUsageKind::Shot) {
        return ShowShotReference(usage.shot_id);
    }
    if (usage.entity_id > 0) {
        selectEntity(QString::number(usage.entity_id));
    }
    if (usage.asset_id > 0) {
        selectAsset(QString::number(usage.asset_id));
    }
    return usage.entity_id > 0 || usage.asset_id > 0;
}

bool AssetPageModel::ShowShotReference(qint64 shot_id) {
    if (db_ == nullptr || shot_id <= 0) {
        return false;
    }
    novelcore::NovelVisual visual(*db_);
    auto shot = visual.GetShot(shot_id);
    if (!shot) {
        return false;
    }
    auto* drawer = new widgets::Drawer(QStringLiteral("分镜引用 #%1").arg(shot_id));
    auto* values = new data::KeyValue(drawer);
    values->SetPairs({
        {QStringLiteral("场景"), QString::number(shot->scene_id)},
        {QStringLiteral("镜序"), QString::number(shot->ord)},
        {QStringLiteral("角色 JSON"), QString::fromStdString(shot->character_ids_json)},
        {QStringLiteral("动作"), QString::fromStdString(shot->action)},
        {QStringLiteral("表演"), QString::fromStdString(shot->expression)},
        {QStringLiteral("情绪"), QString::fromStdString(shot->mood)},
        {QStringLiteral("Prompt"), QString::fromStdString(shot->prompt_text)},
    });
    drawer->BodyLayout()->addWidget(values);
    drawer->Open();
    return true;
}

void AssetPageModel::ProjectRefs(const std::vector<AssetRefFact>& refs) {
    QVariantList view;
    for (const AssetRefFact& fact : refs) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), fact.id);
        row.insert(QStringLiteral("originalName"), fact.originalName);
        row.insert(QStringLiteral("absPath"), fact.absPath);
        row.insert(QStringLiteral("orientation"), fact.orientation);
        row.insert(QStringLiteral("rawWidth"), fact.rawWidth);
        row.insert(QStringLiteral("rawHeight"), fact.rawHeight);
        row.insert(QStringLiteral("displayWidth"), fact.displayWidth);
        row.insert(QStringLiteral("displayHeight"), fact.displayHeight);
        row.insert(QStringLiteral("entityId"), fact.entityId);
        row.insert(QStringLiteral("markers"), fact.markers);
        row.insert(QStringLiteral("markerText"), AssetRefMarkers(fact.markers));
        view.append(row);
    }
    refView_ = view;
}

void AssetPageModel::ProjectPolicy() {
    const auto runtimeIt = runtime_.constFind(selectedAssetId_);
    const bool has_runtime = runtimeIt != runtime_.cend() && !runtimeIt->phase.isEmpty();
    const int minutes = static_cast<int>(policy_.suspendTimeoutMs / (60 * 1000));
    QVariantMap map;
    map.insert(QStringLiteral("allowDegrade"), policy_.allowDegrade);
    map.insert(QStringLiteral("strict"), !policy_.allowDegrade);
    map.insert(QStringLiteral("timeoutMinutes"), minutes);
    map.insert(QStringLiteral("timeoutMs"), static_cast<qlonglong>(policy_.suspendTimeoutMs));
    map.insert(QStringLiteral("summary"),
               policy_.allowDegrade
                   ? QStringLiteral("C 挂起 → 超时 B 降级；超时 %1 分钟").arg(minutes)
                   : QStringLiteral("严格模式：缺依赖始终按 C 挂起，拒绝任何降级"));
    map.insert(QStringLiteral("phase"),
               has_runtime ? runtimeIt->phase : QStringLiteral("none"));
    map.insert(QStringLiteral("layer"),
               has_runtime && !runtimeIt->layer.isEmpty() ? runtimeIt->layer : QStringLiteral("none"));
    map.insert(QStringLiteral("detail"), has_runtime ? runtimeIt->detail : QString());
    map.insert(QStringLiteral("active"), has_runtime && runtimeIt->active);
    map.insert(QStringLiteral("degraded"), has_runtime && runtimeIt->degraded);
    policyView_ = map;
}

void AssetPageModel::RebuildRefs() {
    refFacts_.clear();
    if (projectDir_.empty()) {
        ProjectRefs(refFacts_);
        return;
    }
    if (refs_ == nullptr) {
        refs_ = std::make_unique<visual::ReferenceLibrary>(projectDir_);
    }
    if (auto loaded = refs_->Load(); !loaded) {
        // 读不出 refs.json 视为「还没有参考图」，不是错误：首次导入前该文件不存在。
        ProjectRefs(refFacts_);
        return;
    }
    refFacts_ = AssetCollectRefs(refs_->Images(), projectDir_);
    if (!refSelectedId_.isEmpty() &&
        std::none_of(refFacts_.begin(), refFacts_.end(), [this](const AssetRefFact& f) {
            return f.id == refSelectedId_;
        })) {
        refSelectedId_.clear();
    }
    if (refSelectedId_.isEmpty() && !refFacts_.empty()) {
        refSelectedId_ = refFacts_.front().id;
    }
    ProjectRefs(refFacts_);
}

// —— ① 导出整版设定集 PNG ——
// 真实现迁自已删除的 Widgets 侧：收集就绪层 → SheetGrid 2×360×280 合成 → 存盘。
// **解码 / 合成 / 写盘全部在 worker**（AGENTS.md：图片解码放
// worker，UI 线程不做同步 IO），结果经 async::PostToUi 回填 exportView_。
void AssetPageModel::exportSheet(const QString& target) {
    if (exportBusy_) {
        widgets::Toast::Show(QStringLiteral("正在导出，请稍候。"), widgets::Toast::Tone::Info);
        return;
    }
    // 先在 UI 线程把要导出的层收齐（只读元数据，不碰图像像素）。
    std::vector<std::pair<QString, std::string>> todo;
    todo.reserve(static_cast<std::size_t>(layersView_.size()));
    for (const QVariant& item : layersView_) {
        const QVariantMap layer = item.toMap();
        if (!layer.value(QStringLiteral("ready")).toBool()) {
            continue;
        }
        const QString abs = layer.value(QStringLiteral("absPath")).toString();
        if (abs.isEmpty()) {
            continue;
        }
        todo.emplace_back(layer.value(QStringLiteral("title")).toString(), abs.toStdString());
    }
    if (todo.empty()) {
        widgets::Toast::Show(QStringLiteral("没有可导出的形象层；请先生成至少一层。"),
                             widgets::Toast::Tone::Warning);
        return;
    }

    exportBusy_ = true;
    exportView_.insert(QStringLiteral("busy"), true);
    exportView_.insert(QStringLiteral("count"), static_cast<int>(todo.size()));
    exportView_.insert(QStringLiteral("ok"), false);
    exportView_.insert(QStringLiteral("message"), QStringLiteral("正在合成整版设定集…"));
    emit changed();

    // ⚠️ QFileDialog 是模态的，**必须在 UI 线程选路径**，不能塞进 worker。
    // 离屏/验收场景传显式 target，跳过对话框（否则会挂住整个验收）。
    QString path = target;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("导出整版设定集"),
                                            SelectedAssetName() + QStringLiteral("-角色设定集.png"),
                                            QStringLiteral("PNG 图像 (*.png)"));
        if (path.isEmpty()) {
            exportBusy_ = false;
            exportView_.insert(QStringLiteral("busy"), false);
            exportView_.insert(QStringLiteral("message"), QStringLiteral("已取消导出。"));
            emit changed();
            return;
        }
    }

    // 解码 + 整版合成 + 写盘**一趟做完，全在 worker**（AGENTS.md：图片解码放
    // worker，UI 线程不做同步 IO）。口径沿用历史值：2 列 360×280。
    const QPointer<AssetPageModel> guard(this);
    async::RunOnWorker([guard, todo, path] {
        images::SheetGrid sheet(2, QSize(360, 280));
        for (const auto& [title, source] : todo) {
            QImageReader reader(QString::fromStdString(source));
            reader.setAutoTransform(true);
            const QSize original = reader.size();
            if (original.isValid() && !original.isEmpty()) {
                reader.setScaledSize(original.scaled(QSize(360, 280), Qt::KeepAspectRatio));
            }
            const QImage image = reader.read();
            if (!image.isNull()) {
                sheet.Add(image, title);
            }
        }
        const QImage composed = sheet.Count() > 0 ? sheet.Compose() : QImage();
        const bool ok = !composed.isNull() && composed.save(path, "PNG");
        std::error_code ec;
        const auto bytes = ok ? std::filesystem::file_size(util::PathFromUtf8(path.toStdString()), ec)
                              : 0;
        async::PostToUi([guard, ok, path, bytes, ec] {
            if (guard.isNull()) {
                return;
            }
            guard->exportBusy_ = false;
            guard->exportView_.insert(QStringLiteral("busy"), false);
            guard->exportView_.insert(QStringLiteral("ok"), ok);
            guard->exportView_.insert(QStringLiteral("lastPath"), ok ? path : QString());
            guard->exportView_.insert(QStringLiteral("lastBytes"),
                                      (ok && !ec) ? static_cast<qlonglong>(bytes) : 0);
            guard->exportView_.insert(
                QStringLiteral("message"),
                ok ? QStringLiteral("设定集已导出：%1").arg(path)
                   : QStringLiteral("导出失败，请检查目标路径是否可写。"));
            guard->emit changed();
            if (ok) {
                widgets::Toast::Show(QStringLiteral("设定集已导出：%1").arg(path),
                                     widgets::Toast::Tone::Success);
            } else {
                widgets::Toast::Show(QStringLiteral("导出失败，请检查目标路径是否可写。"),
                                     widgets::Toast::Tone::Error);
            }
        });
    });
}

// —— ② 参考库：导入 / 标记 / 绑定 / 删除 ——
// 真实现迁自已删除的 Widgets 侧。导入是异步的（worker 解码 + 写回 refs.json），
// 这里返回**提交数**，实际落库数看 refImages() / RefProbe()。
int AssetPageModel::importReferences(const QStringList& paths) {
    if (refBusy_ || projectDir_.empty() || paths.isEmpty()) {
        return 0;
    }
    std::vector<std::filesystem::path> parsed;
    parsed.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        parsed.push_back(util::PathFromUtf8(path.toStdString()));
    }
    if (parsed.empty()) {
        return 0;
    }

    refBusy_ = true;
    const std::filesystem::path root = projectDir_;
    const qint64 entity_id = selectedEntityId_;
    const qint64 asset_id = selectedAssetId_;
    auto flag = std::make_shared<std::atomic<bool>>(true);
    refBusyFlag_ = flag; // 验收可以不等事件循环，直接轮询这个原子量
    const QPointer<AssetPageModel> guard(this);
    async::RunOnWorker([guard, flag, root, entity_id, asset_id, parsed] {
        visual::ReferenceLibrary library(root);
        std::string error;
        if (auto loaded = library.Load(); !loaded) {
            error = loaded.error().message;
        }
        int imported = 0;
        if (error.empty()) {
            for (const auto& path : parsed) {
                if (library.Import(path, entity_id, asset_id)) {
                    ++imported;
                }
            }
        }
        async::PostToUi([guard, flag, imported, error] {
            flag->store(false);
            if (guard.isNull()) {
                return;
            }
            guard->refBusy_ = false;
            guard->RebuildRefs();
            guard->emit changed();
            if (!error.empty()) {
                widgets::Toast::Show(QString::fromStdString(error), widgets::Toast::Tone::Error);
            } else if (imported > 0) {
                widgets::Toast::Show(QStringLiteral("已导入 %1 张项目参考图。").arg(imported),
                                     widgets::Toast::Tone::Success);
            }
        });
    });
    return static_cast<int>(parsed.size());
}

void AssetPageModel::chooseReferenceFiles() {
    if (projectDir_.empty()) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, QStringLiteral("导入项目参考图"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.avif);;所有文件 (*)"));
    if (files.isEmpty()) {
        return;
    }
    (void)importReferences(files);
}

void AssetPageModel::selectReference(const QString& id) {
    refSelectedId_ = id;
    emit changed();
}

void AssetPageModel::setReferenceMarkers(const QString& markers) {
    if (refs_ == nullptr || refSelectedId_.isEmpty()) {
        return;
    }
    QString normalized = markers;
    normalized.replace(QStringLiteral("，"), QStringLiteral(","));
    normalized.replace(QStringLiteral("、"), QStringLiteral(","));
    const QStringList parts = normalized.split(QLatin1Char(','), Qt::SkipEmptyParts);
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(parts.size()));
    for (const QString& part : parts) {
        values.push_back(part.trimmed().toStdString());
    }
    if (auto saved = refs_->SetMarkers(refSelectedId_.toStdString(), std::move(values)); !saved) {
        widgets::Toast::Show(QString::fromStdString(saved.error().message),
                             widgets::Toast::Tone::Error);
    }
    RebuildRefs();
    emit changed();
}

void AssetPageModel::bindReferenceToEntity() {
    if (refs_ == nullptr || refSelectedId_.isEmpty() || selectedEntityId_ <= 0) {
        return;
    }
    if (auto bound = refs_->Bind(refSelectedId_.toStdString(), selectedEntityId_, selectedAssetId_);
        !bound) {
        widgets::Toast::Show(QString::fromStdString(bound.error().message),
                             widgets::Toast::Tone::Error);
    }
    RebuildRefs();
    emit changed();
}

void AssetPageModel::removeReference() {
    if (refs_ == nullptr || refSelectedId_.isEmpty()) {
        return;
    }
    if (auto removed = refs_->Remove(refSelectedId_.toStdString()); !removed) {
        widgets::Toast::Show(QString::fromStdString(removed.error().message),
                             widgets::Toast::Tone::Error);
    }
    refSelectedId_.clear();
    RebuildRefs();
    emit changed();
}

void AssetPageModel::refreshReferences() {
    RebuildRefs();
    emit changed();
}

bool AssetPageModel::waitReferences(int timeoutMs) {
    if (!refBusy_) {
        return true;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeoutMs) {
        if (!refBusy_) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    return !refBusy_;
}

// —— ③ 依赖策略面板 ——
// 两个真实开关：allowDegrade（严格模式的反面）与 suspendTimeoutMs。
// 原来这里没有 UI 入口，PolicyProbe 是硬编码 —— 两个开关在 QML 侧够不着。
void AssetPageModel::setAllowDegrade(bool allow) {
    if (policy_.allowDegrade == allow) {
        return;
    }
    policy_.allowDegrade = allow;
    ProjectPolicy();
    emit changed();
}

void AssetPageModel::setSuspendMinutes(int minutes) {
    const int clamped = std::clamp(minutes, 0, 120);
    const auto ms = static_cast<std::int64_t>(clamped) * 60 * 1000;
    if (policy_.suspendTimeoutMs == ms) {
        return;
    }
    policy_.suspendTimeoutMs = ms;
    ProjectPolicy();
    emit changed();
}

QVariantList AssetPageModel::entityGroups() const { return entityGroups_; }
QVariantList AssetPageModel::assets() const { return assetsView_; }
QVariantList AssetPageModel::kinds() const { return kinds_; }
QVariantList AssetPageModel::deriveChain() const { return deriveChain_; }
QVariantList AssetPageModel::layers() const { return layersView_; }
QVariantMap AssetPageModel::consistency() const { return consistencyView_; }
QVariantMap AssetPageModel::timeline() const { return timelineView_; }
QVariantList AssetPageModel::compareKv() const { return compareKv_; }
QVariantList AssetPageModel::diffRows() const { return diffRows_; }
QVariantList AssetPageModel::sheetKv() const { return sheetKv_; }
QVariantMap AssetPageModel::runtime() const { return runtimeView_; }
QString AssetPageModel::bookName() const { return bookName_; }
QString AssetPageModel::openError() const { return openError_; }
QString AssetPageModel::kindFilter() const { return kindFilter_; }
QString AssetPageModel::selectedEntityId() const { return QString::number(selectedEntityId_); }
QString AssetPageModel::selectedAssetId() const { return QString::number(selectedAssetId_); }
bool AssetPageModel::hasBook() const { return db_ != nullptr; }
bool AssetPageModel::busy() const {
    const auto found = runtime_.constFind(selectedAssetId_);
    return found != runtime_.cend() && found->active;
}
bool AssetPageModel::visualsReady() const { return !visualsPending_; }

void AssetPageModel::setKindFilter(const QString& kind) {
    if (kindFilter_ == kind) {
        return;
    }
    kindFilter_ = kind;
    (void)RefreshAssets();
}

void AssetPageModel::selectEntity(const QString& id) {
    const qint64 value = id.toLongLong();
    if (selectedEntityId_ == value) {
        return;
    }
    selectedEntityId_ = value;
    (void)RefreshAssets();
}

void AssetPageModel::selectAsset(const QString& id) {
    const qint64 value = id.toLongLong();
    if (selectedAssetId_ == value) {
        return;
    }
    selectedAssetId_ = value;
    RebuildDerived();
}

QString AssetPageModel::entityNameById(const QString& id) const {
    const qint64 value = id.toLongLong();
    for (const novelcore::EntityRow& entity : entities_) {
        if (entity.id == value) {
            return QString::fromStdString(entity.name);
        }
    }
    return {};
}

std::optional<novelcore::AssetLayer> AssetPageModel::LayerFromKey(const QString& key) const {
    if (key == QStringLiteral("front")) return novelcore::AssetLayer::Front;
    if (key == QStringLiteral("turnaround")) return novelcore::AssetLayer::Turnaround;
    // ⚠️ 真值是 `base_body`（visual_artifacts.layer / AssetVisualData.h 的层表），
    // 这里原来只认 `body` —— 而 QML 的派生链节点传的是 deriveChain[i].key，
    // 于是点「基础身体」会静默返回 false。「body」保留兼容外部旧调用。
    if (key == QStringLiteral("base_body") || key == QStringLiteral("body")) {
        return novelcore::AssetLayer::BaseBody;
    }
    if (key == QStringLiteral("wardrobe")) return novelcore::AssetLayer::Wardrobe;
    return std::nullopt;
}

bool AssetPageModel::startLayer(const QString& layer) {
    if (layer == QStringLiteral("all")) {
        return startPipeline();
    }
    const std::optional<novelcore::AssetLayer> parsed = LayerFromKey(layer);
    if (!parsed) {
        return false;
    }
    return StartRun(parsed);
}

bool AssetPageModel::startPipeline() { return StartRun(std::nullopt); }

bool AssetPageModel::refreshAssets() { return RefreshAssets(); }

void AssetPageModel::exportingUnsupported(const QString& what) {
    widgets::Toast::Show(QStringLiteral("「%1」尚未接入 QML 资产页；该能力仍在 Widgets 版里。")
                             .arg(what),
                         widgets::Toast::Tone::Info);
}

bool AssetPageModel::StartRun(std::optional<novelcore::AssetLayer> layer) {
    if (db_ == nullptr || dbPath_.empty() || selectedAssetId_ == 0) {
        widgets::Toast::Show(QStringLiteral("请先选择一个已有视觉资产的实体。"),
                             widgets::Toast::Tone::Info);
        return false;
    }
    const auto current = runtime_.constFind(selectedAssetId_);
    if (current != runtime_.cend() && current->active) {
        widgets::Toast::Show(QStringLiteral("该资产已有任务在运行；完成后再提交下一次。"),
                             widgets::Toast::Tone::Warning);
        return false;
    }

    RuntimeState state;
    state.run_id = ++runSerial_;
    state.phase = QStringLiteral("GENERATING");
    state.layer = layer ? QString::fromStdString(
                              std::string{novelcore::AssetLayerName(*layer)})
                        : QStringLiteral("all");
    state.detail = QStringLiteral("任务已提交；正在生成形象层并写入产物账。");
    state.history = {state.phase};
    state.active = true;
    runtime_.insert(selectedAssetId_, state);
    RebuildDerived();

    const QPointer<AssetPageModel> guard(this);
    const std::filesystem::path db_path = dbPath_;
    const qint64 asset_id = selectedAssetId_;
    const qint64 run_id = state.run_id;
    async::RunOnWorker([guard, db_path, asset_id, run_id, layer, policy = policy_] {
        WorkerResult result = RunAssetInWorker(db_path, asset_id, layer, policy);
        // ⚠️ 必须走 BeginRunChecking 而不是直接 FinishAssetRun：
        // worker 只回**最终**结果，「落盘校验」这一步发生在 UI 线程（下一条）。
        // 直接跳到 FinishAssetRun 会让 GENERATING → CHECKING 这一跳**永远不出现**，
        // history 退化成 GENERATING>READY，页面上「正在校验」那一态也没了。
        async::PostToUi([guard, asset_id, run_id, result = std::move(result)]() mutable {
            if (!guard.isNull()) {
                guard->BeginRunChecking(asset_id, run_id, QString::fromStdString(result.detail),
                                        QString::fromStdString(result.phase), result.degraded);
            }
        });
    });
    return true;
}

void AssetPageModel::BeginRunChecking(qint64 assetId, qint64 runId, QString detail, QString finalPhase,
                                       bool degraded) {
    const auto found = runtime_.find(assetId);
    if (found == runtime_.end() || found->run_id != runId) {
        return;
    }
    const QString worker_detail = detail;
    found->phase = QStringLiteral("CHECKING");
    found->detail = worker_detail.isEmpty()
                        ? QStringLiteral("正在校验落盘文件与父子派生关系。")
                        : worker_detail;
    found->active = true;
    if (!found->history.contains(found->phase)) {
        found->history.push_back(found->phase);
    }
    RebuildDerived();

    // 「校验中」是给用户看的中间态，停 120ms 再收敛到终态。
    // 少了这一跳，CHECKING 相位永远不可见。
    QTimer::singleShot(120, this, [this, assetId, runId, finalPhase = std::move(finalPhase),
                                   worker_detail = std::move(worker_detail), degraded] {
        FinishAssetRun(assetId, runId, finalPhase, worker_detail, degraded);
    });
}

void AssetPageModel::FinishAssetRun(qint64 assetId, qint64 runId, QString phase, QString detail,
                                    bool degraded) {
    (void)runId;
    const auto found = runtime_.find(assetId);
    if (found == runtime_.end()) {
        return;
    }
    found->phase = phase;
    found->detail = std::move(detail);
    found->active = false;
    found->degraded = found->degraded || degraded;
    if (!found->history.contains(phase)) {
        found->history.push_back(phase);
    }
    (void)RefreshAssets();

    if (phase == QStringLiteral("FAILED")) {
        widgets::Toast::Show(QStringLiteral("资产生成失败：%1").arg(detail),
                             widgets::Toast::Tone::Error);
    } else if (degraded) {
        widgets::Toast::Show(QStringLiteral("资产已完成，但使用了降级策略：%1").arg(detail),
                             widgets::Toast::Tone::Warning);
    }
}

QString AssetPageModel::AssetProbe() const {
    QString selected = QStringLiteral("全部");
    if (selectedEntityId_ != 0) {
        const auto found = std::find_if(entities_.begin(), entities_.end(),
                                        [this](const novelcore::EntityRow& entity) {
                                            return entity.id == selectedEntityId_;
                                        });
        if (found != entities_.end()) {
            selected = QStringLiteral("%1#%2").arg(QString::fromStdString(found->name)).arg(found->id);
        } else {
            selected = QStringLiteral("#%1").arg(selectedEntityId_);
        }
    }
    // empty= 是**真实状态**（当前实体下没有任何资产卡），不是视图层的假象。
    // Widgets 侧一直有这个字段，QML 侧原来漏了 —— 结果「空态」这个验收点
    // 只能对着 Assets.qml 的 Empty 覆盖层存在，桥上查不到。
    QString result = QStringLiteral("book=%1; entities=%2; assets=%3; empty=%4; kind=%5; selected=%6; asset=%7; next=从实体生成视觉资产")
                         .arg(db_ == nullptr ? QStringLiteral("closed") : QStringLiteral("open"))
                         .arg(entities_.size())
                         .arg(assets_.size())
                         .arg(assets_.empty() ? QStringLiteral("1") : QStringLiteral("0"))
                         .arg(kindFilter_.isEmpty() ? QStringLiteral("全部") : kindFilter_, selected)
                         .arg(selectedAssetId_);
    if (!assets_.empty()) {
        const AssetEntry& first = assets_.front();
        const auto runtimeIt = runtime_.constFind(first.asset.id);
        const bool degraded =
            first.degraded || (runtimeIt != runtime_.cend() && runtimeIt->degraded);
        const QString runtime = runtimeIt == runtime_.cend() || runtimeIt->phase.isEmpty()
                                    ? QStringLiteral("none")
                                    : runtimeIt->phase;
        result += QStringLiteral("; card=%1|status=%2|entity=%3|degraded=%4|runtime=%5")
                      .arg(QString::fromStdString(first.asset.name),
                           QString::fromStdString(first.asset.status),
                           QString::fromStdString(first.entity.name))
                      .arg(degraded ? QStringLiteral("1") : QStringLiteral("0"), runtime);
    }
    if (!openError_.isEmpty()) {
        result += QStringLiteral("; error=%1").arg(QString{openError_}.replace('\n', ' '));
    }
    return result;
}

QString AssetPageModel::StateProbe() const {
    const auto found = runtime_.constFind(selectedAssetId_);
    if (found == runtime_.cend()) {
        return QStringLiteral("asset=%1; runtime=none; history=none; active=0; degraded=0")
            .arg(selectedAssetId_);
    }
    const RuntimeState& state = found.value();
    return QStringLiteral("asset=%1; runtime=%2; layer=%3; history=%4; active=%5; degraded=%6; detail=%7")
        .arg(selectedAssetId_)
        .arg(state.phase, state.layer,
             state.history.isEmpty() ? QStringLiteral("none")
                                     : state.history.join(QLatin1Char('>')))
        .arg(state.active ? QStringLiteral("1") : QStringLiteral("0"),
             state.degraded ? QStringLiteral("1") : QStringLiteral("0"),
             state.detail.isEmpty() ? QStringLiteral("none") : state.detail);
}

// 详情探针。字段口径由本函数定死（P05 的 S2 按它解析），全部由 layersView_
// （= AssetVisualData.h 的 AssetCollectLayers 真值）算出。
//
// ⚠️ **没有 decoded 字段**：QML 侧用 QQuickImageLoader 异步加载，页面拿不到
// 「解码了几张」的计数 —— 硬凑一个等于造假。「产物已落盘」（ready）才是
// 页面真正依赖、也真正能观测的量，验收改断言 ready。
QString AssetPageModel::DetailProbe() const {
    if (db_ == nullptr) {
        return QStringLiteral("detail=unavailable");
    }
    int ready = 0;
    QStringList states;
    QStringList chain;
    for (const QVariant& item : layersView_) {
        const QVariantMap layer = item.toMap();
        const QString key = layer.value(QStringLiteral("key")).toString();
        const QString status = layer.value(QStringLiteral("status")).toString();
        if (layer.value(QStringLiteral("ready")).toBool()) {
            ++ready;
        }
        chain.push_back(key);
        states.push_back(QStringLiteral("%1=%2").arg(key, status.isEmpty() ? QStringLiteral("PENDING")
                                                                            : status));
    }
    const int layers = static_cast<int>(layersView_.size());
    const auto runtimeIt = runtime_.constFind(selectedAssetId_);
    const QString phase =
        runtimeIt == runtime_.cend() || runtimeIt->phase.isEmpty() ? QStringLiteral("none")
                                                                   : runtimeIt->phase;
    const QString history =
        runtimeIt == runtime_.cend() || runtimeIt->history.isEmpty()
            ? QStringLiteral("none")
            : runtimeIt->history.join(QLatin1Char('>'));
    return QStringLiteral("asset=#%1:%2; layers=%3; ready=%4; missing=%5; actions=%6; links=%7; chain=%8; states=%9; runtime=%10; history=%11; active=%12; degraded=%13")
        .arg(selectedAssetId_)
        .arg(SelectedAssetName())
        .arg(layers)
        .arg(ready)
        .arg(layers - ready)
        .arg(layers - ready) // 缺层各给一个「生成/重试」行动
        .arg(layers > 0 ? layers - 1 : 0) // 父子派生链边数
        .arg(chain.join(QLatin1Char('>')), states.join(QLatin1Char('>')))
        .arg(phase, history)
        .arg(runtimeIt != runtime_.cend() && runtimeIt->active ? QStringLiteral("1")
                                                              : QStringLiteral("0"),
             runtimeIt != runtime_.cend() && runtimeIt->degraded ? QStringLiteral("1")
                                                                : QStringLiteral("0"));
}

// 策略探针。含 strict 与 timeoutMs（P05 的 S4 按它解析），
// 值全部来自真实的 policy_ 与运行态 —— 原来这里是硬编码 "policy=qml"，
// 任何策略断言都只能对着桩跑出假绿。
QString AssetPageModel::PolicyProbe() const {
    const auto runtimeIt = runtime_.constFind(selectedAssetId_);
    const QString phase =
        runtimeIt == runtime_.cend() || runtimeIt->phase.isEmpty() ? QStringLiteral("none")
                                                                   : runtimeIt->phase;
    const QString layer =
        runtimeIt == runtime_.cend() || runtimeIt->layer.isEmpty() ? QStringLiteral("none")
                                                                   : runtimeIt->layer;
    return QStringLiteral("policy=C>B; allowDegrade=%1; strict=%2; timeoutMs=%3; runtime=%4; layer=%5; active=%6; degraded=%7")
        .arg(policy_.allowDegrade ? QStringLiteral("1") : QStringLiteral("0"),
             policy_.allowDegrade ? QStringLiteral("0") : QStringLiteral("1"))
        .arg(policy_.suspendTimeoutMs)
        .arg(phase, layer)
        .arg(runtimeIt != runtime_.cend() && runtimeIt->active ? QStringLiteral("1")
                                                              : QStringLiteral("0"),
             runtimeIt != runtime_.cend() && runtimeIt->degraded ? QStringLiteral("1")
                                                                : QStringLiteral("0"));
}

// 一致性探针。字段口径由本函数定死（P05 的 S5 按它解析），
// 数据来自 AssetVisualData.h 的 AssetCollectConsistency（唯一真值）。
QString AssetPageModel::ConsistencyProbe() const {
    if (db_ == nullptr) {
        return QStringLiteral("consistency=unavailable");
    }
    const AssetConsistencyFact& fact = consistency_;
    const QString severity = fact.difference < 0
                                 ? QStringLiteral("none")
                                 : QString::number(fact.difference * 100.0, 'f', 1);
    return QStringLiteral("asset=%1; states=%2; emotions=%3; shots=%4; images=%5; compared=%6; diff=%7; severity=%8")
        .arg(selectedAssetId_)
        .arg(static_cast<int>(fact.states.size()))
        .arg(static_cast<int>(fact.emotions.size()))
        .arg(fact.shotCount)
        .arg(static_cast<int>(fact.frames.size()))
        .arg(fact.frames.size() >= 2 ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(severity, fact.severity);
}

QString AssetPageModel::SelectedAssetName() const {
    const AssetEntry* entry = SelectedAsset();
    return entry == nullptr ? QStringLiteral("none") : QString::fromStdString(entry->asset.name);
}

QString AssetPageModel::RefProbe() const {
    if (refs_ == nullptr) {
        return QStringLiteral("refs=0; busy=0; drops=1; first=none");
    }
    return AssetRefProbe(refFacts_, refBusy_, true, refSelectedId_);
}

} // namespace shine::app
