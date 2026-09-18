#include "Comfy/ShineVideoTaskRunner.h"

#include "Asset/ShineCharacterAsset.h"
#include "Comfy/Builders/MiniMaxH3/ShineMiniMaxH3WorkflowBuilder.h"
#include "Comfy/ShineComfyClient.h"
#include "Comfy/ShineComfyPathSettings.h"
#include "Comfy/ShineComfyPaths.h"
#include "Comfy/ShineMentionResolver.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogShineVideoTask, Log, All);

namespace
{
    /** 把 builder 给出的"分镜 → 节点 id"映射对到进度槽上，面板才有东西可定位。 */
    void ApplyShotPlansToStatus(
        const TArray<FShineMiniMaxH3WorkflowBuilder::FShotPlan>& ShotPlans,
        TArray<FShineVideoShotProgress>& Shots)
    {
        for (const FShineMiniMaxH3WorkflowBuilder::FShotPlan& Plan : ShotPlans)
        {
            FShineVideoShotProgress* Progress = Shots.FindByPredicate(
                [&Plan](const FShineVideoShotProgress& Candidate) { return Candidate.ShotIndex == Plan.ShotIndex; });

            if (!Progress)
            {
                continue;
            }

            Progress->ConditioningNodeId = Plan.ConditioningNodeId;
            Progress->SamplerNodeId = Plan.SamplerNodeId;
            Progress->SaveVideoNodeId = Plan.SaveVideoNodeId;
        }
    }
}

const TCHAR* FShineVideoTaskRunner::ToString(EShineVideoTaskState InState)
{
    switch (InState)
    {
    case EShineVideoTaskState::Idle:           return TEXT("Idle");
    case EShineVideoTaskState::Resolving:      return TEXT("Resolving");
    case EShineVideoTaskState::FreeingVram:    return TEXT("FreeingVram");
    case EShineVideoTaskState::WaitingForVram: return TEXT("WaitingForVram");
    case EShineVideoTaskState::Uploading:      return TEXT("Uploading");
    case EShineVideoTaskState::Submitting:     return TEXT("Submitting");
    case EShineVideoTaskState::Running:        return TEXT("Running");
    case EShineVideoTaskState::ReadingHistory: return TEXT("ReadingHistory");
    case EShineVideoTaskState::SavingMedia:    return TEXT("SavingMedia");
    case EShineVideoTaskState::Completed:      return TEXT("Completed");
    case EShineVideoTaskState::Failed:         return TEXT("Failed");
    case EShineVideoTaskState::Cancelled:      return TEXT("Cancelled");
    default:                                   return TEXT("Unknown");
    }
}

FString FShineVideoTaskRunner::MakeUploadFileName(const FString& LocalPath)
{
    // 实现放在 ShineComfyPaths 里：出图链路（FShineImageTaskRunner）用的是同一套命名，
    // 两份实现迟早会在"同名不互踩"这种细节上分叉。
    return ShineComfyPaths::MakeUploadFileName(LocalPath);
}

FShineVideoTaskRunner::FShineVideoTaskRunner()
{
}

FShineVideoTaskRunner::~FShineVideoTaskRunner()
{
    StopTicker();

    if (PromptEventHandle.IsValid())
    {
        FShineComfySocket::Get().PromptEvent.Remove(PromptEventHandle);
        PromptEventHandle.Reset();
    }
}

void FShineVideoTaskRunner::MarkImmediateFailure(const FString& Message)
{
    Status.State = EShineVideoTaskState::Failed;
    Status.Detail = Message;
    Status.ErrorMessage = Message;
    UE_LOG(LogShineVideoTask, Error, TEXT("%s"), *Message);
    BroadcastStatus();
}

