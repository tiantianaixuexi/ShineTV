#include "video/NovelShotBridge.h"

#include "core/Log.h"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace shine::video {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void FnvFeed(std::uint64_t& hash, std::string_view text) noexcept {
    for (const char ch : text) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= kFnvPrime;
    }
}

// 标题（`11` §2.5：`NarrativeShot.shot_id` + 场景标题）
[[nodiscard]] std::string ShotTitle(const NarrativeShotInput& shot, std::string_view sceneTitle,
                                    std::size_t index) {
    std::string key = shot.shotId;
    if (key.empty()) {
        const int ord = shot.ord > 0 ? shot.ord : static_cast<int>(index) + 1;
        key = fmt::format("分镜 #{}", ord);
    }
    return sceneTitle.empty() ? key : fmt::format("{} · {}", key, sceneTitle);
}

// 该镜的序号（`ord` 未给时按数组顺序推，用于 seed 与日志）
[[nodiscard]] int OrdOf(const NarrativeShotInput& shot, std::size_t index) noexcept {
    return shot.ord > 0 ? shot.ord : static_cast<int>(index) + 1;
}

[[nodiscard]] bool HasIssue(const std::vector<GenCheckIssue>& list, std::string_view checkId,
                           std::string_view severity) {
    for (const GenCheckIssue& item : list) {
        if (item.checkId == checkId && item.severity == severity) {
            return true;
        }
    }
    return false;
}

// Sanitize() 前后的快照（纠正规则的唯一来源是 `VideoProject::Sanitize()`，这里只记账）
struct ShotBefore {
    int width = 0;
    int height = 0;
    int length = 0;
    std::size_t refCount = 0;
};

} // namespace

std::int64_t StableShotSeed(std::string_view sceneTitle, std::string_view shotId, int ord) noexcept {
    std::uint64_t hash = kFnvOffset;
    FnvFeed(hash, sceneTitle);
    FnvFeed(hash, "|");
    FnvFeed(hash, shotId);
    FnvFeed(hash, "|");
    // 序号直接按字节喂进去（避免为拼串分配内存 —— 本函数是 `noexcept`）
    hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(ord));
    hash *= kFnvPrime;
    return static_cast<std::int64_t>(hash & 0x7fffffffffffffffULL);
}

