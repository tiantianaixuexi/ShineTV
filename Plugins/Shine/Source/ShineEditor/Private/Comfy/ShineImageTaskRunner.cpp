#include "Comfy/ShineImageTaskRunner.h"

#include "Comfy/ShineComfyClient.h"
#include "Comfy/ShineComfyPaths.h"
#include "EdGraph/EdGraph.h"
#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogShineImageTask, Log, All);

namespace
{
    /** 进程内唯一的活动出图任务（`Start` / `GetActive` 都通过它）。 */
    TWeakPtr<FShineImageTaskRunner> GActiveImageRunner;

    /** ComfyUI 地址的兜底值（与 UShineVideoProject::ComfyBaseUrl 的默认值一致）。 */
    const TCHAR* const DefaultComfyBaseUrl = TEXT("http://127.0.0.1:8188");

    /** 任务状态 → 画布节点上的状态色。 */
    EShineVideoNodeRuntimeState ToNodeState(EShineImageTaskState State)
    {
        switch (State)
        {
        case EShineImageTaskState::Completed:
            return EShineVideoNodeRuntimeState::Finished;
        case EShineImageTaskState::Failed:
            return EShineVideoNodeRuntimeState::Failed;
        case EShineImageTaskState::FreeingVram:
        case EShineImageTaskState::WaitingForVram:
        case EShineImageTaskState::Uploading:
        case EShineImageTaskState::Submitting:
            // 还没提交：对这张图来说就是"排队中"。
            return EShineVideoNodeRuntimeState::Pending;
        case EShineImageTaskState::Running:
        case EShineImageTaskState::ReadingHistory:
        case EShineImageTaskState::SavingMedia:
            return EShineVideoNodeRuntimeState::Running;
        default:
            return EShineVideoNodeRuntimeState::Idle;
        }
    }
}

const TCHAR* FShineImageTaskRunner::ToString(EShineImageTaskState InState)
{
    switch (InState)
    {
    case EShineImageTaskState::Idle:           return TEXT("Idle");
    case EShineImageTaskState::FreeingVram:    return TEXT("FreeingVram");
    case EShineImageTaskState::WaitingForVram: return TEXT("WaitingForVram");
    case EShineImageTaskState::Uploading:      return TEXT("Uploading");
    case EShineImageTaskState::Submitting:     return TEXT("Submitting");
    case EShineImageTaskState::Running:        return TEXT("Running");
    case EShineImageTaskState::ReadingHistory: return TEXT("ReadingHistory");
    case EShineImageTaskState::SavingMedia:    return TEXT("SavingMedia");
    case EShineImageTaskState::Completed:      return TEXT("Completed");
    case EShineImageTaskState::Failed:         return TEXT("Failed");
    case EShineImageTaskState::Cancelled:      return TEXT("Cancelled");
    default:                                   return TEXT("Unknown");
    }
}

TSharedPtr<FShineImageTaskRunner> FShineImageTaskRunner::GetActive()
{
    return GActiveImageRunner.Pin();
}

TSharedPtr<FShineImageTaskRunner> FShineImageTaskRunner::Start(const FShineImageTaskRequest& InRequest, bool bDryRun, FString& OutError)
{
    OutError.Reset();

    if (const TSharedPtr<FShineImageTaskRunner> Active = GetActive())
    {
        OutError = FString::Printf(TEXT("已经有一个出图任务在跑（%s），等它结束再来。"),
            ToString(Active->GetStatus().State));
        return nullptr;
    }

    const TSharedPtr<FShineImageTaskRunner> Runner = MakeShared<FShineImageTaskRunner>();

    // 先占坑再 Run：Run 里的同步失败（参数不合法）会直接走 Finish，而 Finish 会摘掉它。
    GActiveImageRunner = Runner;

    if (!Runner->Run(InRequest, bDryRun))
    {
        OutError = Runner->GetStatus().ErrorMessage;
        if (OutError.IsEmpty())
        {
            OutError = TEXT("出图任务启动失败。");
        }

        GActiveImageRunner.Reset();
        return nullptr;
    }

    return Runner;
}

FShineImageTaskRunner::FShineImageTaskRunner()
{
}

