#pragma once
// ui/pages/assets/AssetVisualData —— 资产页「形象层 / 一致性 / 时间线」三块的
// **只读取数**与数据契约（QML 迁移期间由 Widgets 侧与 AssetPageModel 共用）。
//
// 为什么抽出来：这三条链路各自在两个地方被独立实现过一次 ——
//   一致性：Widgets 侧 ConsistencyView（真实现） vs QML 的 AssetsCompare.qml（写死 mock）
//   派生链：Widgets 侧 AssetDetailView 的 kLayers vs AssetPageModel 构造函数里的硬编码四层
//   时间线：Widgets 侧 AssetDetailView 的 CollectTimeline vs QML 的 AssetsTimeline.qml（写死 mock）
// 两份实现必然漂移，而且已经漂了（一边是 read-only 假数据，一边是真库）。
// 收敛到这一份后：真值只在这里算一次，两侧都只是渲染层。
// （Widgets 侧那三个类已随 QML 迁移退役删除，本头是它们留下的唯一真值。）
//
// 纪律（与 AssetPageModel 一致）：
//   * 全部只读真库（visual_states / character_status / shots / generated_images /
//     visual_artifacts）与文件系统存在性，**不造假数据**。
//   * 图像解码不得进本文件：只回路径，像素缩放交给各渲染层的 worker。
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "novel/NovelTypes.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "visual/ReferenceLibrary.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shine::db::sqlite {
class Database;
}