ToGenShotResult ToGenShot(const VideoProject& sceneTemplate, const NarrativeSceneInput& scene,
                          const ToGenShotOptions& options) {
    ToGenShotResult out;
    out.project = sceneTemplate; // 模型段 / scene* / 采样参数全部继承
    out.project.shots.clear();
    out.project.shots.reserve(scene.shots.size());

    // ———— ① 映射（**不在这里纠正/截断**：纠正统一交给 Sanitize，避免第二套对齐规则）————
    for (std::size_t i = 0; i < scene.shots.size(); ++i) {
        const NarrativeShotInput& src = scene.shots[i];
        if (src.prompt.empty()) {
            // 「不产半成品」：宁可整场失败，也不产出一个"能跑但提示词是错的"的分镜
            out.error = fmt::format(
                "第 {} 镜「{}」没有生成提示词（PromptArtifact 未就绪）：不得用叙事 prompt_text 顶替（11 §2.5）",
                i + 1, src.shotId.empty() ? "?" : src.shotId);
            out.project.shots.clear();
            return out;
        }

        Shot shot;
        shot.title = ShotTitle(src, scene.sceneTitle, i);
        shot.prompt = src.prompt;
        shot.firstFramePath = src.firstFramePath;
        shot.referenceImages = src.referenceImages;
        shot.characterAssetPaths = src.characterAssetPaths;
        // mode（`11` §2.5）：有首帧图 → fl2va（硬锚定）；否则 ref2va
        shot.mode = src.firstFramePath.empty() ? ShotMode::Reference : ShotMode::FirstLastFrame;
        // chainFromPrevious（`11` §2.5「同场景内非首镜 → true」）**加一条修正**：
        // 该镜自己有首帧图时**不接链式** —— 否则 H3 编译器按"链式优先"忽略首帧图（S5 的
        // `first_frame_ignored` 降级），等于自己跟自己打架。
        shot.chainFromPrevious = i > 0 && src.firstFramePath.empty();
        if (i > 0 && !src.firstFramePath.empty()) {
            log::Info("桥：第 {} 镜自带首帧图 → 不接链式（否则首帧图会被链式盖掉）", i + 1);
        }

        shot.width = src.width > 0 ? src.width
                                   : (sceneTemplate.sceneWidth > 0 ? sceneTemplate.sceneWidth : kDefaultWidth);
        shot.height = src.height > 0 ? src.height
                                     : (sceneTemplate.sceneHeight > 0 ? sceneTemplate.sceneHeight
                                                                      : kDefaultHeight);
        shot.length = src.length > 0 ? src.length : kDefaultLength;
        shot.steps = src.steps > 0 ? src.steps
                                   : (sceneTemplate.sceneSteps > 0 ? sceneTemplate.sceneSteps : kDefaultSteps);
        shot.cfg = src.cfg > 0.0 ? src.cfg
                                 : (sceneTemplate.sceneCfg > 0.0 ? sceneTemplate.sceneCfg : kDefaultCfg);
        shot.denoise = src.denoise > 0.0
                           ? src.denoise
                           : (sceneTemplate.sceneDenoise > 0.0 ? sceneTemplate.sceneDenoise : kDefaultDenoise);
        shot.shift = src.shift > 0.0 ? src.shift : kDefaultShift;
        // seed：调用方给了就用；否则由**稳定键**派生（`11` §2.5 禁止纯随机 → 永不留下 -1）
        shot.seed = src.seed >= 0 ? src.seed : StableShotSeed(scene.sceneTitle, src.shotId, OrdOf(src, i));
        out.project.shots.push_back(std::move(shot));
    }

    // ———— ② K20 / K21：跑一次 Sanitize，把「纠正前 → 纠正后」的差异记成 issue + 降级账 ————
    std::vector<ShotBefore> before;
    before.reserve(out.project.shots.size());
    for (const Shot& shot : out.project.shots) {
        before.push_back({shot.width, shot.height, shot.length, shot.referenceImages.size()});
    }
    (void)out.project.Sanitize(); // 幂等；已纠正过的工程再调返回 false

    for (std::size_t i = 0; i < out.project.shots.size(); ++i) {
        const Shot& shot = out.project.shots[i];
        const ShotBefore& was = before[i];
        if (shot.width != was.width || shot.height != was.height) {
            const std::string detail =
                fmt::format("第 {} 镜宽高已对齐到 {} 的倍数：{}x{} → {}x{}", i + 1, kSizeMultiple, was.width,
                            was.height, shot.width, shot.height);
            out.issues.push_back({"K20", "low", detail});
            out.degradations.push_back({std::string{kDegradeSizeAligned}, detail, i});
        }
        if (shot.length != was.length) {
            const std::string detail =
                fmt::format("第 {} 镜帧数已对齐到 n%{}=={}：{} → {}", i + 1, kFrameGridStride,
                            kFrameGridOffset, was.length, shot.length);
            out.issues.push_back({"K20", "low", detail});
            out.degradations.push_back({std::string{kDegradeSizeAligned}, detail, i});
        }
        if (shot.referenceImages.size() < was.refCount) {
            const std::string detail =
                fmt::format("第 {} 镜参考图 {} 张 → {} 张（超出 H3 上限，截断末尾）", i + 1, was.refCount,
                            shot.referenceImages.size());
            out.issues.push_back({"K21", "high", detail});
            out.degradations.push_back({std::string{kDegradeRefTruncated}, detail, i});
            if (options.strictRefLimit) {
                out.error = detail + "；strictRefLimit=true → 拒绝产出";
                out.project.shots.clear();
                return out;
            }
        }
    }

    out.ok = true;
    return out;
}