FShineImageTaskRunner::~FShineImageTaskRunner()
{
    StopTicker();

    if (PromptEventHandle.IsValid())
    {
        FShineComfySocket::Get().PromptEvent.Remove(PromptEventHandle);
        PromptEventHandle.Reset();
    }
}

void FShineImageTaskRunner::MarkImmediateFailure(const FString& Message)
{
    Status.State = EShineImageTaskState::Failed;
    Status.Detail = Message;
    Status.ErrorMessage = Message;
    UE_LOG(LogShineImageTask, Error, TEXT("%s"), *Message);
    ApplyStatusToTargetNode(true);
    BroadcastStatus();
}

bool FShineImageTaskRunner::Run(const FShineImageTaskRequest& InRequest, bool bDryRun)
{
    if (Status.IsActive())
    {
        UE_LOG(LogShineImageTask, Warning, TEXT("上一次出图还没结束（%s），已拒绝新的提交。"), ToString(Status.State));
        return false;
    }

    Status = FShineImageTaskStatus();
    NodeStateById.Reset();
    BuiltNodeClassTypes.Reset();
    UploadedFileNames.Reset();
    UploadOrder.Reset();
    ColorImageLocalPath.Reset();
    UploadIndex = 0;
    HistoryReadAttempts = 0;
    LastHistoryPollSeconds = 0.0;
    LastCanvasRefreshSeconds = 0.0;
    bHistoryReadInFlight = false;
    bExecutionFinished = false;
    bFinishing = false;
    PendingMediaTasks = 0;
    LastBuiltGraphJson.Reset();
    SamplerNodeId.Reset();
    SaveImageNodeId.Reset();
    SelfRef.Reset();

    Request = InRequest;
    Request.Workflow.ColorImageName.Reset();
    Request.Workflow.DepthImageName.Reset();
    Request.Workflow.NormalImageName.Reset();

    if (Request.Workflow.OutputPrefix.TrimStartAndEnd().IsEmpty())
    {
        Request.Workflow.OutputPrefix = TEXT("Shine/SceneToImage");
    }

    BaseUrl = FShineComfyClient::NormalizeBaseUrl(Request.BaseUrl.IsEmpty() ? DefaultComfyBaseUrl : Request.BaseUrl);

    Status.VramRequiredGb = MinFreeVramGb;
    RequiredVramBytes = MinFreeVramGb * 1024.0 * 1024.0 * 1024.0;

    if (!ResolveInputImages(bDryRun))
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

    SetState(EShineImageTaskState::FreeingVram, TEXT("卸载 ComfyUI 缓存，腾显存…"));
    FShineComfyClient::FreeVram(BaseUrl, [Self = AsShared()](FShineComfyFreeVramResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Finish(EShineImageTaskState::Failed, FString::Printf(TEXT("/api/free 失败：%s"), *Result.ErrorMessage));
            return;
        }

        Self->BeginVramWait();
    });

    return true;
}