bool FShineVideoTaskRunner::Run(const UShineVideoProject& Project, const FString& BaseUrlOverride, bool bDryRun)
{
    if (Status.IsActive())
    {
        UE_LOG(LogShineVideoTask, Warning, TEXT("上一次任务还没结束（%s），已拒绝新的提交。"), ToString(Status.State));
        return false;
    }

    Status = FShineVideoTaskStatus();
    NodeStateById.Reset();
    BuiltNodeClassTypes.Reset();
    UploadedFileNames.Reset();
    DistinctLocalImages.Reset();
    ShotLocalImages.Reset();
    UploadIndex = 0;
    HistoryReadAttempts = 0;
    LastHistoryPollSeconds = 0.0;
    bHistoryReadInFlight = false;
    bExecutionFinished = false;
    bFinishing = false;
    PendingMediaTasks = 0;
    LastBuiltGraphJson.Reset();
    SelfRef.Reset();

    BaseUrl = FShineComfyClient::NormalizeBaseUrl(BaseUrlOverride.IsEmpty() ? Project.ComfyBaseUrl : BaseUrlOverride);
    ProjectName = Project.ProjectName.IsEmpty() ? Project.GetName() : Project.ProjectName;

    const UShineComfyPathSettings* Settings = GetDefault<UShineComfyPathSettings>();
    Status.VramRequiredGb = Settings ? Settings->MinFreeVramGb : 0.0;
    RequiredVramBytes = FMath::Max(Status.VramRequiredGb, 0.0) * 1024.0 * 1024.0 * 1024.0;

    // 工作副本：上传后要把参考图标识换成 ComfyUI input 里的文件名，绝不能改用户资产。
    WorkingCopy = TStrongObjectPtr<UShineVideoProject>(DuplicateObject<UShineVideoProject>(&Project, GetTransientPackage()));
    if (!WorkingCopy.IsValid())
    {
        MarkImmediateFailure(TEXT("复制项目失败，无法开工。"));
        return false;
    }

    WorkingCopy->Sanitize();

    if (!ResolveShots())
    {
        return false;
    }

    if (bDryRun)
    {
        // 纯本地：不碰网络，把图编译出来就算完事。用来零成本核对接线。
        BuildOnly();
        return true;
    }

    // 从这里开始是异步的：自持一份引用。WaitingForVram 阶段除了 ticker（只持弱引用）
    // 没有任何东西挂着自己，不这么做会被回收。
    SelfRef = AsShared();
    TouchActivity();
    StartTicker();

    FShineComfySocket& Socket = FShineComfySocket::Get();
    Socket.EnsureConnected(BaseUrl);
    PromptEventHandle = Socket.PromptEvent.AddLambda(
        [Self = AsShared()](const FShineComfySocket::FPromptEvent& Event) { Self->HandlePromptEvent(Event); });

    // 门禁第一步永远是 /api/free：Dynamic VRAM 会尽量占满显存，
    // 不先卸载的话"空闲显存"根本到不了阈值（顺序反了会永远等下去）。
    SetState(EShineVideoTaskState::FreeingVram, TEXT("卸载 ComfyUI 缓存，腾显存…"));
    FShineComfyClient::FreeVram(BaseUrl, [Self = AsShared()](FShineComfyFreeVramResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Finish(EShineVideoTaskState::Failed, FString::Printf(TEXT("/api/free 失败：%s"), *Result.ErrorMessage));
            return;
        }

        Self->BeginVramWait();
    });

    return true;
}

bool FShineVideoTaskRunner::ResolveShots()
{
    const TArray<FShineVideoShot>& Shots = WorkingCopy->Shots;
    ShotLocalImages.SetNum(Shots.Num());
    Status.Shots.Reset();

    for (int32 ShotIndex = 0; ShotIndex < Shots.Num(); ++ShotIndex)
    {
        const FShineVideoShot& Shot = Shots[ShotIndex];
        if (!Shot.IsSubmittable())
        {
            continue;
        }

        FShineMentionResolveRequest Request;
        Request.Prompt = Shot.Prompt;
        Request.ReferenceImages = Shot.ReferenceImages;
        Request.CharacterAssetPaths = Shot.CharacterAssetPaths;
        Request.ProjectCharacterAssetPaths = WorkingCopy->CharacterAssetPaths;

        const FShineMentionResolveResult Resolved = FShineMentionResolver::Resolve(Request);
        Status.Warnings.Append(Resolved.Warnings);
        if (!Resolved.bSuccess)
        {
            MarkImmediateFailure(Resolved.ErrorMessage);
            return false;
        }

        WorkingCopy->Shots[ShotIndex].Prompt = Resolved.ResolvedPrompt;
        if (Resolved.ForcedSeed.IsSet())
        {
            // 角色要求锁种子（跨镜头一致性），覆盖分镜自己的取值。
            WorkingCopy->Shots[ShotIndex].Seed = Resolved.ForcedSeed.GetValue();
        }

        // 首帧（只有 fl2va 用得上）也得上传：它在图里还是一个 LoadImage 节点。
        TArray<FString> LocalImages = Resolved.OrderedImagePaths;
        const FString FirstFrame = Shot.FirstFrameImage.TrimStartAndEnd();
        if (!FirstFrame.IsEmpty())
        {
            LocalImages.AddUnique(FShineMentionResolver::ResolveLocalPath(FirstFrame));
        }

        TArray<FString> DistinctForShot;
        for (const FString& LocalPath : LocalImages)
        {
            if (LocalPath.IsEmpty())
            {
                continue;
            }

            if (!FPaths::FileExists(LocalPath))
            {
                MarkImmediateFailure(FString::Printf(
                    TEXT("第 %d 段的参考图找不到：%s"), ShotIndex + 1, *LocalPath));
                return false;
            }

            DistinctForShot.Add(LocalPath);
            DistinctLocalImages.AddUnique(LocalPath);
        }

        ShotLocalImages[ShotIndex] = MoveTemp(DistinctForShot);

        FShineVideoShotProgress Progress;
        Progress.ShotIndex = ShotIndex;
        Progress.Title = Shot.Title.IsEmpty() ? FString::Printf(TEXT("分镜 %d"), ShotIndex + 1) : Shot.Title;
        Progress.State = TEXT("pending");
        Status.Shots.Add(MoveTemp(Progress));

        UE_LOG(LogShineVideoTask, Display, TEXT("[%s] 参考图 %d 张；提示词：%s"),
            *Status.Shots.Last().Title, ShotLocalImages[ShotIndex].Num(), *Resolved.ResolvedPrompt);
    }

    return true;
}