int RunNovelShotBridgeSelfCheck() {
    int fail = 0;
    const auto expect = [&](bool cond, std::string_view name) {
        if (cond) {
            log::Info("S6 bridge PASS {}", name);
        } else {
            ++fail;
            log::Error("S6 bridge FAIL {}", name);
        }
    };

    VideoProject scene;
    scene.name = "桥自检工程";
    scene.sceneWidth = 1300;  // 非 32 倍数 → K20 会纠正
    scene.sceneHeight = 1000; // 非 32 倍数 → K20 会纠正
    scene.sceneSteps = 30;
    scene.sceneCfg = 5.5;
    scene.sceneDenoise = 0.8;
    scene.sceneCheckpoint = "sd15_selfcheck.safetensors";

    NarrativeShotInput first;
    first.shotId = "sh_001";
    first.ord = 1;
    first.prompt = "a girl standing in the rain";
    first.referenceImages = {"characters/lin.png"};

    NarrativeShotInput second;
    second.shotId = "sh_002";
    second.ord = 2;
    second.prompt = "she looks up at the sky";
    second.firstFramePath = "scene/shine_00001.png";

    NarrativeSceneInput in;
    in.sceneTitle = "第 1 场";
    in.shots = {first, second};

    // —— ① 纯函数 + 采样参数继承 + K20 ——
    const ToGenShotResult a = ToGenShot(scene, in);
    const ToGenShotResult b = ToGenShot(scene, in);
    expect(a.ok && b.ok, "转换成功");
    expect(a.project.ToJson() == b.project.ToJson(), "纯函数：同输入同输出（逐字节一致）");
    expect(a.project.shots.size() == 2, "两镜都产出");
    expect(a.project.shots[0].steps == 30 && a.project.shots[0].cfg == 5.5 &&
               a.project.shots[0].denoise == 0.8,
           "采样参数取 VideoProject.scene*（steps/cfg/denoise）");
    expect(a.project.shots[0].width == 1312 && a.project.shots[0].height == 1024 &&
               a.project.shots[0].length == 107,
           "K20：宽高 1300x1000 → 1312x1024、帧数 → 107");
    expect(HasIssue(a.issues, "K20", "low") && HasDegradation(a.degradations, kDegradeSizeAligned),
           "K20 纠正已记账（severity=low + size_aligned 降级）");

    // —— ② mode / chainFromPrevious ——
    expect(a.project.shots[0].mode == ShotMode::Reference &&
               a.project.shots[1].mode == ShotMode::FirstLastFrame,
           "mode：无首帧 → ref2va；有首帧 → fl2va");
    expect(!a.project.shots[0].chainFromPrevious, "第 1 镜不接链式");
    expect(!a.project.shots[1].chainFromPrevious,
           "第 2 镜自带首帧图 → **不接链式**（否则首帧会被链式盖掉）");
    NarrativeSceneInput chained = in;
    chained.shots[1].firstFramePath.clear();
    const ToGenShotResult c = ToGenShot(scene, chained);
    expect(c.ok && c.project.shots[1].chainFromPrevious &&
               c.project.shots[1].mode == ShotMode::Reference,
           "第 2 镜无首帧 → 接链式 + ref2va");

    // —— ③ seed：稳定键派生、禁止纯随机 ——
    expect(a.project.shots[0].seed >= 0, "seed 永不为 -1（禁止纯随机）");
    expect(a.project.shots[0].seed == b.project.shots[0].seed &&
               a.project.shots[0].seed == c.project.shots[0].seed,
           "同镜同 seed（与是否接链式无关）");
    expect(a.project.shots[0].seed != a.project.shots[1].seed, "不同镜 → 不同 seed");
    NarrativeShotInput explicitSeed = first;
    explicitSeed.seed = 42;
    NarrativeSceneInput seedIn;
    seedIn.sceneTitle = "第 1 场";
    seedIn.shots = {explicitSeed};
    const ToGenShotResult d = ToGenShot(scene, seedIn);
    expect(d.ok && d.project.shots[0].seed == 42, "调用方给了 seed 就用它");

    // —— ④ K21：参考图超上限 ——
    NarrativeShotInput many = first;
    many.referenceImages.clear();
    for (int k = 0; k < 12; ++k) {
        many.referenceImages.push_back(fmt::format("ref/{:02}.png", k));
    }
    NarrativeSceneInput manyIn;
    manyIn.sceneTitle = "第 2 场";
    manyIn.shots = {many};
    const ToGenShotResult e = ToGenShot(scene, manyIn);
    expect(e.ok && e.project.shots[0].referenceImages.size() ==
                       static_cast<std::size_t>(kMaxReferenceImagesPerShot),
           "K21：参考图 12 张 → 9 张");
    expect(HasIssue(e.issues, "K21", "high") && HasDegradation(e.degradations, kDegradeRefTruncated),
           "K21 已记账（severity=high + ref_truncated 降级）");
    expect(e.project.shots[0].characterAssetPaths == first.characterAssetPaths,
           "角色资产路径原样带过（不在这里解析）");
    ToGenShotOptions strict;
    strict.strictRefLimit = true;
    const ToGenShotResult f = ToGenShot(scene, manyIn, strict);
    expect(!f.ok && !f.error.empty() && f.project.shots.empty(),
           "strictRefLimit=true → 拒绝产出且不产半成品");

    // —— ⑤ 不产半成品：没有 PromptArtifact 的镜 ——
    NarrativeShotInput noPrompt;
    noPrompt.shotId = "sh_bad";
    noPrompt.ord = 1;
    NarrativeSceneInput badIn;
    badIn.sceneTitle = "第 3 场";
    badIn.shots = {first, noPrompt};
    const ToGenShotResult g = ToGenShot(scene, badIn);
    expect(!g.ok && g.error.find("PromptArtifact") != std::string::npos && g.project.shots.empty(),
           "空 prompt → 整场失败 + 中文原因（不得用叙事 prompt_text 顶替）");

    if (fail == 0) {
        log::Info("S6 桥自检通过（确定性 / K20·K21 记账 / mode / 链式 / seed / 不产半成品）");
    }
    return fail;
}

} // namespace shine::video
