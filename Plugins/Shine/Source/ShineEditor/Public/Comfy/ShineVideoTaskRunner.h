#pragma once

#include "Asset/ShineVideoProject.h"
#include "Comfy/ShineComfySocket.h"
#include "Comfy/ShineComfyTypes.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

/**
 * 一次视频任务走到哪一步了。
 *
 * 顺序是**执行顺序**：显存门禁 → 上传 → 提交 → 执行 → 回读 → 落盘。
 * 之所以把显存门禁放在最前面：H3 的底座 + Qwen3-VL-32B 文本编码器会同时常驻，
 * 显存不够时 ComfyUI 不报错，而是开始换页、表现为"越来越慢直到卡死"（实测
 * 1344×768 跑 ref2va 必卡死就是这么来的）。宁可在这里等，也不能带着不足的显存进去。
 */
enum class EShineVideoTaskState : uint8
{
    Idle,
    /** 解析 `@image:` / `@char:` / `{{Mixed N}}`，定出每个分镜的参考图与提示词。 */
    Resolving,
    /** POST /api/free：卸载 ComfyUI 的模型缓存。 */
    FreeingVram,
    /** 轮询 /api/system_stats 直到空闲显存到阈值。 */
    WaitingForVram,
    /** 把参考图传进 ComfyUI 的 input 目录，拿到 LoadImage 能用的文件名。 */
    Uploading,
    /** POST /api/prompt。 */
    Submitting,
    /** 等执行：WS 的 progress_state 驱动逐节点进度，另有 history 兜底轮询。 */
    Running,
    /** 回读 /api/history/{prompt_id}——"跑完了没有"只有它能作准。 */
    ReadingHistory,
    /** 定位 / 下载产物到本地。 */
    SavingMedia,
    Completed,
    Failed,
    Cancelled
};

/** 单个分镜在这一次任务里的进度。 */
struct FShineVideoShotProgress
{
    int32 ShotIndex = INDEX_NONE;
    FString Title;

    /** 这三个节点 id 来自 builder 的 FShotPlan，用来把裸节点 id 翻译成人话。 */
    FString ConditioningNodeId;
    FString SamplerNodeId;
    FString SaveVideoNodeId;

    /** 该分镜的总体状态：pending / running / finished / error（取它自己最靠后的那个节点）。 */
    FString State;

    /** 采样进度（只有采样器节点才有步进）。 */
    int32 StepValue = 0;
    int32 StepMax = 0;

    /** 该分镜产出并落盘到本地的文件。 */
    TArray<FString> OutputFiles;

    bool IsFinished() const { return State == TEXT("finished"); }
    bool IsRunning() const { return State == TEXT("running"); }
    bool IsFailed() const { return State == TEXT("error"); }
};

/** 一次任务的完整快照。面板拿它重绘，不必自己拼事件。 */
struct FShineVideoTaskStatus
{
    EShineVideoTaskState State = EShineVideoTaskState::Idle;

    /** 人话描述的当前动作，直接可以显示。 */
    FString Detail;

    FString PromptId;

    /** 显存门禁的读数（GB）。 */
    double VramFreeGb = 0.0;
    double VramRequiredGb = 0.0;
    int32 VramWaitSeconds = 0;

    /** 当前正在跑的节点（有步进时优先报采样器）。 */
    FString CurrentNodeId;
    FString CurrentNodeClass;
    int32 CurrentStep = 0;
    int32 TotalSteps = 0;

    /** 整张图的节点状态（来自 progress_state，按节点 id 合并）。 */
    TArray<FShineComfyNodeProgress> NodeStates;

    TArray<FShineVideoShotProgress> Shots;

    /** 非致命问题（帧数被对齐、参考图超限被裁、角色找不到…）。 */
    TArray<FString> Warnings;

    /** 产出的本地文件（落盘后的绝对路径）。 */
    TArray<FString> OutputFiles;

    FString ErrorMessage;

    bool IsActive() const
    {
        return State != EShineVideoTaskState::Idle
            && State != EShineVideoTaskState::Completed
            && State != EShineVideoTaskState::Failed
            && State != EShineVideoTaskState::Cancelled;
    }

    const FShineComfyNodeProgress* FindNodeState(const FString& NodeId) const
    {
        return NodeStates.FindByPredicate([&NodeId](const FShineComfyNodeProgress& Progress)
        {
            return Progress.NodeId == NodeId;
        });
    }
};