void FShineVideoTaskRunner::BeginVramWait()
{
    VramWaitStartSeconds = FPlatformTime::Seconds();
    LastVramPollSeconds = VramWaitStartSeconds;
    Status.VramWaitSeconds = 0;
    SetState(EShineVideoTaskState::WaitingForVram, TEXT("检查空闲显存…"));
    PollVram();
}

void FShineVideoTaskRunner::PollVram()
{
    FShineComfyClient::FetchSystemStats(BaseUrl, [Self = AsShared()](FShineComfySystemStatsResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Status.Warnings.Add(FString::Printf(TEXT("读 /api/system_stats 失败：%s"), *Result.ErrorMessage));
            Self->BroadcastStatus();
            return;
        }

        Self->Status.VramFreeGb = Result.GetVramFreeGb();
        Self->TouchActivity();

        if (Result.VramFreeBytes >= Self->RequiredVramBytes)
        {
            UE_LOG(LogShineVideoTask, Display, TEXT("显存门禁通过：空闲 %.2f GB（要求 %.2f GB）。"),
                Self->Status.VramFreeGb, Self->Status.VramRequiredGb);

            Self->UploadIndex = 0;
            if (Self->DistinctLocalImages.Num() == 0)
            {
                Self->BuildAndSubmit();
            }
            else
            {
                Self->UploadNextImage();
            }
            return;
        }

        Self->SetState(EShineVideoTaskState::WaitingForVram, FString::Printf(
            TEXT("等空闲显存：%.2f / %.1f GB（已等 %d 秒）"),
            Self->Status.VramFreeGb, Self->Status.VramRequiredGb, Self->Status.VramWaitSeconds));
    });
}

void FShineVideoTaskRunner::UploadNextImage()
{
    if (UploadIndex >= DistinctLocalImages.Num())
    {
        BuildAndSubmit();
        return;
    }

    const FString LocalPath = DistinctLocalImages[UploadIndex];

    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *LocalPath))
    {
        Finish(EShineVideoTaskState::Failed, FString::Printf(TEXT("读不出参考图：%s"), *LocalPath));
        return;
    }

    const FString UploadName = MakeUploadFileName(LocalPath);
    SetState(EShineVideoTaskState::Uploading, FString::Printf(
        TEXT("上传参考图 %d/%d：%s"), UploadIndex + 1, DistinctLocalImages.Num(), *FPaths::GetCleanFilename(LocalPath)));

    FShineComfyClient::UploadImage(BaseUrl, UploadName, FileBytes, false,
        [Self = AsShared(), LocalPath, UploadName](FShineComfyUploadResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Finish(EShineVideoTaskState::Failed, FString::Printf(
                TEXT("上传参考图失败：%s（%s）"), *LocalPath, *Result.ErrorMessage));
            return;
        }

        // LoadImage 的 image 字段认"相对 input 目录的路径"，所以 subfolder 非空时要带上。
        const FString ResolvedName = Result.Name.IsEmpty() ? UploadName : Result.Name;
        const FString ComfyName = Result.Subfolder.IsEmpty()
            ? ResolvedName
            : FString::Printf(TEXT("%s/%s"), *Result.Subfolder, *ResolvedName);

        Self->UploadedFileNames.Add(LocalPath, ComfyName);
        ++Self->UploadIndex;
        Self->TouchActivity();
        Self->UploadNextImage();
    });
}

