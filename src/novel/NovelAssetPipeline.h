#pragma once
// V0 ASSET_PIPELINE 编排骨架：正脸 → 四视图 → 基础身体 → 服装
// 规格：Doc/小说系统/11 §2.6（生产状态机 + 派生链）、§2.7（依赖等待 C 为主 + B 兜底）。
// 契约：02 §2.14（VisualArtifact）、03 §2.3（ASSET_PIPELINE 进入/退出条件）。
//
// 本 S 的边界（明确不做）：
//   · 不接真 Comfy —— 出图走既有 `NovelImageStore::RunImageJob`（后端仍由 Settings.imageBackend 选，
//     mock 即可跑通），派生链的参考图经 `parent_artifact_id` 记账，等 comfy 后端就绪再消费；
//   · 不做队列与优先级（`11` §2.7 W5 → S5）；不做 `input_state_hash` 复用比对（→ S4）；
//   · 不做上游变化后的 `STALE` 传播（`11` §2.6.1 S3）。
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"
#include "novel/NovelVisual.h"

namespace shine::novelcore {

// 派生链的层（`11` §2.6.2）。枚举顺序 = 编排推进顺序。
enum class AssetLayer { Front, Turnaround, BaseBody, Wardrobe };

// 层名 ↔ 库内文案：front / turnaround / base_body / wardrobe（与 `NovelVisual::ListArtifacts`
// 的排序口径一致）。未知枚举返回空串。
[[nodiscard]] std::string_view AssetLayerName(AssetLayer layer) noexcept;

// 下游能否把它当**分镜参考图**用（`11` §2.6.1 S1：只以生产态判断，不等 canon_status）。
// SHEET_READY / WARDROBE_READY / READY → true（四视图就绪即锁定基础身体）。
[[nodiscard]] bool IsAssetConsumable(std::string_view status) noexcept;

// 下游能否**继续派生**（正脸就绪即可派生四视图，`11` §2.6.1 S2）。
// REF_READY 及以上 → true。
[[nodiscard]] bool IsAssetDerivable(std::string_view status) noexcept;

struct AssetPipelineOptions {
    RowId chapter_scope = 0; // 0 = 不限章（`08` 分章语义）
    RowId chapter_to = 0;
    int width = 0; // 0 = Settings
    int height = 0;
    int steps = 0;
    std::string model;

    // 挂起超期阈值（毫秒，`11` §2.7 W3）：策略 C 挂起超过它 → 策略 B 降级为纯文生图。
    // <= 0 = 不等待、立即降级（自检 / 强制出图用）。
    std::int64_t suspendTimeoutMs = 30 * 60 * 1000;

    // false = 严格模式：缺依赖一律挂起（超期也不降级）。
    bool allowDegrade = true;
};

enum class PipelineOutcome {
    Ready,     // 本层/全链就绪（无降级）
    Suspended, // 策略 C：前置未就绪，挂起回队列
    Degraded,  // 策略 B：超期降级出图，已记 degraded=1
    Failed,    // 出图失败或数据错误
};

struct PipelineResult {
    PipelineOutcome outcome = PipelineOutcome::Failed;
    std::string status;             // 资产当前 status（`11` §2.6.1 的 8 值）
    RowId artifact_id = 0;          // 本次涉及的 `visual_artifacts` 行（挂起时是 PENDING 行）
    std::vector<VisualArtifactRow> artifacts; // 该资产全部层（派生序）
    std::vector<std::string> notes; // 记账（`11` §2.7 W2「降级必须可见」）
    std::string detail;             // 挂起原因 / 失败文案
};

// 推进**单层**。幂等：该层已 DONE 直接复用，不重新出图（`11` §2.6.1 S4）。
// 仅 worker 调用（写库 + 阻塞出图）。
[[nodiscard]] std::expected<PipelineResult, DbError>
RunAssetLayer(db::sqlite::Database& db, RowId assetId, AssetLayer layer,
              const AssetPipelineOptions& opt = {});

// 推进**整条链**到 READY（PENDING → PROMPTING → REF_READY → SHEET_READY → WARDROBE_READY → READY）。
// 任一层挂起即整体返回 Suspended（策略 C：该资产回队列，先跑无依赖的资产/镜）；降级则继续推进。
[[nodiscard]] std::expected<PipelineResult, DbError>
RunAssetPipeline(db::sqlite::Database& db, RowId assetId,
                 const AssetPipelineOptions& opt = {});

// 降级清单 JSON（`11` §2.7 W1/W2：进章级 cost_report / stop_report）。
// 形状：{"items":[{"asset_id":1,"layer":"wardrobe","note":"…"}],"count":N}
[[nodiscard]] std::string DegradedSummaryJson(const std::vector<VisualArtifactRow>& artifacts);

// 离线自检（不发网络）：mock 后端跑通 PENDING→READY、派生链父子、幂等、
// C 挂起（含未超期不降级）、超期 B 降级并记账、严格模式拒绝降级、失败路径 FAILED。
[[nodiscard]] bool RunAssetPipelineSelfCheck();

} // namespace shine::novelcore
