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
#include "video/H3WorkflowBuilder.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
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

    [[nodiscard]] bool Busy() const noexcept {
        return phase != VideoTaskPhase::Idle && phase != VideoTaskPhase::Done && phase != VideoTaskPhase::Failed;
    }
};

// 一次要跑的任务：两个阶段各给一个"生产函数"（都跑在 **worker** 上）。
// H3 分镜走 `StartShot()` 的封装；其它产物（分镜图等）可以自己拼一个 job 复用整条链路。
struct VideoJob {
    std::string label;
    std::size_t shotIndex = static_cast<std::size_t>(-1);
    // ① 解析阶段：产出**要上传的本地文件绝对路径**（UTF-8）；失败写 error 并返回空
    std::function<std::vector<std::string>(std::string& error)> collectUploads;
    // ② 编译阶段：拿到"原始文件名 → 上传后的文件名"映射，产出最终 API JSON；失败写 error
    std::function<std::string(const std::map<std::string, std::string>& uploadedNames, std::string& error)> build;
    // 完成（Done/Failed）时在 **UI 线程**回调一次（UI 用它回填分镜字段）
    std::function<void(const VideoTaskState&)> onFinish;
};

class VideoTaskRunner {
public:
    static VideoTaskRunner& Instance();

    void Init();
    void Shutdown();
    void Tick(); // **UI 线程**，每帧

    // 通用入口。`Resolving` 起手；同一时刻只允许一个任务（`Busy()` 时返回 false）
    [[nodiscard]] bool Start(VideoJob job);

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
    // 未连接 / object_info 还没加载 → 返回 `ok=true` 并在日志里写明"跳过校验"（不阻断离线自测）。
    [[nodiscard]] static GraphCheckResult CheckAgainstComfyUI(std::string_view apiJson);

private:
    VideoTaskRunner() = default;

    void Fail(std::string error);
    void FinishBackfill();
    void AdvanceToRunning(std::string promptId);
    void PollQueue();
    void RequestHistory();
    void OnHistory(comfy::HistoryResult result);
    void CollectAndUpload(VideoJob job);
    void DownloadOutputs(std::vector<comfy::HistoryMedia> media);

    VideoTaskState state_;
    VideoJob job_;                       // 当前任务（含两个生产函数）
    std::vector<comfy::HistoryMedia> pendingMedia_;
    std::filesystem::path outputDir_;
    std::int64_t historyRequestedMs_ = 0;
    std::size_t downloadDone_ = 0;
};

} // namespace shine::video