void FShineVideoTaskRunner::BuildOnly()
{
    SetState(EShineVideoTaskState::Submitting, TEXT("dry-run：只编译节点图…"));

    const FShineMiniMaxH3WorkflowBuilder::FBuildResult BuildResult = FShineMiniMaxH3WorkflowBuilder::Build(*WorkingCopy);
    Status.Warnings.Append(BuildResult.Warnings);
    if (!BuildResult.bSuccess)
    {
        Finish(EShineVideoTaskState::Failed, BuildResult.ErrorMessage);
        return;
    }

    LastBuiltGraphJson = BuildResult.ToJsonString();
    BuiltNodeClassTypes = BuildResult.NodeClassTypes;
    ApplyShotPlansToStatus(BuildResult.ShotPlans, Status.Shots);

    Finish(EShineVideoTaskState::Completed, FString::Printf(
        TEXT("dry-run：编译出 %d 个节点、%d 段，未提交。"), BuildResult.NodeClassTypes.Num(), BuildResult.ShotPlans.Num()));
}

void FShineVideoTaskRunner::BuildAndSubmit()
{
    // 把上传后的文件名写回工作副本：builder 是纯函数，它只认 ComfyUI input 里的名字。
    for (int32 ShotIndex = 0; ShotIndex < ShotLocalImages.Num(); ++ShotIndex)
    {
        FShineVideoShot& Shot = WorkingCopy->Shots[ShotIndex];

        TArray<FString> ComfyNames;
        for (const FString& LocalPath : ShotLocalImages[ShotIndex])
        {
            if (const FString* ComfyName = UploadedFileNames.Find(LocalPath))
            {
                ComfyNames.Add(*ComfyName);
            }
        }

        Shot.ReferenceImages = MoveTemp(ComfyNames);

        const FString FirstFrame = Shot.FirstFrameImage.TrimStartAndEnd();
        if (!FirstFrame.IsEmpty())
        {
            const FString LocalFirstFrame = FShineMentionResolver::ResolveLocalPath(FirstFrame);
            if (const FString* ComfyName = UploadedFileNames.Find(LocalFirstFrame))
            {
                Shot.FirstFrameImage = *ComfyName;
            }
            else
            {
                // 上传表里没有它就说明这张首帧本来就不存在/没解析出来，
                // 交给 builder 走"用第一张参考图当首帧"的兜底。
                Shot.FirstFrameImage.Reset();
            }
        }
    }

    SetState(EShineVideoTaskState::Submitting, TEXT("编译节点图并提交…"));

    const FShineMiniMaxH3WorkflowBuilder::FBuildResult BuildResult = FShineMiniMaxH3WorkflowBuilder::Build(*WorkingCopy);
    Status.Warnings.Append(BuildResult.Warnings);
    if (!BuildResult.bSuccess)
    {
        Finish(EShineVideoTaskState::Failed, BuildResult.ErrorMessage);
        return;
    }

    LastBuiltGraphJson = BuildResult.ToJsonString();
    BuiltNodeClassTypes = BuildResult.NodeClassTypes;
    ApplyShotPlansToStatus(BuildResult.ShotPlans, Status.Shots);

    UE_LOG(LogShineVideoTask, Display, TEXT("节点图编译完成：%d 个节点、%d 段。"),
        BuildResult.NodeClassTypes.Num(), BuildResult.ShotPlans.Num());

    FShineComfyPromptSubmitRequest SubmitRequest;
    SubmitRequest.Prompt = BuildResult.Prompt;
    SubmitRequest.ClientId = FShineComfySocket::Get().GetClientId();

    FShineComfyClient::SubmitPrompt(BaseUrl, SubmitRequest, [Self = AsShared()](FShineComfyPromptSubmitResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Finish(EShineVideoTaskState::Failed, FString::Printf(TEXT("提交失败：%s"), *Result.ErrorMessage));
            return;
        }

        Self->Status.PromptId = Result.PromptId;
        if (Self->WorkingCopy.IsValid())
        {
            for (FShineVideoShot& Shot : Self->WorkingCopy->Shots)
            {
                Shot.LastPromptId = Result.PromptId;
            }
        }

        // node_errors 非空要显式提示：ComfyUI 对未知输入键是静默忽略的，
        // "提交被接受"从来都不能证明参数接对了。
        if (!Result.RawNodeErrorsJson.IsEmpty() && !Result.RawNodeErrorsJson.Equals(TEXT("{}")))
        {
            Self->Status.Warnings.Add(FString::Printf(TEXT("ComfyUI 报了 node_errors：%s"), *Result.RawNodeErrorsJson));
        }

        Self->SetState(EShineVideoTaskState::Running, FString::Printf(TEXT("已提交 %s，等待执行…"), *Result.PromptId));
    });
}

