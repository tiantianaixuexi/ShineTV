#include "ui/pages/assets/AssetPageModel.h"

#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "ui/kit/controls/Surfaces.h"
#include "util/Encoding.h"

#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPointer>

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

// 与 AssetWorkspace 的 kAssetEntityKinds 同序同值（同一份词表，不新增真值）
constexpr std::array<KindInfo, 4> kKinds = {{
    {"person", "人物", "人物/生物"},
    {"location", "地点", "空间/势力"},
    {"item", "物品", "装备"},
    {"faction", "势力", "空间/势力"},
}};

[[nodiscard]] bool IsMissingAsset(const std::string& message) {
    return message == "该实体尚无视觉资产";
}

// 状态词表与 tone：逐条照搬 AssetWorkspace.cpp 的 StatusLabel / StatusTone。
// QML 只消费「中文标签 + tone 名」，不认识 PENDING/SHEET_READY 这些内部值。
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

// 两张帧图的平均绝对差。**只在 worker 上调用** —— 解码 + 逐像素比较都是
// 重活，放 UI 线程就是当初 H-2 那类卡顿。下采样到 64×64 与 ConsistencyView 同口径。
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

// 从 AssetWorkspace.cpp 原样搬来：这一段是资产管线的唯一实现，
// Widgets 版删除后由本文件继续持有（不要在两处各写一份）。
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
    // 设定集事实行（口径与 Widgets 的 AssetDetailView 一致，全部来自真库行数）
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
    if (key == QStringLiteral("body")) return novelcore::AssetLayer::BaseBody;
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
        async::PostToUi([guard, asset_id, run_id, result = std::move(result)]() mutable {
            if (!guard.isNull()) {
                guard->FinishAssetRun(asset_id, run_id, QString::fromStdString(result.phase),
                                      QString::fromStdString(result.detail), result.degraded);
            }
        });
    });
    return true;
}

void AssetPageModel::BeginRunChecking(qint64 assetId, qint64 runId, QString detail, QString finalPhase,
                                       bool degraded) {
    const auto found = runtime_.find(assetId);
    if (found == runtime_.end()) {
        return;
    }
    found->phase = QStringLiteral("CHECKING");
    found->detail = std::move(detail);
    found->history.append(QStringLiteral("CHECKING"));
    found->degraded = found->degraded || degraded;
    (void)runId;
    (void)finalPhase;
    RebuildDerived();
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
    return QStringLiteral("entities=%1; assets=%2; kind=%3; selected=%4; asset=%5")
        .arg(entities_.size())
        .arg(assets_.size())
        .arg(kindFilter_.isEmpty() ? QStringLiteral("全部") : kindFilter_, selected)
        .arg(selectedAssetId_);
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

} // namespace shine::app