bool FShineImageTaskRunner::ResolveInputImages(bool bDryRun)
{
    UploadOrder.Reset();

    const FString ColorPath = FPaths::ConvertRelativePathToFull(Request.ColorImagePath.TrimStartAndEnd());
    if (ColorPath.IsEmpty())
    {
        MarkImmediateFailure(TEXT("没有颜色参考图：先给「分镜图」节点接一张图（或填「图片路径」）。"));
        return false;
    }

    if (!FPaths::FileExists(ColorPath))
    {
        MarkImmediateFailure(FString::Printf(TEXT("颜色参考图在磁盘上找不到：%s"), *ColorPath));
        return false;
    }

    ColorImageLocalPath = ColorPath;
    UploadOrder.Add(ColorPath);

    // 深度 / 法线：没显式给就找兄弟文件。找不到就少接一支 ControlNet（builder 会照实少接）。
    struct FChannel
    {
        FString* Path;
        const TCHAR* Suffix;
        const TCHAR* Label;
    };

    const FChannel Channels[] =
    {
        { &Request.DepthImagePath, TEXT("_Depth"), TEXT("深度") },
        { &Request.NormalImagePath, TEXT("_Normal"), TEXT("法线") }
    };

    for (const FChannel& Channel : Channels)
    {
        FString Path = Channel.Path->TrimStartAndEnd();

        if (Path.IsEmpty() && Request.bAutoFindSiblings)
        {
            Path = ShineComfyPaths::FindSiblingImage(ColorPath, Channel.Suffix);
            if (Path.IsEmpty())
            {
                Status.Warnings.Add(FString::Printf(
                    TEXT("颜色图旁边没有 %s%s 兄弟文件，这一支不接 ControlNet。"),
                    Channel.Suffix, *FPaths::GetExtension(ColorPath, true)));
            }
            else
            {
                Status.Warnings.Add(FString::Printf(TEXT("自动接上%s图：%s"), Channel.Label, *FPaths::GetCleanFilename(Path)));
            }
        }

        Channel.Path->Reset();
        if (Path.IsEmpty())
        {
            continue;
        }

        const FString Absolute = FPaths::ConvertRelativePathToFull(Path);
        if (!FPaths::FileExists(Absolute))
        {
            MarkImmediateFailure(FString::Printf(TEXT("%s图在磁盘上找不到：%s"), Channel.Label, *Absolute));
            return false;
        }

        *Channel.Path = Absolute;
        UploadOrder.Add(Absolute);
    }

    if (bDryRun)
    {
        // dry-run 不传文件：图名直接用本地文件名。场景捕获本来就落在 ComfyUI 的
        // input 目录下、文件名就是 ComfyUI 认的那个，所以对拍依然有意义。
        Request.Workflow.ColorImageName = FPaths::GetCleanFilename(ColorPath);
        Request.Workflow.DepthImageName = Request.DepthImagePath.IsEmpty()
            ? FString() : FPaths::GetCleanFilename(Request.DepthImagePath);
        Request.Workflow.NormalImageName = Request.NormalImagePath.IsEmpty()
            ? FString() : FPaths::GetCleanFilename(Request.NormalImagePath);
    }

    return true;
}

void FShineImageTaskRunner::BuildOnly()
{
    SetState(EShineImageTaskState::Submitting, TEXT("dry-run：只编译节点图…"));

    const FShineSceneToImageWorkflowBuilder::FBuildResult BuildResult = FShineSceneToImageWorkflowBuilder::Build(Request.Workflow);
    Status.Warnings.Append(BuildResult.Warnings);
    if (!BuildResult.bSuccess)
    {
        Finish(EShineImageTaskState::Failed, BuildResult.ErrorMessage);
        return;
    }

    LastBuiltGraphJson = BuildResult.ToJsonString();
    BuiltNodeClassTypes = BuildResult.NodeClassTypes;
    SamplerNodeId = BuildResult.SamplerNodeId;
    SaveImageNodeId = BuildResult.SaveImageNodeId;

    Finish(EShineImageTaskState::Completed, FString::Printf(
        TEXT("dry-run：编译出 %d 个节点（SaveImage=%s），未提交。"),
        BuildResult.NodeClassTypes.Num(), *BuildResult.SaveImageNodeId));
}

void FShineImageTaskRunner::BeginVramWait()
{
    VramWaitStartSeconds = FPlatformTime::Seconds();
    LastVramPollSeconds = VramWaitStartSeconds;
    Status.VramWaitSeconds = 0;
    SetState(EShineImageTaskState::WaitingForVram, TEXT("检查空闲显存…"));
    PollVram();
}

void FShineImageTaskRunner::PollVram()
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
            UE_LOG(LogShineImageTask, Display, TEXT("显存门禁通过：空闲 %.2f GB（要求 %.1f GB）。"),
                Self->Status.VramFreeGb, Self->Status.VramRequiredGb);

            Self->UploadIndex = 0;
            Self->UploadNextImage();
            return;
        }

        Self->SetState(EShineImageTaskState::WaitingForVram, FString::Printf(
            TEXT("等空闲显存：%.2f / %.1f GB（已等 %d 秒）"),
            Self->Status.VramFreeGb, Self->Status.VramRequiredGb, Self->Status.VramWaitSeconds));
    });
}