/**
 * 视频任务的执行器：项目 →（图）→ ComfyUI → 本地 mp4。
 *
 * ## 它与 builder 的分工
 *
 * builder 是纯函数，只认"已经上传到 ComfyUI 的参考图文件名"；上传、显存门禁、提交、
 * 等执行、回读、落盘这些有 IO / 异步的事全在这里。所以跑一次任务的顺序是：
 *
 *   1. 解析引用语法 → 每个分镜的有序本地图片列表；
 *   2. 去重后逐个上传到 ComfyUI，拿到 input 里的文件名；
 *   3. 把这些名字写回项目的**工作副本**，再交给 builder 编译成 API 图；
 *   4. 显存门禁 → 提交 → 等执行 → 回读 history → 落盘。
 *
 * ## 为什么"跑完了没有"要同时看 WS 事件和 history
 *
 * ComfyUI 对未知输入键是静默忽略的（H3-SPEC.md 坑 1），"提交成功"什么都证明不了；
 * 而 WS 事件本身也不是可靠的凭据（连接要是在提交后才建起来，事件是收不到的）。
 * 所以这里用 `progress_state` 驱动**显示**，用 `/api/history/{prompt_id}` 决定**结束**，
 * 并且在 Running 期间按固定间隔轮询 history 兜底。
 *
 * ## 生命周期
 *
 * 用 `TSharedRef` 持有；`Run()` 之后内部会自持一份引用直到结束，所以调用方
 * 不必担心"任务跑一半对象被销毁"。结束时（无论成功失败）会广播 `FinishedEvent`。
 */
class SHINEEDITOR_API FShineVideoTaskRunner : public TSharedFromThis<FShineVideoTaskRunner, ESPMode::ThreadSafe>
{
public:
    using FOnStatusChanged = TFunction<void(const FShineVideoTaskStatus&)>;

    FShineVideoTaskRunner();
    ~FShineVideoTaskRunner();

    /**
     * 跑一个项目（所有可提交的分镜一次提交）。
     *
     * @param BaseUrlOverride 留空则用项目自己的 ComfyBaseUrl。
     * @param bDryRun         true = 只解析引用 + 编译节点图，**不碰网络**（不上传、不提交）。
     *                        用来在没有 GPU 成本的前提下核对图接得对不对——ComfyUI 对未知
     *                        输入键是静默忽略的，"提交成功"证明不了任何事，图本身才是判据。
     * @return false 表示**立刻**就失败了（项目不合法之类）；异步失败走 FinishedEvent。
     */
    bool Run(const UShineVideoProject& Project, const FString& BaseUrlOverride = FString(), bool bDryRun = false);

    /** 停止：先中断运行中的任务，没开始跑的就从队列里撤掉。 */
    void Cancel();

    const FShineVideoTaskStatus& GetStatus() const { return Status; }

    void SetStatusCallback(FOnStatusChanged InCallback) { OnStatusChanged = MoveTemp(InCallback); }

    /** 结束（成功/失败/取消都会触发，且只触发一次）。 */
    FSimpleMulticastDelegate FinishedEvent;

    /** 最后一次成功编译出的 API 图（调试、以及与 P0 探针图对拍都用它）。 */
    const FString& GetLastBuiltGraphJson() const { return LastBuiltGraphJson; }

    /**
     * 把最后一次编译出的 API 图写成可读 JSON（`Saved/ShineH3/<名字>_graph.json`）。
     *
     * 存在的理由：ComfyUI 对未知输入键是**静默忽略**的，"提交成功"完全不能证明接线对，
     * 图本身才是判据。写出来就能用 `Scripts/H3/DiffProbeGraph.py` 跟 P0 探针图逐项对拍。
     *
     * @return 写出的绝对路径；还没有图或写失败时返回空串。
     */
    FString SaveBuiltGraphJson(const FString& InName) const;

    static const TCHAR* ToString(EShineVideoTaskState InState);

private:
    /** 解析引用语法，并把每个分镜要上传的本地图片列出来（去重）。 */
    bool ResolveShots();

    /** 上传完最后一张图后：把文件名写回工作副本 → 编译 → 提交。 */
    void BuildAndSubmit();

    /** dry-run：编译出节点图就收工，不碰网络。 */
    void BuildOnly();

    void UploadNextImage();
    void BeginVramWait();
    void PollVram();
    void ReadHistory();
    void HandleHistoryEntry(const FShineComfyHistoryEntry& Entry);
    void ResolveOutputs(const TArray<FShineComfyHistoryMedia>& Videos);

