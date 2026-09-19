#include "novel/NovelAssetPipeline.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "util/Encoding.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <utility>

#include <yyjson.h>

namespace shine::novelcore {
namespace {

constexpr std::string_view kActor = "asset_pipeline";

struct LayerSpec {
    AssetLayer layer;
    std::string_view name;
    std::string_view parent;      // 空 = 派生链起点（文生图）
    std::string_view readyStatus; // 本层 DONE 后资产应达到的 status（`11` §2.6.1）
};

// `11` §2.6.2 的派生链（本 S 覆盖前四层；stage/shot 属分镜侧，不在 V0）。
constexpr LayerSpec kChain[] = {
    {AssetLayer::Front, "front", "", "REF_READY"},
    {AssetLayer::Turnaround, "turnaround", "front", "SHEET_READY"},
    {AssetLayer::BaseBody, "base_body", "turnaround", "SHEET_READY"},
    {AssetLayer::Wardrobe, "wardrobe", "base_body", "WARDROBE_READY"},
};
constexpr std::size_t kChainN = sizeof(kChain) / sizeof(kChain[0]);

constexpr std::string_view kDefaultNegative =
    "lowres, blurry, extra fingers, deformed hands, watermark, text, logo, "
    "extra limbs, bad anatomy, duplicate person";

[[nodiscard]] std::int64_t NowMs() noexcept { return util::NowMillis(); }

[[nodiscard]] DbError PErr(std::string_view m) { return DbError{0, std::string{m}}; }

// 生产状态位阶（`11` §2.6.1）：只用于「不把就绪态写小」。
[[nodiscard]] constexpr int StatusRank(std::string_view s) noexcept {
    if (s == "REF_READY") return 1;
    if (s == "SHEET_READY") return 2;
    if (s == "WARDROBE_READY") return 3;
    if (s == "READY") return 4;
    return 0; // PENDING | PROMPTING | FAILED | STALE | 未知
}

[[nodiscard]] constexpr const LayerSpec* SpecOf(AssetLayer layer) noexcept {
    for (const auto& s : kChain) {
        if (s.layer == layer) return &s;
    }
    return nullptr;
}

// 与 `11` §2.6.1 一致：SHEET_READY 起可作分镜参考图（S1：只等 status，不等 canon_status）
[[nodiscard]] constexpr bool Consumable(std::string_view status) noexcept {
    return StatusRank(status) >= 2;
}

// 挂起记账写在 artifact.note 里：`suspended_since=<毫秒>`（跨调用累计挂起时长，无需队列状态）
[[nodiscard]] std::int64_t ParseSuspendedSince(std::string_view note) noexcept {
    constexpr std::string_view kKey = "suspended_since=";
    const auto pos = note.find(kKey);
    if (pos == std::string_view::npos) return 0;
    std::int64_t out = 0;
    std::size_t i = pos + kKey.size();
    for (; i < note.size(); ++i) {
        const char c = note[i];
        if (c < '0' || c > '9') break;
        out = out * 10 + static_cast<std::int64_t>(c - '0');
    }
    return out;
}

// 取该资产某一层的**最新**一条产物（同层可能因重出而多行）
[[nodiscard]] std::optional<VisualArtifactRow>
FindArtifact(const NovelVisual& vis, RowId assetId, std::string_view layer) {
    auto rows = vis.ListArtifacts(assetId);
    if (!rows) return std::nullopt;
    std::optional<VisualArtifactRow> best;
    for (const auto& r : *rows) {
        if (r.layer != layer) continue;
        if (!best || r.id > best->id) best = r;
    }
    return best;
}

[[nodiscard]] const VisualArtifactRow* FindIn(const std::vector<VisualArtifactRow>& rows,
                                             std::string_view layer) noexcept {
    for (const auto& r : rows) {
        if (r.layer == layer) return &r;
    }
    return nullptr;
}

void LogAuditQuiet(db::sqlite::Database& db, std::string_view action, RowId assetId,
                   std::string_view layer, std::string_view detail) {
    NovelGraph g(db);
    if (auto a = g.LogAudit(kActor, action, "visual_asset", assetId,
                            fmt::format("layer={} {}", layer, detail));
        !a) {
        log::Warn("V0 资产编排：写 audit_logs 失败：{}", a.error().message);
    }
}

// 写资产 status：就绪态（≥REF_READY）不会被写小；非就绪态之间可自由迁移（PENDING↔PROMPTING↔FAILED）
[[nodiscard]] std::expected<std::string, DbError>
SetAssetStatus(NovelVisual& vis, const VisualAssetRow& cur, std::string_view target) {
    const int curRank = StatusRank(cur.status);
    const int tgtRank = StatusRank(target);
    if (tgtRank < curRank && curRank >= 1) {
        return cur.status; // 不降级
    }
    if (std::string_view{cur.status} == target) {
        return cur.status;
    }
    VisualAssetRow next = cur;
    next.status = std::string{target};
    if (auto r = vis.UpsertAsset(next); !r) return std::unexpected(r.error());
    return std::string{target};
}

// 由「已 DONE 的层」重算资产 status（只升不降）；无 DONE 层则保持不变
[[nodiscard]] std::expected<std::string, DbError>
RecomputeAssetStatus(NovelVisual& vis, RowId assetId, const VisualAssetRow& cur) {
    std::string target;
    std::size_t done = 0;
    for (const auto& spec : kChain) {
        const auto a = FindArtifact(vis, assetId, spec.name);
        if (!a || a->status != "DONE") continue;
        ++done;
        target = std::string{spec.readyStatus};
    }
    if (done == kChainN) target = "READY";
    if (target.empty()) return cur.status;
    return SetAssetStatus(vis, cur, target);
}

// 落一条 `visual_artifacts`（挂起= PENDING 行；完成= DONE 行）
[[nodiscard]] std::expected<RowId, DbError>
WriteArtifact(NovelVisual& vis, RowId assetId, const LayerSpec& spec,
              const AssetPipelineOptions& opt, const std::optional<VisualArtifactRow>& existing,
              RowId parentId, std::string_view relPath, std::string_view jobId, bool done,
              bool degraded, std::string_view note) {
    VisualArtifactRow row;
    row.id = existing ? existing->id : 0;
    row.asset_id = assetId;
    row.layer = std::string{spec.name};
    row.chapter_scope = opt.chapter_scope;
    row.chapter_to = opt.chapter_to;
    row.rel_path = std::string{relPath};
    row.parent_artifact_id = parentId;
    row.job_id = std::string{jobId};
    row.status = done ? std::string{"DONE"} : std::string{"PENDING"};
    row.degraded = degraded;
    row.note = std::string{note};
    return vis.UpsertArtifact(row);
}

// 每层提示词（英文，出图模型友好）；派生关系由 `parent_artifact_id` 承载，不混进 prompt
[[nodiscard]] std::string BuildPrompt(const VisualAssetRow& asset, const LayerSpec& spec) {
    const std::string base = asset.base_desc.empty() ? asset.name : asset.base_desc;
    const std::string mats = asset.materials_colors;
    std::string p;
    switch (spec.layer) {
        case AssetLayer::Front:
            p = fmt::format("{}, {} front view portrait, neutral expression, plain background, "
                            "character reference sheet",
                            base, asset.name);
            break;
        case AssetLayer::Turnaround:
            p = fmt::format("character turnaround sheet, four views: front, side, back, "
                            "three-quarter, identical identity, plain background, {}",
                            base);
            break;
        case AssetLayer::BaseBody:
            p = fmt::format("base body sheet, underwear layer, neutral anatomy, locked "
                            "proportions, plain background, {}",
                            base);
            break;
        case AssetLayer::Wardrobe:
            p = fmt::format("wardrobe layer, full outfit, {}, {}", base, mats);
            break;
    }
    if (spec.layer != AssetLayer::Wardrobe && !mats.empty()) {
        p += fmt::format(", {}", mats);
    }
    return p;
}

} // namespace

std::string_view AssetLayerName(AssetLayer layer) noexcept {
    const LayerSpec* spec = SpecOf(layer);
    return spec == nullptr ? std::string_view{} : spec->name;
}

bool IsAssetConsumable(std::string_view status) noexcept { return Consumable(status); }

bool IsAssetDerivable(std::string_view status) noexcept { return StatusRank(status) >= 1; }

std::expected<PipelineResult, DbError> RunAssetLayer(db::sqlite::Database& db, RowId assetId,
                                                    AssetLayer layer,
                                                    const AssetPipelineOptions& opt) {
    const LayerSpec* spec = SpecOf(layer);
    if (spec == nullptr) return std::unexpected(PErr("未知的形象层"));
    if (assetId <= 0) return std::unexpected(PErr("资产 id 必须 > 0"));

    NovelVisual vis(db);
    auto assetR = vis.GetAsset(assetId);
    if (!assetR) return std::unexpected(assetR.error());
    VisualAssetRow asset = *assetR;

    const auto existing = FindArtifact(vis, assetId, spec->name);

    PipelineResult out;
    out.status = asset.status;

    // —— 幂等：本层已 DONE 直接复用（`11` §2.6.1 S4：上游未变不重生成）——
    if (existing && existing->status == "DONE") {
        auto st = RecomputeAssetStatus(vis, assetId, asset);
        if (!st) return std::unexpected(st.error());
        out.status = *st;
        auto rows = vis.ListArtifacts(assetId);
        if (!rows) return std::unexpected(rows.error());
        out.artifacts = std::move(*rows);
        out.artifact_id = existing->id;
        out.outcome = existing->degraded ? PipelineOutcome::Degraded : PipelineOutcome::Ready;
        return out;
    }

    // —— 前置层（派生链的边）——
    std::optional<VisualArtifactRow> parent;
    bool depReady = true;
    if (!spec->parent.empty()) {
        parent = FindArtifact(vis, assetId, spec->parent);
        depReady = parent.has_value() && parent->status == "DONE";
    }

    bool degraded = false;
    if (!depReady) {
        // 策略 C（`11` §2.7）：挂起本层、先跑无依赖的资产；挂起时长跨调用累计在 artifact.note
        const std::int64_t seen = existing ? ParseSuspendedSince(existing->note) : 0;
        const std::int64_t now = NowMs();
        const std::int64_t start = seen > 0 ? seen : now;
        const std::int64_t waited = now - start;
        const bool expired = opt.suspendTimeoutMs <= 0 || waited >= opt.suspendTimeoutMs;
        if (!opt.allowDegrade || !expired) {
            const std::string note = fmt::format("suspended_since={} dep={} waited={}ms",
                                                 start, spec->parent, waited);
            auto wid = WriteArtifact(vis, assetId, *spec, opt, existing,
                                     parent ? parent->id : 0, {}, {}, false, false, note);
            if (!wid) return std::unexpected(wid.error());
            out.artifact_id = *wid;
            out.outcome = PipelineOutcome::Suspended;
            out.detail = fmt::format(
                "前置层 {} 未就绪 → 按策略 C 挂起（waited={}ms 阈值={}ms allowDegrade={}）",
                spec->parent, waited, opt.suspendTimeoutMs, opt.allowDegrade ? 1 : 0);
            out.notes.push_back(fmt::format("asset#{} {}：{}", assetId, spec->name, out.detail));
            LogAuditQuiet(db, "asset_suspend", assetId, spec->name, out.detail);
            log::Info("V0 资产挂起：asset#{} {} — {}", assetId, spec->name, out.detail);
            auto rows = vis.ListArtifacts(assetId);
            if (!rows) return std::unexpected(rows.error());
            out.artifacts = std::move(*rows);
            return out;
        }
        degraded = true; // 超期 → 策略 B：无参考图（纯文生图）兜底
    }

    // —— 出图：PROMPTING → 调用既有出图任务账（generated_images）——
    if (StatusRank(asset.status) == 0) {
        auto st = SetAssetStatus(vis, asset, "PROMPTING");
        if (!st) return std::unexpected(st.error());
        asset.status = *st;
        out.status = asset.status;
    }

    ImageJobInput in;
    in.prompt = BuildPrompt(asset, *spec);
    in.negative = std::string{kDefaultNegative};
    in.asset_id = assetId;
    in.width = opt.width;
    in.height = opt.height;
    in.steps = opt.steps;
    in.model = opt.model;

    auto job = RunImageJob(db, in);
    if (!job) {
        (void)SetAssetStatus(vis, asset, "FAILED");
        out.outcome = PipelineOutcome::Failed;
        out.detail = job.error().message;
        out.notes.push_back(
            fmt::format("asset#{} {}：出图失败（{}）", assetId, spec->name, job.error().message));
        LogAuditQuiet(db, "asset_failed", assetId, spec->name, out.detail);
        log::Error("V0 资产出图失败：asset#{} {} — {}", assetId, spec->name, out.detail);
        auto rows = vis.ListArtifacts(assetId);
        if (rows) out.artifacts = std::move(*rows);
        return out;
    }

    // —— 完成记账：派生边（parent_artifact_id）+ job_id + degraded ——
    std::string note;
    if (degraded) {
        note = fmt::format(
            "degraded: 前置层 {} 未就绪且挂起超期，按策略 B 用纯文生图兜底（11 §2.7 W1）",
            spec->parent);
    }
    auto wid = WriteArtifact(vis, assetId, *spec, opt, existing,
                             (parent && parent->status == "DONE") ? parent->id : 0, job->rel_path,
                             job->job_id, true, degraded, note);
    if (!wid) return std::unexpected(wid.error());
    out.artifact_id = *wid;

    if (degraded) {
        out.outcome = PipelineOutcome::Degraded;
        out.notes.push_back(fmt::format("asset#{} {}：{}", assetId, spec->name, note));
        LogAuditQuiet(db, "asset_degrade", assetId, spec->name, note);
        log::Warn("V0 资产降级：asset#{} {} — {}", assetId, spec->name, note);
    } else {
        out.outcome = PipelineOutcome::Ready;
    }

    auto st = RecomputeAssetStatus(vis, assetId, asset);
    if (!st) return std::unexpected(st.error());
    out.status = *st;

    auto rows = vis.ListArtifacts(assetId);
    if (!rows) return std::unexpected(rows.error());
    out.artifacts = std::move(*rows);
    log::Info("V0 资产层完成：asset#{} {} → {}（job={} degraded={}）", assetId, spec->name,
              out.status, job->job_id, degraded ? 1 : 0);
    return out;
}

std::expected<PipelineResult, DbError> RunAssetPipeline(db::sqlite::Database& db, RowId assetId,
                                                       const AssetPipelineOptions& opt) {
    const NovelVisual vis(db);
    auto assetR = vis.GetAsset(assetId);
    if (!assetR) return std::unexpected(assetR.error());

    PipelineResult out;
    out.status = assetR->status;

    if (assetR->status == "READY") {
        out.outcome = PipelineOutcome::Ready;
        auto rows = vis.ListArtifacts(assetId);
        if (!rows) return std::unexpected(rows.error());
        out.artifacts = std::move(*rows);
        return out;
    }

    bool degradedAny = false;
    for (const auto& spec : kChain) {
        auto r = RunAssetLayer(db, assetId, spec.layer, opt);
        if (!r) return std::unexpected(r.error());
        out.notes.insert(out.notes.end(), r->notes.begin(), r->notes.end());
        out.status = r->status;
        out.artifact_id = r->artifact_id;
        out.artifacts = r->artifacts;
        if (r->outcome == PipelineOutcome::Degraded) {
            degradedAny = true;
            continue;
        }
        if (r->outcome != PipelineOutcome::Ready) {
            out.outcome = r->outcome; // Suspended / Failed：链停在此层
            out.detail = fmt::format("链在 {} 层停止：{}", spec.name, r->detail);
            return out;
        }
    }

    if (out.status != "READY") {
        out.outcome = PipelineOutcome::Suspended;
        out.detail = fmt::format("四层均完成但资产 status={}（预期 READY）", out.status);
        return out;
    }
    out.outcome = degradedAny ? PipelineOutcome::Degraded : PipelineOutcome::Ready;
    return out;
}

std::string DegradedSummaryJson(const std::vector<VisualArtifactRow>& artifacts) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) return "{}";
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "items", arr);
    int count = 0;
    for (const auto& a : artifacts) {
        if (!a.degraded) continue;
        ++count;
        yyjson_mut_val* o = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, o, "asset_id", a.asset_id);
        yyjson_mut_obj_add_strncpy(doc, o, "layer", a.layer.data(), a.layer.size());
        yyjson_mut_obj_add_strncpy(doc, o, "note", a.note.data(), a.note.size());
        yyjson_mut_arr_add_val(arr, o);
    }
    yyjson_mut_obj_add_int(doc, root, "count", count);
    std::size_t len = 0;
    char* text = yyjson_mut_val_write(root, 0, &len);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) return "{}";
    std::string out{text, len};
    std::free(text);
    return out;
}