void FShineImageTaskRunner::UploadNextImage()
{
    if (UploadIndex >= UploadOrder.Num())
    {
        BuildAndSubmit();
        return;
    }

    const FString LocalPath = UploadOrder[UploadIndex];

    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *LocalPath))
    {
        Finish(EShineImageTaskState::Failed, FString::Printf(TEXT("读不出参考图：%s"), *LocalPath));
        return;
    }

    const FString UploadName = ShineComfyPaths::MakeUploadFileName(LocalPath);
    SetState(EShineImageTaskState::Uploading, FString::Printf(
        TEXT("上传参考图 %d/%d：%s"), UploadIndex + 1, UploadOrder.Num(), *FPaths::GetCleanFilename(LocalPath)));

    FShineComfyClient::UploadImage(BaseUrl, UploadName, FileBytes, false,
        [Self = AsShared(), LocalPath, UploadName](FShineComfyUploadResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Finish(EShineImageTaskState::Failed, FString::Printf(
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

void FShineImageTaskRunner::BuildAndSubmit()
{
    // 上传完才知道 ComfyUI 那边叫什么，builder 只认这个名字。
    const FString* ColorName = UploadedFileNames.Find(ColorImageLocalPath);
    if (!ColorName || ColorName->IsEmpty())
    {
        Finish(EShineImageTaskState::Failed, TEXT("颜色参考图没有上传成功，无法编译节点图。"));
        return;
    }

    Request.Workflow.ColorImageName = *ColorName;

    auto ResolveUploaded = [this](const FString& LocalPath) -> FString
    {
        if (LocalPath.IsEmpty())
        {
            return FString();
        }

        const FString* Found = UploadedFileNames.Find(LocalPath);
        return Found ? *Found : FString();
    };

    Request.Workflow.DepthImageName = ResolveUploaded(Request.DepthImagePath);
    Request.Workflow.NormalImageName = ResolveUploaded(Request.NormalImagePath);

    SetState(EShineImageTaskState::Submitting, TEXT("编译节点图并提交…"));

    const FShineSceneToImageWorkflowBuilder::FBuildResult BuildResult = FShineSceneToImageWorkflowBuilder::Build(Request.Workflow);
    Status.Warnings.Append(BuildResult.Warnings);
    if (!BuildResult.bSuccess)
    {
        Finish(EShineImageTaskState::Failed, BuildResult.ErrorMessage);
        return;
    }

    LastBuiltGraphJson = BuildResult.ToJsonString();
    BuiltNodeClassTypes = BuildResult.NodeClassTypes;
    SamplerNodeId = BuildResult.SamplerNodeId;
    SaveImageNodeId = BuildResult.SaveImageNodeId;

    UE_LOG(LogShineImageTask, Display, TEXT("出图节点图编译完成：%d 个节点（SaveImage=%s）。"),
        BuildResult.NodeClassTypes.Num(), *SaveImageNodeId);

    FShineComfyPromptSubmitRequest SubmitRequest;
    SubmitRequest.Prompt = BuildResult.Prompt;
    SubmitRequest.ClientId = FShineComfySocket::Get().GetClientId();

    FShineComfyClient::SubmitPrompt(BaseUrl, SubmitRequest, [Self = AsShared()](FShineComfyPromptSubmitResult&& Result)
    {
        if (!Result.bSuccess)
        {
            Self->Finish(EShineImageTaskState::Failed, FString::Printf(TEXT("提交失败：%s"), *Result.ErrorMessage));
            return;
        }

        Self->Status.PromptId = Result.PromptId;

        // node_errors 非空要显式提示：ComfyUI 对未知输入键是静默忽略的，
        // "提交被接受"从来都不能证明参数接对了。
        if (!Result.RawNodeErrorsJson.IsEmpty() && !Result.RawNodeErrorsJson.Equals(TEXT("{}")))
        {
            Self->Status.Warnings.Add(FString::Printf(TEXT("ComfyUI 报了 node_errors：%s"), *Result.RawNodeErrorsJson));
        }

        Self->SetState(EShineImageTaskState::Running, FString::Printf(TEXT("已提交 %s，等待执行…"), *Result.PromptId));
    });
}

void FShineImageTaskRunner::HandlePromptEvent(const FShineComfySocket::FPromptEvent& Event)
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

        Finish(EShineImageTaskState::Failed, Message);
        return;
    }

    if (Event.Type == TEXT("execution_interrupted"))
    {
        Finish(EShineImageTaskState::Failed, TEXT("任务被中断。"));
        return;
    }

    BroadcastStatus();
}

void FShineImageTaskRunner::MergeNodeStates(const TArray<FShineComfyNodeProgress>& NodeStates)
{
    for (const FShineComfyNodeProgress& Progress : NodeStates)
    {
        if (!Progress.NodeId.IsEmpty())
        {
            NodeStateById.Add(Progress.NodeId, Progress);
        }
    }

    // 出图只有一颗采样器（KSampler），步进只在它身上：优先报它，否则退到第一个在跑的节点。
    FString RunningNodeId;
    int32 RunningValue = 0;
    int32 RunningMax = 0;

    if (const FShineComfyNodeProgress* Sampler = NodeStateById.Find(SamplerNodeId))
    {
        if (Sampler->State == TEXT("running"))
        {
            RunningNodeId = Sampler->NodeId;
            RunningValue = Sampler->Value;
            RunningMax = Sampler->Max;
        }
    }

    if (RunningNodeId.IsEmpty())
    {
        for (const TPair<FString, FShineComfyNodeProgress>& Pair : NodeStateById)
        {
            if (Pair.Value.State != TEXT("running"))
            {
                continue;
            }

            RunningNodeId = Pair.Key;
            RunningValue = Pair.Value.Value;
            RunningMax = Pair.Value.Max;
            break;
        }
    }

    Status.CurrentNodeId = RunningNodeId;
    Status.CurrentStep = RunningValue;
    Status.TotalSteps = RunningMax;
    Status.CurrentNodeClass = BuiltNodeClassTypes.Contains(RunningNodeId) ? BuiltNodeClassTypes[RunningNodeId] : FString();

    ApplyStatusToTargetNode(false);
}

void FShineImageTaskRunner::ReadHistory()
{
    if (bHistoryReadInFlight || Status.PromptId.IsEmpty())
    {
        return;
    }

    bHistoryReadInFlight = true;
    LastHistoryPollSeconds = FPlatformTime::Seconds();

    if (bExecutionFinished)
    {
        SetState(EShineImageTaskState::ReadingHistory, TEXT("回读生成结果…"));
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
                Self->Finish(EShineImageTaskState::Failed, FString::Printf(
                    TEXT("执行结束后仍读不到 history 结果（试了 %d 次）：%s"),
                    MaxHistoryReadAttempts, *Result.ErrorMessage));
                return;
            }

            Self->SetState(EShineImageTaskState::Running, TEXT("等 ComfyUI 落 history…"));
            return;
        }

        Self->HandleHistoryEntry(Result.Entries[0]);
    });
}