    void HandlePromptEvent(const FShineComfySocket::FPromptEvent& Event);
    void MergeNodeStates(const TArray<FShineComfyNodeProgress>& NodeStates);
    void UpdateShotStates();

    /** 把一段产物记到全局与对应分镜上（能对上节点 id 就对，对不上按顺序兜底）。 */
    void RecordSavedMedia(const FShineComfyHistoryMedia& Media, const FString& LocalPath);

    /** 一个产物任务收尾；全部收尾后判定成功。 */
    void CompleteMediaTask();

    bool Tick(float DeltaTime);

    void StartTicker();
    void StopTicker();
    void SetState(EShineVideoTaskState InState, const FString& InDetail);
    void TouchActivity();
    void BroadcastStatus();
    void Finish(EShineVideoTaskState FinalState, const FString& Message);

    /**
     * 同步失败（Run 还没返回就发现项目不合法）。
     * 这类失败**不广播** FinishedEvent：调用方手里已经有 false 了。
     */
    void MarkImmediateFailure(const FString& Message);

    /** 上传后 ComfyUI 会给的文件名（确定性、带路径哈希，避免同名互踩）。 */
    static FString MakeUploadFileName(const FString& LocalPath);

    FShineVideoTaskStatus Status;
    FOnStatusChanged OnStatusChanged;

    /** 运行期自持引用：WaitingForVram 阶段没有任何异步回调挂着自己，靠它续命。 */
    TSharedPtr<FShineVideoTaskRunner> SelfRef;

    /** 工作副本：上传后要把参考图标识换成 ComfyUI input 里的文件名，不能改用户资产。 */
    TStrongObjectPtr<UShineVideoProject> WorkingCopy;

    FString BaseUrl;
    FString ProjectName;
    FString ProjectOutputDirectory;

    // ------------------------------------------------------------ 门禁
    double RequiredVramBytes = 0.0;
    double VramWaitStartSeconds = 0.0;
    double LastVramPollSeconds = 0.0;
    static constexpr double VramPollIntervalSeconds = 2.0;
    /** 等显存的上限：超了就直接失败，否则一个过高的阈值会让任务永远挂在那。 */
    static constexpr double VramWaitTimeoutSeconds = 180.0;

    // ------------------------------------------------------------ 上传
    /** 全项目去重后的本地图片（绝对路径）。 */
    TArray<FString> DistinctLocalImages;
    /** 本地绝对路径 → ComfyUI input 里的文件名。 */
    TMap<FString, FString> UploadedFileNames;
    /** 每个分镜的有序本地图片（下标与 WorkingCopy->Shots 对齐）。 */
    TArray<TArray<FString>> ShotLocalImages;
    int32 UploadIndex = 0;

    // ------------------------------------------------------------ 运行
    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle PromptEventHandle;

    TMap<FString, FShineComfyNodeProgress> NodeStateById;

    /** 节点 id -> class_type（来自 builder），把裸 id 翻译成人话用。 */
    TMap<FString, FString> BuiltNodeClassTypes;

    /** 还没收尾的产物任务数（本机上文件已在磁盘时不会进这个计数）。 */
    int32 PendingMediaTasks = 0;

    bool bHistoryReadInFlight = false;
    bool bFinishing = false;

    /**
     * 是否已经收到"执行结束"的信号（execution_success）。
     *
     * 这个标志决定"history 里还没有"是**正常**（还在跑，慢慢等）还是**异常**
     * （已经结束了还写不出来，重试几次就该报错）。不区分的话，正常跑着的那几十分钟
     * 会被反复当成"读不到结果"而提前判失败。
     */
    bool bExecutionFinished = false;

    int32 HistoryReadAttempts = 0;
    double LastHistoryPollSeconds = 0.0;
    double LastActivitySeconds = 0.0;

    /** Running 期间的兜底轮询间隔（正常靠 WS 事件推进，这里只是防事件丢失）。 */
    static constexpr double HistoryPollIntervalSeconds = 15.0;
    /** 收到执行结束信号后的重试间隔：ComfyUI 写 history 与发事件之间有点时间差。 */
    static constexpr double HistoryPollIntervalAfterFinishSeconds = 2.0;
    static constexpr int32 MaxHistoryReadAttempts = 5;
    /** 活动看门狗：两段链式实测约 42 分钟，90 分钟足够，超了说明是真挂了。 */
    static constexpr double ActivityTimeoutSeconds = 5400.0;

    FString LastBuiltGraphJson;
};
