#pragma once

#include "Comfy/Builders/SceneToImage/ShineSceneToImageWorkflowBuilder.h"
#include "Comfy/ShineComfySocket.h"
#include "Comfy/ShineComfyTypes.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"

class UShineVideoShotImageGraphNode;

/**
 * 一次"出分镜图"任务走到哪一步了。
 *
 * 顺序是**执行顺序**：显存门禁 → 上传 → 提交 → 执行 → 回读 → 落盘。
 * 与视频任务（`EShineVideoTaskState`）刻意分成两个枚举：两者的量级差了一个数量级
 * （SD1.5 一张图几秒，H3 一段 20 分钟），门禁阈值、超时、重试次数都不一样，
 * 合成一个枚举会让"这是哪条链路的状态"变得要靠猜。
 */
enum class EShineImageTaskState : uint8
{
    Idle,
    /** POST /api/free：卸载 ComfyUI 的模型缓存。 */
    FreeingVram,
    /** 轮询 /api/system_stats 直到空闲显存到阈值。 */
    WaitingForVram,
    /** 把颜色 / 深度 / 法线图传进 ComfyUI 的 input 目录，拿到 LoadImage 能用的文件名。 */
    Uploading,
    /** POST /api/prompt。 */
    Submitting,
    /** 等执行：WS 的 progress_state 驱动步进，另有 history 兜底轮询。 */
    Running,
    /** 回读 /api/history/{prompt_id}——"跑完了没有"只有它能作准。 */
    ReadingHistory,
    /** 定位 / 下载产物到本地。 */
    SavingMedia,
    Completed,
    Failed,
    Cancelled
};

/** 一次出图任务的完整快照：画布与日志都拿它重绘，不必自己拼事件。 */
struct FShineImageTaskStatus
{
    EShineImageTaskState State = EShineImageTaskState::Idle;

    /** 人话描述的当前动作，直接可以显示。 */
    FString Detail;

    FString PromptId;

    double VramFreeGb = 0.0;
    double VramRequiredGb = 0.0;
    int32 VramWaitSeconds = 0;

    /** 当前正在跑的节点（有步进时优先报采样器）。 */
    FString CurrentNodeId;
    FString CurrentNodeClass;
    int32 CurrentStep = 0;
    int32 TotalSteps = 0;

    /** 非致命问题（宽高被对齐、没有 ControlNet、参考图找不到兄弟文件…）。 */
    TArray<FString> Warnings;

    /** 产出的本地图片（落盘后的绝对路径）。 */
    TArray<FString> OutputFiles;

    FString ErrorMessage;

    bool IsActive() const
    {
        return State != EShineImageTaskState::Idle
            && State != EShineImageTaskState::Completed
            && State != EShineImageTaskState::Failed
            && State != EShineImageTaskState::Cancelled;
    }
};

/** 一次出图任务的输入：本地图片 + 采样参数 + 跑完写回哪个节点。 */
struct FShineImageTaskRequest
{
    /**
     * builder 的输入（提示词 / 采样 / 产物前缀）。
     *
     * `ColorImageName` / `DepthImageName` / `NormalImageName` 会被执行器**覆盖**：
     * 正式跑时填上传后 ComfyUI 给的文件名，dry-run 时填本地文件的文件名。
     */
    FShineSceneToImageRequest Workflow;

    /** 颜色参考图（本地磁盘绝对路径）。必填。 */
    FString ColorImagePath;

    /**
     * 深度 / 法线图（本地磁盘绝对路径）。留空且 `bAutoFindSiblings` 时会自动找
     * 同名 `_Depth` / `_Normal` 兄弟文件（场景捕获出来的三件套就是这样命名的）。
     */
    FString DepthImagePath;
    FString NormalImagePath;

    /** 是否自动找兄弟文件。显示/对拍时想严格用给定的那几张图，就关掉它。 */
    bool bAutoFindSiblings = true;

    /** ComfyUI 地址。留空就用默认（`http://127.0.0.1:8188`）。 */
    FString BaseUrl;

    /** 下载兜底时产物落的子目录名（本机 + 配了 output 目录时用不到）。 */
    FString OutputDirectoryName;

    /** 跑完把产物路径写回这个节点（画布上立刻出缩略图）。可空。 */
    TWeakObjectPtr<UShineVideoShotImageGraphNode> TargetNode;
};

