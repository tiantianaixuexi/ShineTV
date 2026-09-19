#pragma once
// shine::video —— **叙事分镜 → 生成分镜** 的桥（`11` §2.5，G12 的那座桥）
//
//   NarrativeShot（`02` §2.7，叙事：怎么拍）  →【本文件】→  GenShot / VideoProject（`02` §2.8，生成：怎么交给 Comfy）
//
// 三条纪律：
//   1. **纯函数**：同输入同输出 —— 不读时钟、不用随机；`seed` 由**稳定键派生**（`11` §2.5 明确"禁止纯随机"）。
//   2. **不依赖 novelcore**：`src/video` 与 `src/novel` 至今零引用（`11` §3 的 11-1 就是"缺这座桥"），
//      所以输入是**只读 DTO**；读表、把 `PromptArtifact.references` 解析成 `visual_assets.sheet_rel_path`
//      这类工作由小说侧（或 MCP/Agent）做完再传进来。
//   3. **产出必须先过 K20/K21**（`06` §2.3）：宽高 32 对齐、帧数 `n % 17 == 5`、参考图 ≤ 9。
//      纠正规则**不重复实现** —— 唯一来源是 `VideoProject::Sanitize()`；本文件只做「纠正前 → 纠正后」的**差异记账**。
//
// 本文件的边界：**不自动出图**。它只产出"可提交的 `VideoProject`"，提交归 `VideoTaskRunner`（S5 的队列）。
#include "video/GenerationLedger.h"
#include "video/VideoProject.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shine::video {

// 小说侧「一镜」的只读输入（`02` §2.7 `NarrativeShot` 的视觉相关子集 + **已解析好的素材路径**）。
struct NarrativeShotInput {
    std::string shotId; // `NarrativeShot.shot_id`（稳定键：seed 由它派生）
    int ord = 0;        // 场景内序号（1 基；0 = 未给，按数组顺序推）
    // `11` §2.5 明确：prompt 必须来自 **PromptArtifact**，**不得**直接用叙事 `prompt_text`
    std::string prompt;
    std::string negativePrompt;                 // PromptArtifact.negative
    std::string firstFramePath;                 // 该镜的分镜图（未生成 → 空；相对 `mediaLibraryDir`）
    std::vector<std::string> referenceImages;   // PromptArtifact.references → `visual_assets.sheet_rel_path`
    std::vector<std::string> characterAssetPaths; // `start_state.characters` 对应的视觉资产
    std::int64_t seed = -1;                     // ≥0 = 调用方指定；-1 = 由稳定键派生（产出**永不**留 -1）
    int width = 0;                              // 0 = 用 `sceneTemplate.sceneWidth`
    int height = 0;                             // 0 = 用 `sceneTemplate.sceneHeight`
    int length = 0;                             // 0 = `kDefaultLength`
    int steps = 0;                              // 0 = `sceneTemplate.sceneSteps`
    double cfg = 0.0;                           // ≤0 = `sceneTemplate.sceneCfg`
    double denoise = 0.0;                       // ≤0 = `sceneTemplate.sceneDenoise`
    double shift = 0.0;                         // ≤0 = `kDefaultShift`
};

struct NarrativeSceneInput {
    std::string sceneTitle; // 用于 title 与 seed 键
    std::vector<NarrativeShotInput> shots;
};

// `06` §2.3 的机器校验记录（本桥只产出 K20/K21 两条）
struct GenCheckIssue {
    std::string checkId;  // "K20" | "K21"
    std::string severity; // "high" | "low"
    std::string detail;   // 中文，可直接显示
};

struct ToGenShotOptions {
    // K21（参考图 ≤ 9）：false = 超出即**截断并记 high**（默认，产出仍可提交，账里看得见）；
    // true = 直接拒绝产出（调用方要求"宁可不出也不降级"时用）。
    bool strictRefLimit = false;
};

struct ToGenShotResult {
    bool ok = false;         // 能否产出可提交的 `VideoProject`
    std::string error;       // ok=false 时的中文原因（**不产半成品**）
    VideoProject project;    // `sceneTemplate` 的拷贝（模型段 / scene* / 采样参数全继承）+ 本场景的 shots
    std::vector<GenCheckIssue> issues;               // K20（low，已自动纠正）/ K21（high）
    std::vector<GenerationDegradation> degradations; // 降级账口径（`11` §2.7 W2「降级必须可见」）
};

// 转换（纯函数）。`sceneTemplate` 提供 `scene*` 采样参数与宽高；为空/0 的字段回落到分镜默认值。
[[nodiscard]] ToGenShotResult ToGenShot(const VideoProject& sceneTemplate,
                                        const NarrativeSceneInput& scene,
                                        const ToGenShotOptions& options = {});

// 稳定种子（`11` §2.5：**禁止纯随机**）：FNV-1a(`sceneTitle|shotId|ord`) → `[0, 2^63)`。
// 同一镜无论编译多少次都是同一个种子；不同镜几乎不会撞。
[[nodiscard]] std::int64_t StableShotSeed(std::string_view sceneTitle, std::string_view shotId,
                                          int ord) noexcept;

// 离线自检（`SHINE_SCENE_IMAGE_CHECK` 会连带跑）：确定性 / K20·K21 记账 / mode / 链式 / seed / 不产半成品。
// 返回 fail 条数（0 = 全过）。
[[nodiscard]] int RunNovelShotBridgeSelfCheck();

} // namespace shine::video
