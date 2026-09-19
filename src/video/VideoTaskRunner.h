#pragma once
// shine::video —— 视频任务执行器（P5.5）
//
// 状态机（**只会向前推进，或回 Idle/Failed**）：
//   `Idle → Resolving → Uploading → Compiling → Submitting → Running → ReadingHistory → SavingMedia → Done | Failed`
//
// 线程纪律（`Doc/RULES-LANG.md` / `MEMORY.md`「异步任务规范」）：
//   * 状态只由 **UI 线程** 改（`Tick()` 每帧调；worker 只通过 `async::PostToUi` 投递结果）；
//   * 解析 / 读文件 / 上传 / 编译 / 下载 全在 **worker**；
//   * 提交与历史查询用既有的异步接口（`SubmitPromptJson` / `FetchHistory`，回调在 UI 线程）。
//
// 完成判定（`Doc/RULES-COMFY.md` §12）：**只看 ComfyUI 说了什么** ——
//   `QueueModel` 里该 `promptId` 走到 `Done/Failed/Cancelled` 才算数；
//   `execution_interrupted` = **已中断 ≠ 失败**（回 Idle）；静默 20s 才退回 `/history` 兜底。
//
// 上传口径（S2）：素材上传到 ComfyUI `input` 时**改名带时间戳**（避免 ComfyUI 按文件名命中旧缓存），
// 只把新名字写进**本次提交的工作副本**（`H3BuildOptions::uploadedNames`），**绝不改用户工程字段**。
#include "comfy/ComfyTypes.h" // HistoryMedia / HistoryResult（私有实现用）
#include "video/ApiGraphValidator.h"
#include "video/GenerationLedger.h"
#include "video/H3WorkflowBuilder.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace shine::video {

enum class VideoTaskPhase : int {
    Idle = 0,
    Resolving,
    Uploading,
    Compiling,
    Submitting,
    Running,
    ReadingHistory,
    SavingMedia,
    Done,
    Failed,
};

[[nodiscard]] const char* VideoTaskPhaseLabel(VideoTaskPhase phase) noexcept;

struct VideoTaskState {
    VideoTaskPhase phase = VideoTaskPhase::Idle;
    std::string detail;      // 中文一行（"解析引用…" / "上传 2/5" / "节点 12 KSampler" / 错误原因）
    float progress = 0.f;    // 0..1（Running 阶段来自 ComfyUI 的 progress 事件）
    std::size_t shotIndex = static_cast<std::size_t>(-1);
    std::string label;       // 任务名（分镜标题）
    std::string promptId;
    std::string error;       // 中文；Failed 时有效
    std::vector<std::string> savedFiles; // 落盘的绝对路径（UTF-8）
    std::size_t uploadDone = 0;
    std::size_t uploadTotal = 0;
    std::int64_t startedMs = 0;
    // K28「降级必须可见」：本次任务编译期发生的降级（无参考图 / 缺 ControlNet / 尺寸对齐…）。
    // 失败时**不清空**（降级原因正是排查线索），由 UI 汇总进章级报告。
    std::vector<GenerationDegradation> degradations;

    [[nodiscard]] bool Busy() const noexcept {
        return phase != VideoTaskPhase::Idle && phase != VideoTaskPhase::Done && phase != VideoTaskPhase::Failed;
    }
};

// 编译阶段的产物：API JSON + **降级账**（K28）。
// 为什么把降级随产物一起回传：编译发生在 worker，降级也在那时产生；只有随返回值出境，
// 才能既不跨线程改状态、又不丢账（以前只有一个 bool，出了 `build` 就没了）。
struct VideoBuildResult {
    std::string apiJson;                          // 空 = 编译失败（此时看 error）
    std::vector<GenerationDegradation> degradations;
};

// 队列优先级（`11` §2.7 W5）：**数字小的先跑**。角色资产（V0）必须先于依赖它的分镜图，
// 否则分镜图拿不到参考图、只能降级。同级之间按入队顺序 FIFO。
enum class VideoJobPriority : int {
    Asset = 0,      // 角色资产（正脸/四视图/基础身体/服装）
    SceneImage = 10, // 分镜图（依赖角色资产）
    ShotVideo = 20, // 分镜视频（H3）
};

[[nodiscard]] constexpr const char* VideoJobPriorityLabel(VideoJobPriority priority) noexcept {
    switch (priority) {
    case VideoJobPriority::Asset:
        return "角色资产";
    case VideoJobPriority::SceneImage:
        return "分镜图";
    case VideoJobPriority::ShotVideo:
        return "分镜视频";
    }
    return "任务";
}