/**
 * 出图任务的执行器：本地图片 →（ComfyUI）→ 本地图片 → 节点。
 *
 * ## 它与 builder 的分工
 *
 * builder 是纯函数，只认"已经上传到 ComfyUI 的文件名"；上传、显存门禁、提交、等执行、
 * 回读、落盘、回填节点这些有 IO / 异步的事全在这里。分工与视频那条线
 * （`FShineMiniMaxH3WorkflowBuilder` + `FShineVideoTaskRunner`）一一对应——
 * 两条链路的形状一样，读过一个就能读懂另一个。
 *
 * ## 为什么"跑完了没有"要同时看 WS 事件和 history
 *
 * ComfyUI 对未知输入键是静默忽略的，"提交成功"什么都证明不了；而 WS 事件本身也不是
 * 可靠的凭据（连接要是在提交后才建起来，事件是收不到的）。所以这里用 `progress_state`
 * 驱动**显示**，用 `/api/history/{prompt_id}` 决定**结束**，Running 期间按固定间隔轮询兜底。
 *
 * ## 生命周期与"谁来持有"
 *
 * 用 `TSharedRef` 持有；`Run()` 之后内部自持一份引用直到结束，调用方不必担心
 * 任务跑一半对象被销毁。结束时（无论成功失败）会广播 `FinishedEvent`。
 * 画布上的按钮与无 UI 控制台都走 `Start()`：一次只允许一个出图任务，
 * 这条约束由 `Start()` 统一守着，UI 与控制台不会各说各话。
 */
class SHINEEDITOR_API FShineImageTaskRunner : public TSharedFromThis<FShineImageTaskRunner, ESPMode::ThreadSafe>
{
public:
    using FOnStatusChanged = TFunction<void(const FShineImageTaskStatus&)>;

    FShineImageTaskRunner();
    ~FShineImageTaskRunner();

    /**
     * 跑一次出图。
     *
     * @param bDryRun true = 只编译节点图，**不碰网络**（不上传、不提交）。
     *                用来在零 GPU 成本下核对"图接得对不对"——ComfyUI 对未知输入键是
     *                静默忽略的，"提交成功"证明不了任何事，图本身才是判据。
     * @return false 表示**立刻**就失败了（参数不合法之类）；异步失败走 FinishedEvent。
     */
    bool Run(const FShineImageTaskRequest& Request, bool bDryRun = false);

    /** 停止：中断运行中的任务。 */
    void Cancel();

    const FShineImageTaskStatus& GetStatus() const { return Status; }

    void SetStatusCallback(FOnStatusChanged InCallback) { OnStatusChanged = MoveTemp(InCallback); }

    /** 结束（成功/失败/取消都会触发，且只触发一次）。 */
    FSimpleMulticastDelegate FinishedEvent;

    /** 最后一次成功编译出的 API 图（dry-run 对拍用）。 */
    const FString& GetLastBuiltGraphJson() const { return LastBuiltGraphJson; }

    /** 把最后一次编译出的 API 图写成可读 JSON（`Saved/ShineH3/<名字>_image_graph.json`）。 */
    FString SaveBuiltGraphJson(const FString& InName) const;

    static const TCHAR* ToString(EShineImageTaskState InState);

    // ---------------------------------------------------------------- 进程唯一的活动任务

    /** 当前正在跑的出图任务（没有则空）。 */
    static TSharedPtr<FShineImageTaskRunner> GetActive();

    /**
     * 起一个出图任务。
     *
     * 已经有活动任务时不再起第二个，并通过 `OutError` 说明原因——一次提交两张图
     * 会让 ComfyUI 队列里堆两个 SD1.5 任务，回读时节点也分不清哪张产物属于谁。
     *
     * @return 新的任务；启动失败（已有活动任务 / 参数不合法）时返回空。
     */
    static TSharedPtr<FShineImageTaskRunner> Start(const FShineImageTaskRequest& Request, bool bDryRun, FString& OutError);

    // ---------------------------------------------------------------- 工具

    /**
     * 出图的门禁阈值（GB）。
     *
     * 比 H3 那个 18GB 低得多：这条路上只会同时常驻 SD1.5 底座 + 两个 ControlNet（几 GB），
     * 拿 H3 的阈值守它等于永远等不到（编辑器自己就占着 9GB 左右）。
     */
    static constexpr double MinFreeVramGb = 4.0;

private:
    /**
     * 定出这次要用的本地图片（颜色图必填，深度/法线没给就找兄弟文件）。
     * @param bDryRun true 时把图名直接定成本地文件名（dry-run 不传文件）。
     * @return false = 参数不合法，已经 `MarkImmediateFailure` 过了。
     */
    bool ResolveInputImages(bool bDryRun);