void FShineImageTaskRunner::HandleHistoryEntry(const FShineComfyHistoryEntry& Entry)
{
    if (Entry.bFailed)
    {
        Finish(EShineImageTaskState::Failed, FString::Printf(TEXT("ComfyUI 报任务失败：%s"), *Entry.StatusText));
        return;
    }

    // 只认 SaveImage 那一条：图里同时接了 PreviewImage，它会再写一份 temp 图。
    TArray<FShineComfyHistoryMedia> Images;
    for (const FShineComfyHistoryMedia& Media : Entry.Media)
    {
        if (Media.IsImage() && !SaveImageNodeId.IsEmpty() && Media.NodeId == SaveImageNodeId)
        {
            Images.Add(Media);
        }
    }

    if (Images.Num() == 0)
    {
        for (const FShineComfyHistoryMedia& Media : Entry.Media)
        {
            if (Media.IsImage() && Media.Type != TEXT("temp"))
            {
                Images.Add(Media);
            }
        }

        if (Images.Num() > 0)
        {
            Status.Warnings.Add(FString::Printf(
                TEXT("history 里没有节点 %s 的产物（图被改过？），已按「非 temp 的图片」兜底认领。"),
                *SaveImageNodeId));
        }
    }

    if (Images.Num() == 0)
    {
        Finish(EShineImageTaskState::Failed,
            TEXT("任务成功，但 history 里没有任何图片产物：SaveImage 没接上，或者产物被落在别的键里了。"));
        return;
    }

    ResolveOutputs(Images);
}