void FShineVideoTaskRunner::HandlePromptEvent(const FShineComfySocket::FPromptEvent& Event)
{
    if (Status.PromptId.IsEmpty() || !Event.PromptId.Equals(Status.PromptId))
    {
        return;
    }

    TouchActivity();

    if (Event.Type == TEXT("progress_state"))
    {
        MergeNodeStates(Event.NodeStates);
        BroadcastStatus();
        return;
    }

    if (Event.Type == TEXT("execution_success"))
    {
        // 事件只说明"跑完了"，产物与成败仍然以 history 为准。
        bExecutionFinished = true;
        ReadHistory();
        return;
    }

    if (Event.Type == TEXT("execution_error"))
    {
        FString Message = FString::Printf(TEXT("执行失败：%s"), *Event.ErrorMessage);
        if (!Event.ErrorNodeType.IsEmpty() || !Event.ErrorNodeId.IsEmpty())
        {
            Message += FString::Printf(TEXT("（节点 %s %s）"), *Event.ErrorNodeId, *Event.ErrorNodeType);
        }

        Finish(EShineVideoTaskState::Failed, Message);
        return;
    }

    if (Event.Type == TEXT("execution_interrupted"))
    {
        Finish(EShineVideoTaskState::Failed, TEXT("任务被中断。"));
        return;
    }

    BroadcastStatus();
}

void FShineVideoTaskRunner::MergeNodeStates(const TArray<FShineComfyNodeProgress>& NodeStates)
{
    for (const FShineComfyNodeProgress& Progress : NodeStates)
    {
        if (!Progress.NodeId.IsEmpty())
        {
            NodeStateById.Add(Progress.NodeId, Progress);
        }
    }

    UpdateShotStates();
}

void FShineVideoTaskRunner::UpdateShotStates()
{
    Status.NodeStates.Reset();
    NodeStateById.GenerateValueArray(Status.NodeStates);

    // 节点 id 是我们的 builder 发的号，按数值排序显示起来才跟图里的顺序一致。
    Status.NodeStates.Sort([](const FShineComfyNodeProgress& Left, const FShineComfyNodeProgress& Right)
    {
        return FCString::Atoi64(*Left.NodeId) < FCString::Atoi64(*Right.NodeId);
    });

    FString RunningNodeId;
    int32 RunningValue = 0;
    int32 RunningMax = 0;

    for (FShineVideoShotProgress& Shot : Status.Shots)
    {
        bool bAnyRunning = false;
        bool bAnyFinished = false;
        bool bAnyError = false;
        bool bSaveFinished = false;
        Shot.StepValue = 0;
        Shot.StepMax = 0;

        const FString ShotNodeIds[] = { Shot.ConditioningNodeId, Shot.SamplerNodeId, Shot.SaveVideoNodeId };
        for (const FString& NodeId : ShotNodeIds)
        {
            const FShineComfyNodeProgress* Progress = NodeStateById.Find(NodeId);
            if (!Progress)
            {
                continue;
            }

            if (Progress->IsFailed())
            {
                bAnyError = true;
            }
            if (Progress->IsFinished())
            {
                bAnyFinished = true;
            }
            if (Progress->State == TEXT("running"))
            {
                bAnyRunning = true;
            }

            if (NodeId == Shot.SamplerNodeId)
            {
                Shot.StepValue = Progress->Value;
                Shot.StepMax = Progress->Max;

                // 面板要的是"当前跑到第几步"，采样器是唯一带步进的节点，优先报它。
                if (Progress->State == TEXT("running"))
                {
                    RunningNodeId = Progress->NodeId;
                    RunningValue = Progress->Value;
                    RunningMax = Progress->Max;
                }
            }

            if (NodeId == Shot.SaveVideoNodeId && Progress->IsFinished())
            {
                bSaveFinished = true;
            }
        }

        if (bAnyError)
        {
            Shot.State = TEXT("error");
        }
        else if (bSaveFinished)
        {
            Shot.State = TEXT("finished");
        }
        else if (bAnyRunning || bAnyFinished)
        {
            // 前面的节点过了但还没轮到落盘，对这个分镜来说就是"正在跑"。
            Shot.State = TEXT("running");
        }
        else
        {
            Shot.State = TEXT("pending");
        }
    }

    if (RunningNodeId.IsEmpty())
    {
        for (const TPair<FString, FShineComfyNodeProgress>& Pair : NodeStateById)
        {
            if (Pair.Value.State == TEXT("running"))
            {
                RunningNodeId = Pair.Key;
                RunningValue = Pair.Value.Value;
                RunningMax = Pair.Value.Max;
                break;
            }
        }
    }

    Status.CurrentNodeId = RunningNodeId;
    Status.CurrentStep = RunningValue;
    Status.TotalSteps = RunningMax;
    Status.CurrentNodeClass = BuiltNodeClassTypes.Contains(RunningNodeId) ? BuiltNodeClassTypes[RunningNodeId] : FString();
}