    /** dry-run：编译出节点图就收工，不碰网络。 */
    void BuildOnly();

    void BeginVramWait();
    void PollVram();
    void UploadNextImage();
    void BuildAndSubmit();

    void HandlePromptEvent(const FShineComfySocket::FPromptEvent& Event);
    void MergeNodeStates(const TArray<FShineComfyNodeProgress>& NodeStates);

    void ReadHistory();
    void HandleHistoryEntry(const FShineComfyHistoryEntry& Entry);
    void ResolveOutputs(const TArray<FShineComfyHistoryMedia>& Images);

    /** 一个下载任务收尾；全部收尾后判定成功。 */
    void CompleteMediaTask();

    /** 把状态同步到画布节点上（状态色 / 步进 / 缩略图）。 */
    void ApplyStatusToTargetNode(bool bForceCanvasRefresh);
    void WriteOutputsToTargetNode();

    bool Tick(float DeltaTime);

    void StartTicker();
    void StopTicker();
    void SetState(EShineImageTaskState InState, const FString& InDetail);
    void TouchActivity();
    void BroadcastStatus();
    void Finish(EShineImageTaskState FinalState, const FString& Message);

    /**
     * 同步失败（Run 还没返回就发现参数不合法）。
     * 这类失败**不广播** FinishedEvent：调用方手里已经有 false 了。
     */
    void MarkImmediateFailure(const FString& Message);

    /** 上传后 ComfyUI 会给的文件名（确定性、带路径哈希，避免同名互踩）。 */
    static FString MakeUploadFileName(const FString& LocalPath);

    FShineImageTaskStatus Status;
    FOnStatusChanged OnStatusChanged;

    /** 运行期自持引用：WaitingForVram 阶段除了 ticker（只持弱引用）没人挂着自己。 */
    TSharedPtr<FShineImageTaskRunner> SelfRef;

    /** 请求的工作副本（dry-run 时也要能拿到参数）。 */
    FShineImageTaskRequest Request;

    FString BaseUrl;

    // ------------------------------------------------------------ 输入图
    /** 颜色图的本地绝对路径（解析过之后的那份）。 */
    FString ColorImageLocalPath;
    /** 要去上传的本地图片（下标 0 恒为颜色图）。 */
    TArray<FString> UploadOrder;
    /** 本地绝对路径 → ComfyUI input 里的文件名。 */
    TMap<FString, FString> UploadedFileNames;
    int32 UploadIndex = 0;

    // ------------------------------------------------------------ 门禁
    double RequiredVramBytes = 0.0;
    double VramWaitStartSeconds = 0.0;
    double LastVramPollSeconds = 0.0;
    static constexpr double VramPollIntervalSeconds = 2.0;
    /** 等显存的上限：超了就直接失败，否则阈值填错会让任务永远挂在那。 */
    static constexpr double VramWaitTimeoutSeconds = 120.0;

    // ------------------------------------------------------------ 运行
    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle PromptEventHandle;

    TMap<FString, FShineComfyNodeProgress> NodeStateById;
    TMap<FString, FString> BuiltNodeClassTypes;

    /** 采样器节点 id（KSampler）：步进进度只看它。 */
    FString SamplerNodeId;

    /** 落盘节点 id（SaveImage）：回读产物只认它，PreviewImage 那份 temp 图要排掉。 */
    FString SaveImageNodeId;

    int32 PendingMediaTasks = 0;
    bool bHistoryReadInFlight = false;
    bool bFinishing = false;
    bool bExecutionFinished = false;

    int32 HistoryReadAttempts = 0;
    double LastHistoryPollSeconds = 0.0;
    double LastActivitySeconds = 0.0;

    /** 画布重建的节流：逐节点进度一秒能来好几条，不该每条都重建全部节点控件。 */
    double LastCanvasRefreshSeconds = 0.0;
    static constexpr double CanvasRefreshIntervalSeconds = 1.0;

    /** Running 期间的兜底轮询间隔（正常靠 WS 事件推进，这里只是防事件丢失）。 */
    static constexpr double HistoryPollIntervalSeconds = 5.0;
    /** 收到执行结束信号后的重试间隔：ComfyUI 写 history 与发事件之间有点时间差。 */
    static constexpr double HistoryPollIntervalAfterFinishSeconds = 1.0;
    static constexpr int32 MaxHistoryReadAttempts = 5;
    /** 活动看门狗：SD1.5 一张图几十秒，10 分钟足够，超了说明是真挂了。 */
    static constexpr double ActivityTimeoutSeconds = 600.0;

    FString LastBuiltGraphJson;
};