// 一次要跑的任务：两个阶段各给一个"生产函数"（都跑在 **worker** 上）。
// H3 分镜走 `StartShot()` 的封装；其它产物（分镜图等）可以自己拼一个 job 复用整条链路。
struct VideoJob {
    std::string label;
    std::size_t shotIndex = static_cast<std::size_t>(-1);
    // 队列优先级；忙时提交会入队，空闲时按它挑选下一个（角色资产先于分镜图）
    VideoJobPriority priority = VideoJobPriority::SceneImage;
    // ① 解析阶段：产出**要上传的本地文件绝对路径**（UTF-8）；失败写 error 并返回空
    std::function<std::vector<std::string>(std::string& error)> collectUploads;
    // ② 编译阶段：拿到"原始文件名 → 上传后的文件名"映射，产出 API JSON + 降级账；失败写 error
    std::function<VideoBuildResult(const std::map<std::string, std::string>& uploadedNames, std::string& error)> build;
    // 完成（Done/Failed）时在 **UI 线程**回调一次（UI 用它回填分镜字段）
    std::function<void(const VideoTaskState&)> onFinish;
};

class VideoTaskRunner {
public:
    static VideoTaskRunner& Instance();

    void Init();
    void Shutdown();
    void Tick(); // **UI 线程**，每帧

    // 通用入口。空闲 → `Resolving` 起手；**忙 → 入队**（返回 true，`QueueSize()` 可见）。
    // 同一时刻仍只跑一个任务（ComfyUI 单卡），队列只负责"排队 + 按优先级挑选"（`11` §2.7 W5）。
    [[nodiscard]] bool Start(VideoJob job);

    // 待跑队列长度（不含正在跑的那个）
    [[nodiscard]] std::size_t QueueSize() const noexcept { return queue_.size(); }
    // 清空待跑队列（「中断」会顺带调用 —— 中断的语义是"停下来"，不是"跑下一个"）
    void ClearQueue() noexcept { queue_.clear(); }

    // 便捷封装：编译并提交某个分镜的 H3 工作流（`projectDir` = 角色资产根）
    [[nodiscard]] bool StartShot(const VideoProject& project, std::size_t shotIndex,
                                 const std::filesystem::path& projectDir,
                                 const std::filesystem::path& mediaLibraryDir,
                                 std::function<void(const VideoTaskState&)> onFinish = {});

    // 「中断」= `/interrupt`；状态机回到 Idle（**已上传素材不回滚**）
    void Cancel();

    [[nodiscard]] const VideoTaskState& State() const noexcept { return state_; }
    // Done/Failed 之后由 UI 调一次，把状态收回 Idle（提示已读）
    void Acknowledge();

    // 把一份 API JSON 对着**本机 `/object_info`** 校验（**UI 线程**；提交前必做，见 `.cpp` 里的说明）。
    // S4：`/object_info` 还没拿到 → **`ok=false` + `blocked=true`（拒绝提交）**，不再"跳过校验"。
    // 「没校验过」不等于「校验通过」—— 否则写错的输入名会被 ComfyUI 静默忽略，参考图悄悄丢掉。
    [[nodiscard]] static GraphCheckResult CheckAgainstComfyUI(std::string_view apiJson);

private:
    VideoTaskRunner() = default;

    // 队列条目（`seq` = 入队序，同级 FIFO 用）
    struct QueuedJob {
        VideoJob job;
        std::int64_t seq = 0;
    };

    void Fail(std::string error);
    void StartNow(VideoJob job); // 真正起手（`Start` 与 `Tick` 都走它）
    void FinishBackfill();
    void AdvanceToRunning(std::string promptId);
    void PollQueue();
    void RequestHistory();
    void OnHistory(comfy::HistoryResult result);
    void CollectAndUpload(VideoJob job);
    void DownloadOutputs(std::vector<comfy::HistoryMedia> media);

    VideoTaskState state_;
    VideoJob job_;                       // 当前任务（含两个生产函数）
    std::vector<QueuedJob> queue_;       // 待跑队列（不含当前任务）
    std::int64_t nextSeq_ = 0;
    std::vector<comfy::HistoryMedia> pendingMedia_;
    std::filesystem::path outputDir_;
    std::int64_t historyRequestedMs_ = 0;
    std::size_t downloadDone_ = 0;
};

// 队列挑选的输入（只取挑选需要的两个字段 → 纯函数，离线可断言）
struct QueuePickCandidate {
    int priority = 0;   // `VideoJobPriority` 的整数值
    std::int64_t seq = 0; // 入队序（同级 FIFO）
};

// 队列挑选规则（`11` §2.7 W5）：优先级小的先；同级按入队序。**纯函数**。
// 返回下标；空输入返回 `npos`（`static_cast<size_t>(-1)`）。
[[nodiscard]] std::size_t PickNextQueuedJobIndex(std::span<const QueuePickCandidate> candidates) noexcept;

// 离线自检（`SHINE_SCENE_IMAGE_CHECK` 会连带跑）：队列优先级 + 一镜多 job 不互相覆盖 +
// H3 侧降级类型化。返回 fail 条数（0 = 全过）。
[[nodiscard]] int RunVideoQueueSelfCheck();

} // namespace shine::video