void FShineVideoTaskRunner::ReadHistory()
{
    if (bHistoryReadInFlight || Status.PromptId.IsEmpty())
    {
        return;
    }

    bHistoryReadInFlight = true;
    LastHistoryPollSeconds = FPlatformTime::Seconds();

    if (bExecutionFinished)
    {
        SetState(EShineVideoTaskState::ReadingHistory, TEXT("回读任务结果…"));
    }

    FShineComfyClient::FetchHistoryForPrompt(BaseUrl, Status.PromptId, [Self = AsShared()](FShineComfyHistoryResult&& Result)
    {
        Self->bHistoryReadInFlight = false;

        if (!Result.bSuccess || Result.Entries.Num() == 0)
        {
            if (!Self->bExecutionFinished)
            {
                // 还在跑：history 里查不到是**正常状态**，不计失败次数。
                return;
            }

            ++Self->HistoryReadAttempts;
            if (Self->HistoryReadAttempts >= MaxHistoryReadAttempts)
            {
                Self->Finish(EShineVideoTaskState::Failed, FString::Printf(
                    TEXT("执行结束后仍读不到 history 结果（试了 %d 次）：%s"),
                    MaxHistoryReadAttempts, *Result.ErrorMessage));
                return;
            }

            // ComfyUI 写 history 和发 execution_success 之间有一点时间差，
            // 回到 Running 等下一次轮询/事件再试。
            Self->SetState(EShineVideoTaskState::Running, TEXT("等 ComfyUI 落 history…"));
            return;
        }

        Self->HandleHistoryEntry(Result.Entries[0]);
    });
}

void FShineVideoTaskRunner::HandleHistoryEntry(const FShineComfyHistoryEntry& Entry)
{
    if (Entry.bFailed)
    {
        Finish(EShineVideoTaskState::Failed, FString::Printf(TEXT("ComfyUI 报任务失败：%s"), *Entry.StatusText));
        return;
    }

    TArray<FShineComfyHistoryMedia> Videos;
    for (const FShineComfyHistoryMedia& Media : Entry.Media)
    {
        if (Media.IsVideo())
        {
            Videos.Add(Media);
        }
    }

    if (Videos.Num() == 0)
    {
        Finish(EShineVideoTaskState::Failed,
            TEXT("任务成功，但 history 里没有任何视频产物：SaveVideo 没接上，或者产物被落在别的键里了。"));
        return;
    }

    ResolveOutputs(Videos);
}

void FShineVideoTaskRunner::ResolveOutputs(const TArray<FShineComfyHistoryMedia>& Videos)
{
    SetState(EShineVideoTaskState::SavingMedia, FString::Printf(TEXT("定位 %d 段产物…"), Videos.Num()));

    struct FPendingDownload
    {
        FShineComfyHistoryMedia Media;
        FString Destination;
    };

    TArray<FPendingDownload> Downloads;

    for (const FShineComfyHistoryMedia& Media : Videos)
    {
        // 本机 ComfyUI + 配了输出目录时，文件本来就在磁盘上：直接读原文件，
        // 不复制、不下载。下载只在"ComfyUI 在别的机器上 / 输出目录没配"时才需要。
        const FString LocalPath = ShineComfyPaths::MakeLocalMediaPath(Media);
        if (!LocalPath.IsEmpty() && FPaths::FileExists(LocalPath))
        {
            RecordSavedMedia(Media, LocalPath);
            continue;
        }

        FPendingDownload Download;
        Download.Media = Media;
        Download.Destination = FPaths::Combine(
            ShineComfyPaths::GetConfiguredMediaLibraryDirectory(),
            ProjectName,
            FPaths::GetCleanFilename(Media.FileName));
        Downloads.Add(MoveTemp(Download));
    }

    if (Downloads.Num() == 0)
    {
        Finish(EShineVideoTaskState::Completed, FString::Printf(TEXT("完成：%d 段视频已就位。"), Status.OutputFiles.Num()));
        return;
    }

    PendingMediaTasks = Downloads.Num();
    for (const FPendingDownload& Download : Downloads)
    {
        FShineComfyClient::DownloadMedia(BaseUrl, Download.Media, Download.Destination,
            [Self = AsShared(), Media = Download.Media](FShineComfyMediaDownloadResult&& Result)
        {
            if (!Result.bSuccess)
            {
                Self->Status.Warnings.Add(FString::Printf(
                    TEXT("抓产物失败：%s（%s）"), *Media.BuildRelativePath(), *Result.ErrorMessage));
            }
            else
            {
                Self->RecordSavedMedia(Media, Result.LocalPath);
            }

            Self->CompleteMediaTask();
        });
    }
}