bool RunAssetPipelineSelfCheck() {
    if (!NovelDb::RunSchemaSelfCheck()) {
        log::Error("V0 资产编排自检：schema 自检不通过");
        return false;
    }

    namespace fs = std::filesystem;

    // 用**真实 schema**（一本小说一个 novel.db）跑，不另抄一份建表 SQL
    auto& ndb = NovelDb::Instance();
    const bool openedHere = !ndb.isOpen();
    fs::path root;
    if (openedHere) {
        root = fs::temp_directory_path() / util::PathFromUtf8("shine_asset_pipeline_check");
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        if (auto r = ndb.Open(root / "novel.db"); !r) {
            log::Error("V0 资产编排自检：打开临时工程库失败 {}", r.error().message);
            return false;
        }
    } else {
        log::Info("V0 资产编排自检：复用当前已打开的工程库");
    }
    db::sqlite::Database& db = ndb.raw();

    const AppSettings saved = Settings();
    Settings().imageBackend = "mock";
    Settings().imageOutputRelDir = "visual/gen";
    Settings().imageModel = "mock-model";

    const bool bodyOk = [&]() -> bool {
        NovelGraph g(db);
        NovelVisual v(db);

        auto person = g.UpsertEntity({.kind = std::string{kind::person}, .name = "林默"});
        if (!person) {
            log::Error("V0 资产编排自检：建实体失败 {}", person.error().message);
            return false;
        }

        AssetPipelineOptions opt;
        opt.width = 128;
        opt.height = 128;
        opt.steps = 2;
        opt.model = "mock-model";
        opt.suspendTimeoutMs = 10 * 60 * 1000;

        // —— 1) 全链 PENDING → READY ——
        auto assetA = v.UpsertAsset({.entity_id = *person,
                                     .kind = "character",
                                     .name = "林默",
                                     .base_desc = "young man, black hair, dark eyes",
                                     .materials_colors = "dark coat"});
        if (!assetA) {
            log::Error("V0 资产编排自检：建资产失败 {}", assetA.error().message);
            return false;
        }
        auto a0 = v.GetAsset(*assetA);
        if (!a0 || a0->status != "PENDING") {
            log::Error("V0 资产编排自检：新资产应为 PENDING，实际={}", a0 ? a0->status : "?");
            return false;
        }
        if (IsAssetConsumable("PENDING") || IsAssetDerivable("PENDING")) {
            log::Error("V0 资产编排自检：PENDING 不应可被消费/派生");
            return false;
        }
        if (!IsAssetDerivable("REF_READY") || IsAssetConsumable("REF_READY")) {
            log::Error("V0 资产编排自检：REF_READY 应可派生、不可作分镜参考图");
            return false;
        }

        auto chain = RunAssetPipeline(db, *assetA, opt);
        if (!chain) {
            log::Error("V0 资产编排自检：全链失败 {}", chain.error().message);
            return false;
        }
        if (chain->outcome != PipelineOutcome::Ready || chain->status != "READY") {
            log::Error("V0 资产编排自检：全链应 READY，实际 outcome={} status={}",
                       static_cast<int>(chain->outcome), chain->status);
            return false;
        }
        if (chain->artifacts.size() != kChainN) {
            log::Error("V0 资产编排自检：应有 {} 层产物，实际 {}", kChainN, chain->artifacts.size());
            return false;
        }
        // 派生链父子 + job/落盘记账
        RowId prevId = 0;
        for (const auto& spec : kChain) {
            const auto* art = FindIn(chain->artifacts, spec.name);
            if (art == nullptr || art->status != "DONE" || art->degraded) {
                log::Error("V0 资产编排自检：层 {} 应 DONE 且未降级", spec.name);
                return false;
            }
            if (art->job_id.empty() || art->rel_path.empty()) {
                log::Error("V0 资产编排自检：层 {} 缺 job_id/rel_path", spec.name);
                return false;
            }
            if (art->parent_artifact_id != prevId) {
                log::Error("V0 资产编排自检：层 {} 的父产物应为 {}，实际 {}", spec.name, prevId,
                           art->parent_artifact_id);
                return false;
            }
            prevId = art->id;
        }
        auto jobs = ListGeneratedImages(db, 50);
        if (!jobs || jobs->size() != kChainN) {
            log::Error("V0 资产编排自检：generated_images 应有 {} 行，实际 {}", kChainN,
                       jobs ? jobs->size() : 0);
            return false;
        }
        if (!IsAssetConsumable(chain->status)) {
            log::Error("V0 资产编排自检：READY 应可被消费");
            return false;
        }

        // —— 2) 幂等：重跑不重新出图 ——
        auto again = RunAssetPipeline(db, *assetA, opt);
        if (!again || again->outcome != PipelineOutcome::Ready ||
            again->artifacts.size() != kChainN) {
            log::Error("V0 资产编排自检：幂等重跑未复用");
            return false;
        }
        jobs = ListGeneratedImages(db, 50);
        if (!jobs || jobs->size() != kChainN) {
            log::Error("V0 资产编排自检：幂等重跑不应新增出图任务（实际 {}）",
                       jobs ? jobs->size() : 0);
            return false;
        }

        // —— 3) 策略 C：前置缺失 → 挂起（且未超期不降级、不产生出图任务）——
        auto assetB = v.UpsertAsset({.entity_id = *person, .kind = "character", .name = "苏晚"});
        if (!assetB) return false;
        auto sus = RunAssetLayer(db, *assetB, AssetLayer::Wardrobe, opt);
        if (!sus || sus->outcome != PipelineOutcome::Suspended) {
            log::Error("V0 资产编排自检：缺 base_body 前置应挂起");
            return false;
        }
        auto b0 = v.GetAsset(*assetB);
        if (!b0 || b0->status != "PENDING") {
            log::Error("V0 资产编排自检：挂起不应改变资产 status，实际={}", b0 ? b0->status : "?");
            return false;
        }
        const auto* susRow = FindIn(sus->artifacts, "wardrobe");
        if (susRow == nullptr || susRow->status != "PENDING" ||
            susRow->note.find("suspended_since=") == std::string::npos) {
            log::Error("V0 资产编排自检：挂起应落 PENDING 行并记 suspended_since");
            return false;
        }
        auto sus2 = RunAssetLayer(db, *assetB, AssetLayer::Wardrobe, opt);
        if (!sus2 || sus2->outcome != PipelineOutcome::Suspended) {
            log::Error("V0 资产编排自检：未超期应继续挂起");
            return false;
        }
        jobs = ListGeneratedImages(db, 50);
        if (!jobs || jobs->size() != kChainN) {
            log::Error("V0 资产编排自检：挂起不应产生出图任务（实际 {}）",
                       jobs ? jobs->size() : 0);
            return false;
        }

        // —— 4) 策略 B：超期 → 降级出图并记账 ——
        AssetPipelineOptions force = opt;
        force.suspendTimeoutMs = 0; // 不等待，立即降级
        auto deg = RunAssetLayer(db, *assetB, AssetLayer::Wardrobe, force);
        if (!deg || deg->outcome != PipelineOutcome::Degraded) {
            log::Error("V0 资产编排自检：超期应降级");
            return false;
        }
        const auto* degRow = FindIn(deg->artifacts, "wardrobe");
        if (degRow == nullptr || degRow->status != "DONE" || !degRow->degraded ||
            degRow->note.find("degraded") == std::string::npos) {
            log::Error("V0 资产编排自检：降级应落 DONE 行且 degraded=1 + note");
            return false;
        }
        if (deg->status != "WARDROBE_READY") {
            log::Error("V0 资产编排自检：降级后资产应 WARDROBE_READY，实际 {}", deg->status);
            return false;
        }
        const std::string degJson = DegradedSummaryJson(deg->artifacts);
        if (degJson.find("\"wardrobe\"") == std::string::npos ||
            degJson.find("\"count\":1") == std::string::npos) {
            log::Error("V0 资产编排自检：降级清单 JSON 不符：{}", degJson);
            return false;
        }
        jobs = ListGeneratedImages(db, 50);
        if (!jobs || jobs->size() != kChainN + 1) {
            log::Error("V0 资产编排自检：降级应产生 1 个出图任务（实际 {}）",
                       jobs ? jobs->size() : 0);
            return false;
        }

        // —— 5) 严格模式：allowDegrade=false 时超期也不降级 ——
        auto assetC = v.UpsertAsset({.entity_id = *person, .kind = "character", .name = "阿岩"});
        if (!assetC) return false;
        AssetPipelineOptions strict = force;
        strict.allowDegrade = false;
        auto st3 = RunAssetLayer(db, *assetC, AssetLayer::Wardrobe, strict);
        if (!st3 || st3->outcome != PipelineOutcome::Suspended ||
            st3->detail.find("allowDegrade") == std::string::npos) {
            log::Error("V0 资产编排自检：严格模式应挂起且说明 allowDegrade");
            return false;
        }

        // —— 6) 失败路径：comfy 占位后端（不发网络）→ FAILED ——
        auto assetD = v.UpsertAsset({.entity_id = *person, .kind = "character", .name = "裴照"});
        if (!assetD) return false;
        Settings().imageBackend = "comfy";
        auto bad = RunAssetLayer(db, *assetD, AssetLayer::Front, opt);
        Settings().imageBackend = "mock";
        if (!bad || bad->outcome != PipelineOutcome::Failed) {
            log::Error("V0 资产编排自检：comfy 占位后端应失败");
            return false;
        }
        auto d0 = v.GetAsset(*assetD);
        if (!d0 || d0->status != "FAILED") {
            log::Error("V0 资产编排自检：失败后资产应 FAILED，实际={}", d0 ? d0->status : "?");
            return false;
        }
        (void)AssetLayerName(AssetLayer::BaseBody);
        return true;
    }();

    Settings() = saved;
    if (openedHere) {
        ndb.Close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    if (!bodyOk) return false;
    log::Info("V0 资产编排自检通过（PENDING→READY / 派生链父子 / 幂等复用 / C 挂起 / B 降级记账 / "
              "严格模式 / FAILED）");
    return true;
}

} // namespace shine::novelcore