namespace shine::app {

// —— 形象层定义（webui Assets.jsx 的 V0 四层顺序即派生顺序）——
// 这张表是四层顺序与中文层名的**唯一真值**，不要在别处再写一份。
struct AssetLayerSpec {
    novelcore::AssetLayer layer;
    std::string_view key;    // visual_artifacts.layer
    std::string_view title;  // 中文层名
    std::string_view parent; // 上一层 key（"" = 根）
};

[[nodiscard]] inline const std::vector<AssetLayerSpec>& AssetLayerSpecs() {
    static const std::vector<AssetLayerSpec> specs{
        {novelcore::AssetLayer::Front, "front", "正脸", ""},
        {novelcore::AssetLayer::Turnaround, "turnaround", "四视图", "front"},
        {novelcore::AssetLayer::BaseBody, "base_body", "基础身体", "turnaround"},
        {novelcore::AssetLayer::Wardrobe, "wardrobe", "服装", "base_body"},
    };
    return specs;
}

// rel_path 为空时返回 nullopt；绝对路径原样返回，相对路径挂到 projectDir 下。
[[nodiscard]] inline std::optional<std::filesystem::path>
AssetResolvePath(const std::filesystem::path& projectDir, std::string_view relative) {
    if (relative.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path path = util::PathFromUtf8(relative);
    return path.is_absolute() ? std::optional<std::filesystem::path>(path)
                              : std::optional<std::filesystem::path>(projectDir / path);
}

// ===================================================================
// ① 形象层：读 visual_artifacts + 旧单图回退 + 落盘存在性
// ===================================================================
struct AssetLayerFact {
    std::string key;
    QString title;
    QString parentKey;
    QString relPath;    // 相对 projectDir 的产物路径（空 = 该层还没有产物）
    QString absPath;    // 绝对路径，QML 侧直接当 file:// 用
    QString status;     // visual_artifacts.status
    bool ready = false; // status==DONE 且文件确实在盘上
    bool legacyFront = false; // 来自 visual_assets.sheet_rel_path 的旧工程回退
};

[[nodiscard]] inline std::vector<AssetLayerFact>
AssetCollectLayers(novelcore::NovelVisual& visual, const novelcore::VisualAssetRow& asset,
                   const std::filesystem::path& projectDir) {
    std::vector<AssetLayerFact> out;
    out.reserve(AssetLayerSpecs().size());

    std::vector<novelcore::VisualArtifactRow> artifacts;
    if (auto listed = visual.ListArtifacts(asset.id); listed) {
        artifacts = std::move(*listed);
    }

    for (const AssetLayerSpec& spec : AssetLayerSpecs()) {
        AssetLayerFact fact;
        fact.key = std::string{spec.key};
        fact.title = QString::fromUtf8(spec.title.data(), static_cast<int>(spec.title.size()));
        fact.parentKey = QString::fromUtf8(spec.parent.data(), static_cast<int>(spec.parent.size()));

        const novelcore::VisualArtifactRow* hit = nullptr;
        for (const novelcore::VisualArtifactRow& artifact : artifacts) {
            if (artifact.layer == fact.key) {
                hit = &artifact;
                break;
            }
        }
        std::string rel;
        if (hit != nullptr) {
            rel = hit->rel_path;
            fact.status = QString::fromStdString(hit->status);
        } else if (fact.key == "front" && !asset.sheet_rel_path.empty()) {
            // 旧工程没有 visual_artifacts 行，只有 visual_assets.sheet_rel_path。
            rel = asset.sheet_rel_path;
            fact.status = QStringLiteral("DONE");
            fact.legacyFront = true;
        }
        if (rel.empty()) {
            out.push_back(std::move(fact));
            continue;
        }
        const std::optional<std::filesystem::path> abs = AssetResolvePath(projectDir, rel);
        if (!abs) {
            out.push_back(std::move(fact));
            continue;
        }
        std::error_code ec;
        fact.absPath = QString::fromStdString(util::PathToUtf8(*abs));
        fact.ready = fact.status == QStringLiteral("DONE") && std::filesystem::is_regular_file(*abs, ec);
        if (!fact.ready) {
            // 不可用就别把半截路径递给 QML，省得它去 load 一个不存在的文件。
            fact.absPath.clear();
        }
        fact.relPath = QString::fromStdString(rel);
        out.push_back(std::move(fact));
    }
    return out;
}

// ===================================================================
// ② 一致性：visual_states 按章基线 + character_status 情绪 + 同角色镜头帧
// ===================================================================
struct AssetStateFact {
    int chapterOrd = 0;   // 章序（1 基）。**from_chapter 存的是章节行 id，不能当章号显示**
    QString where;        // 人类可读章位；行 id 定位不到时显式说明
    QString stage;        // stage_label，缺则用 stage_key
    QString appearance;
    QString colors;
    QString note;
    QString tone;         // ok / warn / danger
};

struct AssetEmotionFact {
    int chapterOrd = 0;
    QString where;
    QString summary;
};

struct AssetShotFrameFact {
    qint64 shotId = 0;
    int chapterOrd = 0;
    QString label;
    QString absPath;      // 帧图绝对路径；**像素解码交给渲染层 worker**
    bool hasImage = false;
};

struct AssetConsistencyFact {
    std::vector<AssetStateFact> states;
    std::vector<AssetEmotionFact> emotions;
    std::vector<AssetShotFrameFact> frames; // 已按 chapterOrd 排序
    int shotCount = 0;                      // 该实体在全部章节命中的镜头数
    double difference = -1.0;               // 首两帧平均绝对差；<0 = 未比对
    QString severity;                       // none/same/minor/significant
    QString verdict;                        // 中文结论
    QString firstLabel;                     // 基线章位
    QString lastLabel;                      // 当前章位
};

// ⚠️ 本函数只查库、只 stat 文件，**不解码像素**。difference 由渲染层拿到
// 两帧后在 worker 上算（别搬回 UI 线程 —— 那正是当初 H-2 那类卡顿）。
[[nodiscard]] inline AssetConsistencyFact
AssetCollectConsistency(shine::db::sqlite::Database& db, novelcore::NovelVisual& visual,
                        const novelcore::VisualAssetRow& asset, novelcore::RowId entityId,
                        const std::filesystem::path& projectDir) {
    AssetConsistencyFact fact;
    if (asset.id <= 0 || entityId <= 0) {
        return fact;
    }
    fact.severity = QStringLiteral("none");
    fact.verdict = QStringLiteral("未比对");

    auto state_result = visual.ListStates(asset.id);
    if (!state_result) {
        return fact;
    }
    novelcore::NovelGraph graph(db);
    auto status_result = graph.ListCharacterStatuses(entityId);
    auto chapters = graph.ListChapters(1000);

    // 章节行 id → 章序。visual_states.from_chapter 与 character_status.chapter_id
    // 存的都是行 id，直接当章号显示会得到「第 1042 章」这种假数据。
    std::unordered_map<novelcore::RowId, int> chapter_ord;
    if (chapters) {
        for (const novelcore::ChapterRow& chapter : *chapters) {
            chapter_ord.emplace(chapter.id, chapter.ord);
        }
    }
    const auto ord_of = [&chapter_ord](novelcore::RowId chapterId) {
        const auto found = chapter_ord.find(chapterId);
        return found == chapter_ord.end() ? 0 : found->second;
    };
    const auto where_of = [](int ord, novelcore::RowId rawId) {
        return ord > 0 ? QStringLiteral("第 %1 章").arg(ord)
                       : QStringLiteral("章节未定位（#%1）").arg(rawId);
    };

    for (const auto& state : *state_result) {
        AssetStateFact line;
        line.chapterOrd = ord_of(state.from_chapter);
        line.where = where_of(line.chapterOrd, state.from_chapter);
        line.stage = state.stage_label.empty() ? QString::fromStdString(state.stage_key)
                                               : QString::fromStdString(state.stage_label);
        line.appearance = QString::fromStdString(state.appearance);
        line.colors = QString::fromStdString(state.materials_colors);
        line.tone = QStringLiteral("ok");
        line.note = QStringLiteral("与基线一致");
        if (!fact.states.empty()) {
            const AssetStateFact& baseline = fact.states.front();
            const bool appearance_changed = state.appearance !=
                                           std::string{baseline.appearance.toStdString()};
            const bool colors_changed =
                state.materials_colors != std::string{baseline.colors.toStdString()};
            line.tone = (appearance_changed && colors_changed)   ? QStringLiteral("danger")
                        : (appearance_changed || colors_changed) ? QStringLiteral("warn")
                                                                 : QStringLiteral("ok");
            line.note = line.tone == QStringLiteral("ok")  ? QStringLiteral("与基线一致")
                       : line.tone == QStringLiteral("warn") ? QStringLiteral("颜色或单项外观变化")
                                                             : QStringLiteral("外观与颜色均显著变化");
        }
        fact.states.push_back(std::move(line));
    }
    if (status_result) {
        for (const auto& status : *status_result) {
            AssetEmotionFact line;
            line.chapterOrd = ord_of(status.chapter_id);
            line.where = where_of(line.chapterOrd, status.chapter_id);
            line.summary = QString::fromStdString(status.emotion_json);
            fact.emotions.push_back(std::move(line));
        }
    }

    // 同实体镜头：chapters → ListShotsByChapter → character_ids_json 含本实体
    std::unordered_map<novelcore::RowId, int> matched;
    if (chapters) {
        for (const novelcore::ChapterRow& chapter : *chapters) {
            auto shots = visual.ListShotsByChapter(chapter.id);
            if (!shots) {
                continue;
            }
            for (const auto& shot : *shots) {
                const std::string ids = shot.character_ids_json;
                if (ids.find(std::to_string(entityId)) != std::string::npos) {
                    matched[shot.id] = chapter.ord;
                }
            }
        }
    }
    fact.shotCount = static_cast<int>(matched.size());

    if (auto generated = novelcore::ListGeneratedImages(db, 5000); generated) {
        for (const auto& image : *generated) {
            if (image.source_kind != "shot" || image.rel_path.empty() ||
                matched.find(image.source_id) == matched.end()) {
                continue;
            }
            const std::optional<std::filesystem::path> abs = AssetResolvePath(projectDir, image.rel_path);
            if (!abs) {
                continue;
            }
            std::error_code ec;
            if (!std::filesystem::is_regular_file(*abs, ec)) {
                continue;
            }
            AssetShotFrameFact frame;
            frame.shotId = image.source_id;
            frame.chapterOrd = matched[image.source_id];
            frame.label = QStringLiteral("第 %1 章 / 镜头 #%2")
                              .arg(frame.chapterOrd)
                              .arg(image.source_id);
            frame.absPath = QString::fromStdString(util::PathToUtf8(*abs));
            frame.hasImage = true;
            fact.frames.push_back(std::move(frame));
        }
    }
    std::sort(fact.frames.begin(), fact.frames.end(),
              [](const AssetShotFrameFact& a, const AssetShotFrameFact& b) {
                  return a.chapterOrd < b.chapterOrd;
              });

    const auto chapter_of = [](int ord) {
        return ord > 0 ? QStringLiteral("第 %1 章").arg(ord) : QStringLiteral("未定位");
    };
    fact.firstLabel = fact.states.empty() ? QStringLiteral("暂无")
                                           : chapter_of(fact.states.front().chapterOrd);
    fact.lastLabel = fact.states.empty() ? QStringLiteral("暂无")
                                          : chapter_of(fact.states.back().chapterOrd);
    return fact;
}

// 像素差异的严重度口径集中在这里，别在两个渲染层各判一次。
inline void AssetApplyDifference(AssetConsistencyFact& fact, double difference) {
    fact.difference = difference;
    if (difference < 0) {
        fact.severity = QStringLiteral("none");
        fact.verdict = QStringLiteral("未比对");
    } else if (difference <= 0.02) {
        fact.severity = QStringLiteral("same");
        fact.verdict = QStringLiteral("全部一致");
    } else if (difference <= 0.12) {
        fact.severity = QStringLiteral("minor");
        fact.verdict = QStringLiteral("轻微差异");
    } else {
        fact.severity = QStringLiteral("significant");
        fact.verdict = QStringLiteral("显著差异");
    }
}

// 逐段对照基线：外观 / 色彩两项**分开判**，避免一格混判。
// 返回 [{key, changed}]，key 是中文项名。
[[nodiscard]] inline std::vector<std::pair<QString, bool>> AssetDiffRows(
    const AssetConsistencyFact& fact) {
    std::vector<std::pair<QString, bool>> rows;
    if (fact.states.size() < 2) {
        return rows;
    }
    const AssetStateFact& baseline = fact.states.front();
    for (std::size_t i = 1; i < fact.states.size(); ++i) {
        const AssetStateFact& line = fact.states[i];
        rows.emplace_back(QStringLiteral("外观 / 服装"),
                          line.appearance != baseline.appearance);
        rows.emplace_back(QStringLiteral("色彩基调"), line.colors != baseline.colors);
    }
    return rows;
}

// ===================================================================
// ④ 项目参考库：assets/refs/ 的只读投影 + 探针口径
// ===================================================================
// 这套原先只存在于已删除的 Widgets 侧 RefLibraryView。QML 迁移后视图重建，
// 但**行 → 字段的映射与探针口径不该重写**：一处漂移就会让 QML 侧与验收脚本
// 读出不同的 entity/markers。取值仍全部来自 visual::ReferenceLibrary
// （refs.json + 文件 stat），不新增真值。
struct AssetRefFact {
    QString id;
    QString originalName;
    QString relPath;
    QString absPath;
    QString orientation;
    int rawWidth = 0;
    int rawHeight = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    qint64 entityId = 0;
    qint64 assetId = 0;
    QStringList markers;
};

[[nodiscard]] inline std::vector<AssetRefFact>
AssetCollectRefs(const std::vector<visual::ReferenceImage>& images,
                 const std::filesystem::path& projectDir) {
    std::vector<AssetRefFact> out;
    out.reserve(images.size());
    for (const visual::ReferenceImage& image : images) {
        AssetRefFact fact;
        fact.id = QString::fromStdString(image.id);
        fact.originalName = QString::fromStdString(image.original_name);
        fact.relPath = QString::fromStdString(image.rel_path);
        const std::optional<std::filesystem::path> abs = AssetResolvePath(projectDir, image.rel_path);
        if (abs) {
            fact.absPath = QString::fromStdString(util::PathToUtf8(*abs));
        }
        fact.orientation = QString::fromStdString(image.orientation);
        fact.rawWidth = image.raw_width;
        fact.rawHeight = image.raw_height;
        fact.displayWidth = image.display_width;
        fact.displayHeight = image.display_height;
        fact.entityId = image.entity_id;
        fact.assetId = image.asset_id;
        for (const std::string& marker : image.markers) {
            fact.markers.push_back(QString::fromStdString(marker));
        }
        out.push_back(std::move(fact));
    }
    return out;
}

// 标记的展示口径（「、」分隔）；无标记时给显式 "none"，不留空串。
[[nodiscard]] inline QString AssetRefMarkers(const QStringList& markers) {
    return markers.isEmpty() ? QStringLiteral("none") : markers.join(QStringLiteral("、"));
}

// 参考库探针。字段口径 `refs=N; busy=N; drops=N; first=…` 由本函数定死 ——
// 验收脚本按这串解析，改字段名就要同步改脚本。
[[nodiscard]] inline QString AssetRefProbe(const std::vector<AssetRefFact>& refs, bool busy,
                                           bool drops, const QString& firstId) {
    QString first = QStringLiteral("none");
    for (const AssetRefFact& fact : refs) {
        if (fact.id != firstId) {
            continue;
        }
        first = QStringLiteral("%1|orientation=%2|display=%3x%4|markers=%5|entity=%6")
                    .arg(fact.originalName, fact.orientation)
                    .arg(fact.displayWidth)
                    .arg(fact.displayHeight)
                    .arg(AssetRefMarkers(fact.markers))
                    .arg(fact.entityId);
        break;
    }
    return QStringLiteral("refs=%1; busy=%2; drops=%3; first=%4")
        .arg(static_cast<int>(refs.size()))
        .arg(busy ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(drops ? QStringLiteral("1") : QStringLiteral("0"), first);
}

// ===================================================================
// ③ 关联时间线：按章外观基线事件 + 绑定镜头 + 镜头参考图
// ===================================================================
struct AssetTimelineEventFact {
    int chapterOrd = 0;
    QString where;   // 人类可读章位
    QString label;   // 事件名（「外观基线 ①」/「绑定镜头」）
    bool hot = false; // 最后一个外观基线标记为「当前」
    int shotId = 0;   // 事件类型：0=外观基线，>0=该镜头 id
    bool isShot = false;
};

struct AssetTimelineFact {
    int chapterCount = 0;                    // 时间轴刻度跨度（章数）
    std::vector<AssetTimelineEventFact> events;
    std::vector<qint64> boundShots;          // 绑定镜头（有序去重）
    std::vector<QString> refImages;          // 镜头参考图绝对路径
    std::unordered_map<qint64, QStringList> shotRefs; // shotId → 该镜头的参考图
};

[[nodiscard]] inline AssetTimelineFact AssetCollectTimeline(shine::db::sqlite::Database& db,
                                                            novelcore::NovelVisual& visual,
                                                            const novelcore::VisualAssetRow& asset,
                                                            novelcore::RowId entityId,
                                                            const std::filesystem::path& projectDir) {
    AssetTimelineFact fact;
    auto state_result = visual.ListStates(asset.id);
    if (!state_result) {
        return fact;
    }
    novelcore::NovelGraph graph(db);
    auto chapters = graph.ListChapters(1000);
    std::unordered_map<novelcore::RowId, int> chapter_ord;
    if (chapters) {
        for (const novelcore::ChapterRow& chapter : *chapters) {
            chapter_ord.emplace(chapter.id, chapter.ord);
        }
    }
    const auto ord_of = [&chapter_ord](novelcore::RowId chapterId) {
        const auto found = chapter_ord.find(chapterId);
        return found == chapter_ord.end() ? 0 : found->second;
    };

    // 外观基线事件：每个 visual_states 一条，最后一条标 hot（=「当前」）
    for (std::size_t i = 0; i < state_result->size(); ++i) {
        const auto& state = (*state_result)[i];
        AssetTimelineEventFact event;
        event.chapterOrd = ord_of(state.from_chapter);
        event.label = QStringLiteral("外观基线 %1").arg(i + 1);
        event.hot = (i + 1 == state_result->size());
        event.isShot = false;
        fact.events.push_back(std::move(event));
    }

    // 绑定镜头：chapters → ListShotsByChapter → character_ids_json 含本实体
    if (chapters) {
        for (const novelcore::ChapterRow& chapter : *chapters) {
            auto shots = visual.ListShotsByChapter(chapter.id);
            if (!shots) {
                continue;
            }
            for (const auto& shot : *shots) {
                if (shot.character_ids_json.find(std::to_string(entityId)) == std::string::npos) {
                    continue;
                }
                fact.boundShots.push_back(shot.id);
                AssetTimelineEventFact event;
                event.chapterOrd = chapter.ord;
                event.shotId = shot.id;
                event.isShot = true;
                event.label = QStringLiteral("镜头 #%1").arg(shot.id);
                fact.events.push_back(std::move(event));
            }
        }
    }
    std::sort(fact.boundShots.begin(), fact.boundShots.end());
    fact.boundShots.erase(std::unique(fact.boundShots.begin(), fact.boundShots.end()),
                          fact.boundShots.end());

    // 镜头参考图：generated_images 里 source_kind=="shot" 且落在绑定镜头集合内
    if (!fact.boundShots.empty()) {
        std::unordered_map<qint64, int> bound_set;
        for (qint64 id : fact.boundShots) {
            bound_set.emplace(id, 0);
        }
        if (auto generated = novelcore::ListGeneratedImages(db, 5000); generated) {
            for (const auto& image : *generated) {
                if (image.source_kind != "shot" || image.rel_path.empty() ||
                    bound_set.find(image.source_id) == bound_set.end()) {
                    continue;
                }
                const std::optional<std::filesystem::path> abs = AssetResolvePath(projectDir, image.rel_path);
                if (!abs) {
                    continue;
                }
                std::error_code ec;
                if (!std::filesystem::is_regular_file(*abs, ec)) {
                    continue;
                }
                const QString path = QString::fromStdString(util::PathToUtf8(*abs));
                fact.refImages.push_back(path);
                fact.shotRefs[image.source_id].push_back(path);
            }
        }
    }

    // 章刻度跨度：取全部事件的最大章序。没有任何事件时给 1（避免除零）。
    int max_ord = 1;
    for (const AssetTimelineEventFact& event : fact.events) {
        max_ord = std::max(max_ord, event.chapterOrd);
    }
    fact.chapterCount = max_ord;
    std::sort(fact.events.begin(), fact.events.end(),
              [](const AssetTimelineEventFact& a, const AssetTimelineEventFact& b) {
                  return a.chapterOrd < b.chapterOrd;
              });
    return fact;
}

} // namespace shine::app