void FShineVideoTaskRunner::RecordSavedMedia(const FShineComfyHistoryMedia& Media, const FString& LocalPath)
{
    Status.OutputFiles.AddUnique(LocalPath);

    if (!Media.NodeId.IsEmpty())
    {
        for (FShineVideoShotProgress& Shot : Status.Shots)
        {
            if (Shot.SaveVideoNodeId == Media.NodeId)
            {
                Shot.OutputFiles.AddUnique(LocalPath);
                return;
            }
        }
    }

    // 兜底：节点 id 对不上（比如图被改过、history 是别处的）时按顺序补给还没产出的分镜。
    for (FShineVideoShotProgress& Shot : Status.Shots)
    {
        if (Shot.OutputFiles.Num() == 0)
        {
            Shot.OutputFiles.AddUnique(LocalPath);
            Status.Warnings.Add(FString::Printf(
                TEXT("产物 %s 在 history 里没能对上任何分镜的 SaveVideo 节点，已按顺序记到「%s」。"),
                *Media.BuildRelativePath(), *Shot.Title));
            return;
        }
    }
}

void FShineVideoTaskRunner::CompleteMediaTask()
{
    if (PendingMediaTasks > 0)
    {
        --PendingMediaTasks;
    }

    if (PendingMediaTasks > 0)
    {
        BroadcastStatus();
        return;
    }

    if (Status.OutputFiles.Num() == 0)
    {
        Finish(EShineVideoTaskState::Failed, TEXT("产物一个都没落盘。"));
        return;
    }

    Finish(EShineVideoTaskState::Completed, FString::Printf(TEXT("完成：%d 段视频已落盘。"), Status.OutputFiles.Num()));
}

bool FShineVideoTaskRunner::Tick(float DeltaTime)
{
    const double Now = FPlatformTime::Seconds();

    if (Status.State == EShineVideoTaskState::WaitingForVram)
    {
        Status.VramWaitSeconds = static_cast<int32>(Now - VramWaitStartSeconds);

        if (Now - VramWaitStartSeconds > VramWaitTimeoutSeconds)
        {
            Finish(EShineVideoTaskState::Failed, FString::Printf(
                TEXT("等了 %.0f 秒，空闲显存仍只有 %.2f GB（要求 %.1f GB）。把 Shine Comfy 设置里的最低空闲显存调低，或先腾出卡。"),
                VramWaitTimeoutSeconds, Status.VramFreeGb, Status.VramRequiredGb));
            return false;
        }

        if (Now - LastVramPollSeconds >= VramPollIntervalSeconds)
        {
            LastVramPollSeconds = Now;
            PollVram();
        }

        return true;
    }

    // history 兜底轮询：WS 事件丢了（连接建晚了、断线重连）也能收尾。
    if ((Status.State == EShineVideoTaskState::Running || Status.State == EShineVideoTaskState::ReadingHistory)
        && !Status.PromptId.IsEmpty()
        && !bHistoryReadInFlight)
    {
        const double Interval = bExecutionFinished ? HistoryPollIntervalAfterFinishSeconds : HistoryPollIntervalSeconds;
        if ((Now - LastHistoryPollSeconds) >= Interval)
        {
            ReadHistory();
        }
    }

    if (Status.IsActive() && (Now - LastActivitySeconds) > ActivityTimeoutSeconds)
    {
        Finish(EShineVideoTaskState::Failed, FString::Printf(
            TEXT("超过 %.0f 分钟没有任何执行进展，判定卡死。"), ActivityTimeoutSeconds / 60.0));
        return false;
    }

    return true;
}

void FShineVideoTaskRunner::StartTicker()
{
    if (TickerHandle.IsValid())
    {
        return;
    }

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(AsShared(), &FShineVideoTaskRunner::Tick), 1.0f);
}

void FShineVideoTaskRunner::StopTicker()
{
    if (!TickerHandle.IsValid())
    {
        return;
    }

    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    TickerHandle.Reset();
}