void FShineImageTaskRunner::ResolveOutputs(const TArray<FShineComfyHistoryMedia>& Images)
{
    SetState(EShineImageTaskState::SavingMedia, FString::Printf(TEXT("定位 %d 张产物…"), Images.Num()));

    struct FPendingDownload
    {
        FShineComfyHistoryMedia Media;
        FString Destination;
    };

    TArray<FPendingDownload> Downloads;
    TArray<FString> LocalPaths;

    for (const FShineComfyHistoryMedia& Media : Images)
    {
        // 本机 ComfyUI + 配了输出目录时，文件本来就在磁盘上：直接读原文件，不复制、不下载。
        const FString LocalPath = ShineComfyPaths::MakeLocalImagePath(Media.Subfolder, Media.FileName);
        if (!LocalPath.IsEmpty() && FPaths::FileExists(LocalPath))
        {
            LocalPaths.AddUnique(LocalPath);
            continue;
        }

        FPendingDownload Download;
        Download.Media = Media;

        const FString DirectoryName = Request.OutputDirectoryName.IsEmpty()
            ? TEXT("ShineStoryboard")
            : Request.OutputDirectoryName;
        Download.Destination = FPaths::Combine(
            ShineComfyPaths::GetConfiguredMediaLibraryDirectory(),
            DirectoryName,
            FPaths::GetCleanFilename(Media.FileName));
        Downloads.Add(MoveTemp(Download));
    }

    Status.OutputFiles = LocalPaths;

    if (Downloads.Num() == 0)
    {
        Finish(EShineImageTaskState::Completed, FString::Printf(TEXT("完成：%d 张图已就位。"), Status.OutputFiles.Num()));
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
                Self->Status.OutputFiles.AddUnique(Result.LocalPath);
            }

            Self->CompleteMediaTask();
        });
    }
}

void FShineImageTaskRunner::CompleteMediaTask()
{
    if (PendingMediaTasks > 0)
    {
        --PendingMediaTasks;
    }

    if (PendingMediaTasks > 0)
    {
        BroadcastStatus();
        ApplyStatusToTargetNode(false);
        return;
    }

    if (Status.OutputFiles.Num() == 0)
    {
        Finish(EShineImageTaskState::Failed, TEXT("产物一张都没落盘。"));
        return;
    }

    Finish(EShineImageTaskState::Completed, FString::Printf(TEXT("完成：%d 张图已落盘。"), Status.OutputFiles.Num()));
}

void FShineImageTaskRunner::ApplyStatusToTargetNode(bool bForceCanvasRefresh)
{
    // 画布可能已经被关掉了（资产编辑器关了），节点也随时可能被 GC。
    UShineVideoShotImageGraphNode* TargetNode = Request.TargetNode.Get();
    if (!TargetNode)
    {
        return;
    }

    TargetNode->SetRuntimeState(ToNodeState(Status.State), Status.Detail, Status.CurrentStep, Status.TotalSteps);

    // 节点身体上的文字是靠 Slate 控件绑定读运行期字段的，只有重建控件才会立刻重画；
    // 而重建（NotifyGraphChanged → PurgeVisualRepresentation）会把整张画布的节点控件
    // 拆一遍，所以这里节流到一秒一次——逐节点进度一秒能来好几条。
    const double Now = FPlatformTime::Seconds();
    if (!bForceCanvasRefresh && (Now - LastCanvasRefreshSeconds) < CanvasRefreshIntervalSeconds)
    {
        return;
    }

    LastCanvasRefreshSeconds = Now;
    if (UEdGraph* Graph = TargetNode->GetGraph())
    {
        Graph->NotifyGraphChanged();
    }
}

void FShineImageTaskRunner::WriteOutputsToTargetNode()
{
    UShineVideoShotImageGraphNode* TargetNode = Request.TargetNode.Get();
    if (!TargetNode || Status.OutputFiles.Num() == 0)
    {
        return;
    }

    // 存进资产的字段：关掉编辑器再打开，这张图上还看得见上一次出的图。
    TargetNode->SetResultImagePaths(Status.OutputFiles);
}