void FShineVideoTaskRunner::SetState(EShineVideoTaskState InState, const FString& InDetail)
{
    Status.State = InState;
    Status.Detail = InDetail;
    UE_LOG(LogShineVideoTask, Display, TEXT("[%s] %s"), ToString(InState), *InDetail);
    BroadcastStatus();
}

void FShineVideoTaskRunner::TouchActivity()
{
    LastActivitySeconds = FPlatformTime::Seconds();
}

void FShineVideoTaskRunner::BroadcastStatus()
{
    if (OnStatusChanged)
    {
        OnStatusChanged(Status);
    }
}

void FShineVideoTaskRunner::Finish(EShineVideoTaskState FinalState, const FString& Message)
{
    if (bFinishing)
    {
        return;
    }

    bFinishing = true;

    StopTicker();

    if (PromptEventHandle.IsValid())
    {
        FShineComfySocket::Get().PromptEvent.Remove(PromptEventHandle);
        PromptEventHandle.Reset();
    }

    Status.State = FinalState;
    Status.Detail = Message;

    const bool bSucceeded = FinalState == EShineVideoTaskState::Completed;
    if (bSucceeded)
    {
        Status.ErrorMessage.Reset();
    }
    else if (FinalState == EShineVideoTaskState::Failed)
    {
        Status.ErrorMessage = Message;
    }

    if (WorkingCopy.IsValid())
    {
        for (FShineVideoShotProgress& Progress : Status.Shots)
        {
            FShineVideoShot* Shot = WorkingCopy->GetShot(Progress.ShotIndex);
            if (!Shot)
            {
                continue;
            }

            Shot->LastOutputFiles = Progress.OutputFiles;
            if (bSucceeded)
            {
                Shot->LastError.Reset();
            }
            else
            {
                Shot->LastError = Message;
            }
        }
    }

    if (bSucceeded)
    {
        UE_LOG(LogShineVideoTask, Display, TEXT("任务结束：%s"), *Message);
        for (const FString& OutputFile : Status.OutputFiles)
        {
            UE_LOG(LogShineVideoTask, Display, TEXT("  产物：%s"), *OutputFile);
        }
    }
    else
    {
        UE_LOG(LogShineVideoTask, Error, TEXT("任务结束（%s）：%s"), ToString(FinalState), *Message);
    }

    BroadcastStatus();
    FinishedEvent.Broadcast();

    // 最后才把自己从自持里摘出来：上面的广播会回调到调用方，那里完全可能把
    // 唯一的外部引用也释放掉，此后再碰任何成员都是悬空的。
    TSharedPtr<FShineVideoTaskRunner> KeepAlive = MoveTemp(SelfRef);
}

FString FShineVideoTaskRunner::SaveBuiltGraphJson(const FString& InName) const
{
    if (LastBuiltGraphJson.IsEmpty())
    {
        return FString();
    }

    FString Stem;
    Stem.Reserve(InName.Len());
    for (const TCHAR Character : InName)
    {
        Stem.AppendChar((FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-')) ? Character : TEXT('_'));
    }
    if (Stem.IsEmpty())
    {
        Stem = TEXT("ShineVideo");
    }

    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ShineH3"));
    IFileManager::Get().MakeDirectory(*Directory, true);

    const FString FilePath = FPaths::Combine(Directory, FString::Printf(TEXT("%s_graph.json"), *Stem));

    // 紧凑 JSON 读起来太痛苦，落盘时重排成缩进版（出问题时要靠眼睛看这张图）。
    FString TextToWrite = LastBuiltGraphJson;
    TSharedPtr<FJsonObject> Parsed;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LastBuiltGraphJson);
    if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
    {
        FString PrettyJson;
        const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&PrettyJson);
        if (FJsonSerializer::Serialize(Parsed.ToSharedRef(), Writer))
        {
            TextToWrite = PrettyJson;
        }
    }

    if (!FFileHelper::SaveStringToFile(TextToWrite, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return FString();
    }

    return FilePath;
}

void FShineVideoTaskRunner::Cancel()
{
    if (!Status.IsActive())
    {
        return;
    }

    if (!Status.PromptId.IsEmpty())
    {
        // 已经提交了就先让 ComfyUI 停下来，否则它会继续占着卡跑完。
        FShineComfyClient::InterruptPrompt(BaseUrl, Status.PromptId,
            [](FShineComfyQueueOperationResult&&) {});
    }

    Finish(EShineVideoTaskState::Cancelled, TEXT("已取消。"));
}