bool FShineImageTaskRunner::Tick(float DeltaTime)
{
    const double Now = FPlatformTime::Seconds();

    if (Status.State == EShineImageTaskState::WaitingForVram)
    {
        Status.VramWaitSeconds = static_cast<int32>(Now - VramWaitStartSeconds);

        if (Now - VramWaitStartSeconds > VramWaitTimeoutSeconds)
        {
            Finish(EShineImageTaskState::Failed, FString::Printf(
                TEXT("等了 %.0f 秒，空闲显存仍只有 %.2f GB（要求 %.1f GB）。先腾出卡再来。"),
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
    if ((Status.State == EShineImageTaskState::Running || Status.State == EShineImageTaskState::ReadingHistory)
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
        Finish(EShineImageTaskState::Failed, FString::Printf(
            TEXT("超过 %.0f 分钟没有任何执行进展，判定卡死。"), ActivityTimeoutSeconds / 60.0));
        return false;
    }

    return true;
}

void FShineImageTaskRunner::StartTicker()
{
    if (TickerHandle.IsValid())
    {
        return;
    }

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(AsShared(), &FShineImageTaskRunner::Tick), 1.0f);
}

void FShineImageTaskRunner::StopTicker()
{
    if (!TickerHandle.IsValid())
    {
        return;
    }

    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    TickerHandle.Reset();
}

void FShineImageTaskRunner::SetState(EShineImageTaskState InState, const FString& InDetail)
{
    Status.State = InState;
    Status.Detail = InDetail;
    UE_LOG(LogShineImageTask, Display, TEXT("[%s] %s"), ToString(InState), *InDetail);
    ApplyStatusToTargetNode(true);
    BroadcastStatus();
}

void FShineImageTaskRunner::TouchActivity()
{
    LastActivitySeconds = FPlatformTime::Seconds();
}

void FShineImageTaskRunner::BroadcastStatus()
{
    if (OnStatusChanged)
    {
        OnStatusChanged(Status);
    }
}

void FShineImageTaskRunner::Finish(EShineImageTaskState FinalState, const FString& Message)
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

    const bool bSucceeded = FinalState == EShineImageTaskState::Completed;
    if (bSucceeded)
    {
        Status.ErrorMessage.Reset();
        WriteOutputsToTargetNode();
    }
    else if (FinalState == EShineImageTaskState::Failed)
    {
        Status.ErrorMessage = Message;
    }

    ApplyStatusToTargetNode(true);

    if (bSucceeded)
    {
        UE_LOG(LogShineImageTask, Display, TEXT("任务结束：%s"), *Message);
        for (const FString& OutputFile : Status.OutputFiles)
        {
            UE_LOG(LogShineImageTask, Display, TEXT("  产物：%s"), *OutputFile);
        }
    }
    else
    {
        UE_LOG(LogShineImageTask, Error, TEXT("任务结束（%s）：%s"), ToString(FinalState), *Message);
    }

    // 先摘掉"活动任务"这个坑，再广播：回调里完全可能立刻起下一个出图任务，
    // 坑不放掉的话它会被自己的上一个任务挡住。
    if (GActiveImageRunner.Pin().Get() == this)
    {
        GActiveImageRunner.Reset();
    }

    BroadcastStatus();
    FinishedEvent.Broadcast();

    // 最后才把自己从自持里摘出来：上面的广播会回调到调用方，那里完全可能把
    // 唯一的外部引用也释放掉，此后再碰任何成员都是悬空的。
    TSharedPtr<FShineImageTaskRunner> KeepAlive = MoveTemp(SelfRef);
}

FString FShineImageTaskRunner::SaveBuiltGraphJson(const FString& InName) const
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
        Stem = TEXT("ShineStoryboard");
    }

    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ShineH3"));
    IFileManager::Get().MakeDirectory(*Directory, true);

    const FString FilePath = FPaths::Combine(Directory, FString::Printf(TEXT("%s_image_graph.json"), *Stem));

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

void FShineImageTaskRunner::Cancel()
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

    Finish(EShineImageTaskState::Cancelled, TEXT("已取消。"));
}
