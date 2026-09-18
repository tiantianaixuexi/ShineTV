#include "Paint/ShineAIPaintService.h"

#include "Comfy/ShineComfyClient.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Http/ShineHttpClient.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Comfy/ShineComfySocket.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    /** ComfyUI 轮询间隔（秒）。 */
    constexpr float GPollIntervalSeconds = 1.5f;

    /** 最长轮询次数，约 15 分钟。 */
    constexpr int32 GMaxPollAttempts = 600;

    /** WebSocket 静默多久（秒）就退回轮询兜底。 */
    constexpr float GSocketSilentTimeoutSeconds = 20.0f;

    bool EncodePngInternal(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPngBytes)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return false;
        }

        TArray64<uint8> PngData;
        FImageUtils::PNGCompressImageArray(
            Width,
            Height,
            TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()),
            PngData);

        if (PngData.Num() <= 0)
        {
            return false;
        }

        OutPngBytes.SetNumUninitialized(static_cast<int32>(PngData.Num()));
        FMemory::Memcpy(OutPngBytes.GetData(), PngData.GetData(), PngData.Num());
        return true;
    }

    FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
    {
        FString Output;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    FString MakeUniqueUploadName(const FString& Prefix)
    {
        // ComfyUI 用文件名做输入缓存键，同名会被复用，所以每次都换名字。
        return FString::Printf(TEXT("%s_%lld.png"), *Prefix, FDateTime::UtcNow().GetTicks());
    }

    /** 单节点 + 其 inputs 子对象，方便拼 prompt graph。 */
    struct FPromptNode
    {
        TSharedPtr<FJsonObject> Node;
        TSharedPtr<FJsonObject> Inputs;
    };

    FPromptNode MakeNode(const FString& ClassType)
    {
        FPromptNode Result;
        Result.Node = MakeShared<FJsonObject>();
        Result.Inputs = MakeShared<FJsonObject>();
        Result.Node->SetStringField(TEXT("class_type"), ClassType);
        Result.Node->SetObjectField(TEXT("inputs"), Result.Inputs);
        return Result;
    }

    void LinkInput(const FPromptNode& Node, const FString& InputName, const FString& FromNodeId, int32 OutputSlot)
    {
        TArray<TSharedPtr<FJsonValue>> LinkValue;
        LinkValue.Add(MakeShared<FJsonValueString>(FromNodeId));
        LinkValue.Add(MakeShared<FJsonValueNumber>(OutputSlot));
        Node.Inputs->SetArrayField(InputName, LinkValue);
    }

    /** 拼一个标准 inpaint 工作流（全部是 ComfyUI 核心节点）。 */
    TSharedPtr<FJsonObject> BuildInpaintPromptGraph(const FShineAIPaintSettings& Settings, const FString& ImageName, const FString& MaskName)
    {
        const int32 Seed = Settings.Seed >= 0
            ? Settings.Seed
            : FMath::RandRange(1, TNumericLimits<int32>::Max() - 1);

        const FPromptNode Checkpoint = MakeNode(TEXT("CheckpointLoaderSimple"));
        Checkpoint.Inputs->SetStringField(TEXT("ckpt_name"), Settings.CheckpointName);

        const FPromptNode LoadImage = MakeNode(TEXT("LoadImage"));
        LoadImage.Inputs->SetStringField(TEXT("image"), ImageName);

        const FPromptNode LoadMask = MakeNode(TEXT("LoadImageMask"));
        LoadMask.Inputs->SetStringField(TEXT("image"), MaskName);
        LoadMask.Inputs->SetStringField(TEXT("channel"), TEXT("red"));

        const FPromptNode EncodeInpaint = MakeNode(TEXT("VAEEncodeForInpaint"));
        LinkInput(EncodeInpaint, TEXT("pixels"), TEXT("2"), 0);
        LinkInput(EncodeInpaint, TEXT("vae"), TEXT("1"), 2);
        LinkInput(EncodeInpaint, TEXT("mask"), TEXT("3"), 0);
        EncodeInpaint.Inputs->SetNumberField(TEXT("grow_mask_by"), Settings.GrowMaskBy);

        const FPromptNode Positive = MakeNode(TEXT("CLIPTextEncode"));
        Positive.Inputs->SetStringField(TEXT("text"), Settings.Prompt);
        LinkInput(Positive, TEXT("clip"), TEXT("1"), 1);

        const FPromptNode Negative = MakeNode(TEXT("CLIPTextEncode"));
        Negative.Inputs->SetStringField(TEXT("text"), Settings.NegativePrompt);
        LinkInput(Negative, TEXT("clip"), TEXT("1"), 1);

        const FPromptNode Sampler = MakeNode(TEXT("KSampler"));
        Sampler.Inputs->SetNumberField(TEXT("seed"), Seed);
        Sampler.Inputs->SetNumberField(TEXT("steps"), FMath::Clamp(Settings.Steps, 1, 200));
        Sampler.Inputs->SetNumberField(TEXT("cfg"), FMath::Clamp(Settings.CFG, 0.1f, 30.0f));
        Sampler.Inputs->SetStringField(TEXT("sampler_name"), TEXT("dpmpp_2m"));
        Sampler.Inputs->SetStringField(TEXT("scheduler"), TEXT("karras"));
        Sampler.Inputs->SetNumberField(TEXT("denoise"), FMath::Clamp(Settings.Denoise, 0.05f, 1.0f));
        LinkInput(Sampler, TEXT("model"), TEXT("1"), 0);
        LinkInput(Sampler, TEXT("positive"), TEXT("5"), 0);
        LinkInput(Sampler, TEXT("negative"), TEXT("6"), 0);
        LinkInput(Sampler, TEXT("latent_image"), TEXT("4"), 0);

        const FPromptNode Decode = MakeNode(TEXT("VAEDecode"));
        LinkInput(Decode, TEXT("samples"), TEXT("7"), 0);
        LinkInput(Decode, TEXT("vae"), TEXT("1"), 2);

        const FPromptNode Save = MakeNode(TEXT("SaveImage"));
        Save.Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ShineAIPaint"));
        LinkInput(Save, TEXT("images"), TEXT("8"), 0);

        TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
        Graph->SetObjectField(TEXT("1"), Checkpoint.Node);
        Graph->SetObjectField(TEXT("2"), LoadImage.Node);
        Graph->SetObjectField(TEXT("3"), LoadMask.Node);
        Graph->SetObjectField(TEXT("4"), EncodeInpaint.Node);
        Graph->SetObjectField(TEXT("5"), Positive.Node);
        Graph->SetObjectField(TEXT("6"), Negative.Node);
        Graph->SetObjectField(TEXT("7"), Sampler.Node);
        Graph->SetObjectField(TEXT("8"), Decode.Node);
        Graph->SetObjectField(TEXT("9"), Save.Node);
        return Graph;
    }

    /** 在 history 响应里递归找第一张输出图。 */
    void FindFirstImageRecursive(
        const TSharedPtr<FJsonObject>& Object,
        FString& OutFileName,
        FString& OutSubfolder,
        FString& OutType,
        bool& bOutFound)
    {
        if (!Object.IsValid() || bOutFound)
        {
            return;
        }

        const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
        if (Object->TryGetArrayField(TEXT("images"), Images) && Images)
        {
            for (const TSharedPtr<FJsonValue>& Value : *Images)
            {
                const TSharedPtr<FJsonObject>* ImageObject = nullptr;
                if (Value.IsValid() && Value->TryGetObject(ImageObject) && ImageObject && (*ImageObject)->HasTypedField<EJson::String>(TEXT("filename")))
                {
                    (*ImageObject)->TryGetStringField(TEXT("filename"), OutFileName);
                    (*ImageObject)->TryGetStringField(TEXT("subfolder"), OutSubfolder);
                    (*ImageObject)->TryGetStringField(TEXT("type"), OutType);
                    bOutFound = true;
                    return;
                }
            }
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
        {
            const TSharedPtr<FJsonObject>* Child = nullptr;
            if (Pair.Value.IsValid() && Pair.Value->TryGetObject(Child) && Child)
            {
                FindFirstImageRecursive(*Child, OutFileName, OutSubfolder, OutType, bOutFound);
                if (bOutFound)
                {
                    return;
                }
            }
        }
    }

    bool FindFirstImage(
        const TSharedPtr<FJsonObject>& HistoryJson,
        FString& OutFileName,
        FString& OutSubfolder,
        FString& OutType)
    {
        bool bFound = false;
        FindFirstImageRecursive(HistoryJson, OutFileName, OutSubfolder, OutType, bFound);
        return bFound;
    }

    /** history 里的 status 是否是执行失败。 */
    bool IsHistoryStatusFailed(const TSharedPtr<FJsonObject>& HistoryJson, FString& OutErrorMessage)
    {
        if (!HistoryJson.IsValid())
        {
            return false;
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : HistoryJson->Values)
        {
            const TSharedPtr<FJsonObject>* EntryObject = nullptr;
            if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(EntryObject) || !EntryObject)
            {
                continue;
            }

            const TSharedPtr<FJsonObject>* StatusObject = nullptr;
            if ((*EntryObject)->TryGetObjectField(TEXT("status"), StatusObject) && StatusObject)
            {
                FString StatusStr;
                if ((*StatusObject)->TryGetStringField(TEXT("status_str"), StatusStr) && StatusStr == TEXT("error"))
                {
                    OutErrorMessage = TEXT("ComfyUI 执行工作流时报错，请检查 ComfyUI 控制台。");
                    return true;
                }
            }
        }

        return false;
    }

    /** ComfyUI / Generic HTTP 共用的异步任务。 */
    class FShineAIPaintJob : public TSharedFromThis<FShineAIPaintJob>
    {
    public:
        void Start(
            const TArray<FColor>& InPixels,
            int32 InWidth,
            int32 InHeight,
            const TArray<uint8>& InMaskPixels,
            const FShineAIPaintSettings& InSettings,
            FShineAIPaintService::FProgress&& InProgress,
            FShineAIPaintService::FCompletion&& InCompletion)
        {
            Pixels = InPixels;
            MaskPixels = InMaskPixels;
            Width = InWidth;
            Height = InHeight;
            Settings = InSettings;
            ProgressCallback = MoveTemp(InProgress);
            Completion = MoveTemp(InCompletion);

            if (!EncodePngInternal(Pixels, Width, Height, BasePng))
            {
                Finish(false, TEXT("画布 PNG 编码失败。"));
                return;
            }

            // inpaint 约定：白 = 让 AI 重画。内部遮罩 255 表示"画上去的遮罩"。
            const bool bWhiteIsEditable = (Settings.MaskMode == EShineAIPaintMaskMode::Protect);
            if (!ShineAIPaintImage::EncodeMaskPng(MaskPixels, Width, Height, bWhiteIsEditable, MaskPng))
            {
                Finish(false, TEXT("遮罩 PNG 编码失败。"));
                return;
            }

            AppendLog(FString::Printf(
                TEXT("画布 %dx%d，遮罩模式：%s"),
                Width,
                Height,
                Settings.MaskMode == EShineAIPaintMaskMode::Protect ? TEXT("保护（AI 不更新遮罩区）") : TEXT("可编辑（AI 只更新遮罩区）")));

            if (Settings.Backend == EShineAIPaintBackend::ComfyUI)
            {
                StartComfyUI();
            }
            else
            {
                StartGenericHttp();
            }
        }

    private:
        void AppendLog(const FString& Line)
        {
            if (!LogText.IsEmpty())
            {
                LogText += TEXT("\n");
            }
            LogText += Line;
        }

        void Finish(bool bSuccess, const FString& ErrorMessage)
        {
            if (bFinished)
            {
                return;
            }
            bFinished = true;

            if (PollHandle.IsValid())
            {
                FTSTicker::RemoveTicker(PollHandle);
                PollHandle.Reset();
            }

            if (WatchdogHandle.IsValid())
            {
                FTSTicker::RemoveTicker(WatchdogHandle);
                WatchdogHandle.Reset();
            }

            // 任务结束就退订共用连接，别让它继续往这个对象里送事件。
            if (SocketEventHandle.IsValid())
            {
                FShineComfySocket::Get().PromptEvent.Remove(SocketEventHandle);
                SocketEventHandle.Reset();
            }

            FShineAIPaintResult Result;
            Result.bSuccess = bSuccess;
            Result.ErrorMessage = ErrorMessage;
            Result.LogText = LogText;

            if (bSuccess)
            {
                Result.Pixels = MoveTemp(ResultPixels);
                Result.Width = ResultWidth;
                Result.Height = ResultHeight;
            }

            if (Completion)
            {
                FShineAIPaintService::FCompletion LocalCompletion = MoveTemp(Completion);
                Completion = nullptr;
                LocalCompletion(MoveTemp(Result));
            }
        }

        void FailWith(const FString& Message)
        {
            AppendLog(Message);
            Finish(false, Message);
        }

        void CompleteWithImage(const TArray<uint8>& PngBytes)
        {
            TArray<FColor> Decoded;
            int32 DecodedWidth = 0;
            int32 DecodedHeight = 0;
            if (!ShineAIPaintImage::DecodePng(PngBytes, Decoded, DecodedWidth, DecodedHeight))
            {
                FailWith(TEXT("AI 返回的图片解码失败（不是有效的 PNG/JPG）。"));
                return;
            }

            AppendLog(FString::Printf(TEXT("拿到结果图 %dx%d"), DecodedWidth, DecodedHeight));

            ResultPixels = MoveTemp(Decoded);
            ResultWidth = DecodedWidth;
            ResultHeight = DecodedHeight;
            Finish(true, FString());
        }

        // ---------------- ComfyUI ----------------

        void StartComfyUI()
        {
            if (Settings.CheckpointName.IsEmpty())
            {
                FailWith(TEXT("请先指定 ComfyUI 的 checkpoint 文件名。"));
                return;
            }

            BaseUrl = FShineComfyClient::NormalizeBaseUrl(Settings.ServiceUrl);
            BaseFileName = MakeUniqueUploadName(TEXT("ShinePaint"));
            MaskFileName = MakeUniqueUploadName(TEXT("ShineMask"));

            // 先把那条共用的常连 /ws 拉起来（连接是异步的，上传期间通常就建好了）。
            FShineComfySocket& Socket = FShineComfySocket::Get();
            Socket.EnsureConnected(BaseUrl);
            ClientId = Socket.GetClientId();

            AppendLog(FString::Printf(TEXT("上传原图到 %s"), *BaseUrl));

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineComfyClient::UploadImage(BaseUrl, BaseFileName, BasePng, false, [Self](FShineComfyUploadResult&& Result)
            {
                if (!Result.bSuccess)
                {
                    Self->FailWith(FString::Printf(TEXT("上传原图失败：%s"), *Result.ErrorMessage));
                    return;
                }

                Self->BaseFileName = Result.Name;
                Self->UploadMask();
            });
        }

        void UploadMask()
        {
            AppendLog(TEXT("上传遮罩…"));

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineComfyClient::UploadImage(BaseUrl, MaskFileName, MaskPng, true, [Self](FShineComfyUploadResult&& Result)
            {
                if (!Result.bSuccess)
                {
                    Self->FailWith(FString::Printf(TEXT("上传遮罩失败：%s"), *Result.ErrorMessage));
                    return;
                }

                Self->MaskFileName = Result.Name;
                Self->SubmitPrompt();
            });
        }

        void SubmitPrompt()
        {
            AppendLog(TEXT("提交 inpaint 工作流…"));

            FShineComfySocket& Socket = FShineComfySocket::Get();
            bUsingWebSocket = Socket.IsConnected();

            FShineComfyPromptSubmitRequest Request;
            Request.Prompt = BuildInpaintPromptGraph(Settings, BaseFileName, MaskFileName);
            // 带上和那条常连一样的 clientId，ComfyUI 才会把这次的事件推到那条连接上。
            Request.ClientId = ClientId;

            if (bUsingWebSocket)
            {
                // 提交前就订阅：任务可能在响应回来之前就开始跑了，晚订阅会漏事件。
                TWeakPtr<FShineAIPaintJob> WeakSelf = AsShared();
                SocketEventHandle = Socket.PromptEvent.AddLambda([WeakSelf](const FShineComfySocket::FPromptEvent& Event)
                {
                    if (const TSharedPtr<FShineAIPaintJob> Pinned = WeakSelf.Pin())
                    {
                        Pinned->HandleSocketEvent(Event);
                    }
                });
            }
            else
            {
                AppendLog(TEXT("WebSocket 未连上，先用轮询 /history 等结果。"));
            }

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineComfyClient::SubmitPrompt(BaseUrl, Request, [Self](FShineComfyPromptSubmitResult&& Result)
            {
                if (!Result.bSuccess)
                {
                    FString Message = FString::Printf(TEXT("提交工作流失败：%s"), *Result.ErrorMessage);
                    if (!Result.RawNodeErrorsJson.IsEmpty())
                    {
                        Message += TEXT("\n") + Result.RawNodeErrorsJson;
                    }
                    Self->FailWith(Message);
                    return;
                }

                Self->PromptId = Result.PromptId;
                Self->AppendLog(FString::Printf(TEXT("已入队，prompt_id=%s"), *Result.PromptId));
                Self->ReportProgress(TEXT("已入队，等待 ComfyUI 执行…"));
                // 现在的 id 才作数：把之前暂存的、属于这次任务的事件补跑一遍。
                Self->ReplayDeferredSocketEvents();

                if (Self->bUsingWebSocket)
                {
                    // 结果靠 WebSocket 事件送回来；再挂个看门狗兜底。
                    Self->StartWatchdog();
                }
                else
                {
                    Self->SchedulePolling();
                }
            });
        }

        // ---------------- WebSocket 事件 ----------------

        void HandleSocketEvent(const FShineComfySocket::FPromptEvent& Event)
        {
            if (bFinished || bResultRequested)
            {
                return;
            }

            if (PromptId.IsEmpty())
            {
                // 提交响应还没回来，还不知道自己的 prompt_id。
                // 这条连接是共用的，这会儿收到的多半是别人的任务，先存着。
                if (DeferredSocketEvents.Num() < 64)
                {
                    DeferredSocketEvents.Add(Event);
                }
                return;
            }

            // 严格按 prompt_id 过滤：同一条连接上还有 ComfyUI 主面板的任务在跑。
            if (Event.PromptId != PromptId)
            {
                return;
            }

            SecondsWithoutSocketEvent = 0.0f;

            if (Event.Type == TEXT("progress"))
            {
                if (Event.ProgressMax > 0)
                {
                    const int32 Percent = FMath::Clamp(
                        FMath::RoundToInt(100.0f * static_cast<float>(Event.ProgressValue) / static_cast<float>(Event.ProgressMax)),
                        0,
                        100);
                    // 进度走单独的通道推给 UI，不写 LogText：采样每步一条会把日志刷爆。
                    ProgressText = FString::Printf(TEXT("采样中 %d%%（%d/%d）"), Percent, Event.ProgressValue, Event.ProgressMax);
                    ReportProgress(ProgressText);
                }
                return;
            }

            if (Event.Type == TEXT("executed"))
            {
                if (!Event.ImageFileName.IsEmpty())
                {
                    SocketImageFileName = Event.ImageFileName;
                    SocketImageSubfolder = Event.ImageSubfolder;
                    SocketImageType = Event.ImageType;
                    ProgressText = TEXT("结果已生成，正在取回…");
                    ReportProgress(ProgressText);
                }
                return;
            }

            if (Event.Type == TEXT("execution_error"))
            {
                FailWith(FString::Printf(
                    TEXT("ComfyUI 执行失败：%s"),
                    Event.ErrorMessage.IsEmpty() ? TEXT("未知原因") : *Event.ErrorMessage));
                return;
            }

            if (Event.Type == TEXT("execution_success"))
            {
                if (!SocketImageFileName.IsEmpty())
                {
                    AppendLog(FString::Printf(TEXT("WebSocket 通知完成，取回 %s"), *SocketImageFileName));
                    DownloadResult(SocketImageFileName, SocketImageSubfolder, SocketImageType);
                }
                else
                {
                    // 没收到 executed 的图信息（老版本 / 被丢包）：查一次 history 补上。
                    AppendLog(TEXT("WebSocket 完成但没带输出图信息，查一次 /history 补上。"));
                    RequestHistoryOnce();
                }
            }
        }

        /** 拿到自己的 prompt_id 后，把之前暂存的事件按 id 过滤补跑一遍。 */
        void ReplayDeferredSocketEvents()
        {
            TArray<FShineComfySocket::FPromptEvent> Pending = MoveTemp(DeferredSocketEvents);
            DeferredSocketEvents.Reset();

            for (const FShineComfySocket::FPromptEvent& Event : Pending)
            {
                if (bFinished || bResultRequested)
                {
                    break;
                }

                HandleSocketEvent(Event);
            }
        }

        /**
         * 看门狗：WebSocket 静默太久就退回轮询，别让用户干等。
         * 正常跑的时候每次事件都会把计时清零，所以它不会误触发。
         */
        void StartWatchdog()
        {
            // 这里捕获 TSharedRef 同时起两个作用：
            //  1) 看门狗能活着；
            //  2) 保持住这个 Job —— 等 WebSocket 事件期间没有任何 HTTP 回调挂着，
            //     要是没人持有引用，Job 会被析构掉，事件就再也送不进来了。
            TSharedRef<FShineAIPaintJob> Self = AsShared();
            WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
                TEXT("ShineAIPaintWatchdog"),
                5.0f,
                [Self](float DeltaTime) -> bool
                {
                    if (Self->bFinished || Self->bResultRequested)
                    {
                        return false;
                    }

                    Self->SecondsWithoutSocketEvent += DeltaTime;
                    if (Self->SecondsWithoutSocketEvent >= GSocketSilentTimeoutSeconds)
                    {
                        Self->AppendLog(TEXT("WebSocket 长时间没消息，改用轮询 /history。"));
                        Self->SchedulePolling();
                        return false;
                    }

                    return true;
                });
        }

        /** 只查一次 history（WebSocket 说完成了但没给文件名时用）。 */
        void RequestHistoryOnce()
        {
            if (bFinished || bResultRequested || PromptId.IsEmpty())
            {
                return;
            }

            const FString HistoryUrl = FShineComfyClient::BuildApiUrl(BaseUrl, FString::Printf(TEXT("history/%s"), *PromptId));

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineHttpClient::GetJsonObject(HistoryUrl, [Self](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
            {
                if (Self->bFinished || Self->bResultRequested)
                {
                    return;
                }

                if (!Response.bSuccess || !JsonObject.IsValid())
                {
                    // 拿不到就只能退回轮询等一会儿。
                    Self->SchedulePolling();
                    return;
                }

                FString FileName;
                FString Subfolder;
                FString Type;
                if (!FindFirstImage(JsonObject, FileName, Subfolder, Type))
                {
                    FString StatusError;
                    if (IsHistoryStatusFailed(JsonObject, StatusError))
                    {
                        Self->FailWith(StatusError);
                    }
                    else
                    {
                        Self->SchedulePolling();
                    }
                    return;
                }

                Self->DownloadResult(FileName, Subfolder, Type);
            });
        }

        void SchedulePolling()
        {
            TSharedRef<FShineAIPaintJob> Self = AsShared();
            PollHandle = FTSTicker::GetCoreTicker().AddTicker(
                TEXT("ShineAIPaintPoll"),
                GPollIntervalSeconds,
                [Self](float /*DeltaTime*/) -> bool
                {
                    return Self->PollOnce();
                });
        }

        /** 返回 true 表示继续轮询。 */
        bool PollOnce()
        {
            if (bFinished || bResultRequested)
            {
                return false;
            }

            if (++PollAttempts > GMaxPollAttempts)
            {
                FailWith(TEXT("等待 ComfyUI 结果超时。"));
                return false;
            }

            const FString HistoryUrl = FShineComfyClient::BuildApiUrl(BaseUrl, FString::Printf(TEXT("history/%s"), *PromptId));

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineHttpClient::GetJsonObject(HistoryUrl, [Self](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
            {
                if (!Response.bSuccess || !JsonObject.IsValid())
                {
                    // 还没执行完时也可能拿不到，继续等。
                    return;
                }

                FString FileName;
                FString Subfolder;
                FString Type;
                if (!FindFirstImage(JsonObject, FileName, Subfolder, Type))
                {
                    FString StatusError;
                    if (IsHistoryStatusFailed(JsonObject, StatusError))
                    {
                        Self->FailWith(StatusError);
                    }
                    return;
                }

                Self->DownloadResult(FileName, Subfolder, Type);
            });

            return true;
        }

        void DownloadResult(const FString& FileName, const FString& Subfolder, const FString& Type)
        {
            if (bFinished || bResultRequested)
            {
                return;
            }

            bResultRequested = true;

            if (PollHandle.IsValid())
            {
                FTSTicker::RemoveTicker(PollHandle);
                PollHandle.Reset();
            }

            const FString ViewUrl = FShineComfyClient::BuildApiUrl(BaseUrl, TEXT("view"))
                + FString::Printf(
                    TEXT("?filename=%s&subfolder=%s&type=%s"),
                    *FGenericPlatformHttp::UrlEncode(FileName),
                    *FGenericPlatformHttp::UrlEncode(Subfolder),
                    *FGenericPlatformHttp::UrlEncode(Type));

            AppendLog(FString::Printf(TEXT("下载结果图 %s…"), *FileName));

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineHttpClient::GetBinary(ViewUrl, [Self](FShineHttpResponse&& Response, const TArray<uint8>& Data)
            {
                if (!Response.bSuccess || Data.Num() == 0)
                {
                    Self->FailWith(FString::Printf(TEXT("下载结果图失败：%s"), *Response.ErrorMessage));
                    return;
                }

                Self->CompleteWithImage(Data);
            });
        }

        // ---------------- Generic HTTP ----------------

        void StartGenericHttp()
        {
            const FString Url = Settings.GenericEndpoint.IsEmpty() ? Settings.ServiceUrl : Settings.GenericEndpoint;
            if (Url.IsEmpty())
            {
                FailWith(TEXT("请先填写 AI 服务地址。"));
                return;
            }

            TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
            Body->SetStringField(TEXT("prompt"), Settings.Prompt);
            Body->SetStringField(TEXT("negative_prompt"), Settings.NegativePrompt);
            Body->SetStringField(TEXT("image"), FBase64::Encode(BasePng));
            Body->SetStringField(TEXT("mask"), FBase64::Encode(MaskPng));
            Body->SetNumberField(TEXT("width"), Width);
            Body->SetNumberField(TEXT("height"), Height);
            Body->SetNumberField(TEXT("steps"), Settings.Steps);
            Body->SetNumberField(TEXT("cfg"), Settings.CFG);
            Body->SetNumberField(TEXT("denoise"), Settings.Denoise);
            Body->SetNumberField(TEXT("seed"), Settings.Seed);
            Body->SetStringField(TEXT("mask_mode"), Settings.MaskMode == EShineAIPaintMaskMode::Protect ? TEXT("protect") : TEXT("editable"));

            TMap<FString, FString> Headers;
            if (!Settings.AuthorizationHeader.IsEmpty())
            {
                Headers.Add(TEXT("Authorization"), Settings.AuthorizationHeader);
            }

            AppendLog(FString::Printf(TEXT("POST %s"), *Url));

            TSharedRef<FShineAIPaintJob> Self = AsShared();
            FShineHttpClient::PostJson(Url, SerializeJson(Body), [Self](FShineHttpResponse&& Response)
            {
                if (!Response.bSuccess)
                {
                    Self->FailWith(FString::Printf(TEXT("AI 服务返回失败：%s"), *Response.ErrorMessage));
                    return;
                }

                TSharedPtr<FJsonObject> JsonObject;
                const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
                if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
                {
                    Self->FailWith(TEXT("AI 服务返回的不是合法 JSON。"));
                    return;
                }

                FString Base64Image;
                if (!JsonObject->TryGetStringField(TEXT("image"), Base64Image))
                {
                    const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
                    if (JsonObject->TryGetArrayField(TEXT("images"), Images) && Images && Images->Num() > 0)
                    {
                        (*Images)[0]->TryGetString(Base64Image);
                    }
                }

                if (Base64Image.IsEmpty())
                {
                    Self->FailWith(TEXT("AI 服务的响应里没有 image 字段。"));
                    return;
                }

                TArray<uint8> PngBytes;
                if (!FBase64::Decode(Base64Image, PngBytes))
                {
                    Self->FailWith(TEXT("AI 返回的 image 字段不是合法 base64。"));
                    return;
                }

                Self->CompleteWithImage(PngBytes);
            }, Headers);
        }

        /** 往 UI 推一条进度文本（走 WebSocket 的实时进度）。 */
        void ReportProgress(const FString& Text)
        {
            if (ProgressCallback)
            {
                ProgressCallback(Text);
            }
        }

    private:
        FShineAIPaintService::FProgress ProgressCallback;
        FShineAIPaintService::FCompletion Completion;
        FShineAIPaintSettings Settings;

        TArray<FColor> Pixels;
        TArray<uint8> MaskPixels;
        int32 Width = 0;
        int32 Height = 0;

        TArray<uint8> BasePng;
        TArray<uint8> MaskPng;

        FString BaseUrl;
        FString BaseFileName;
        FString MaskFileName;
        FString PromptId;
        FString LogText;

        /** 提交任务用的 clientId，和共用的那条常连 WebSocket 一致。 */
        FString ClientId;

        /** 订阅共用连接上执行事件的句柄。 */
        FDelegateHandle SocketEventHandle;

        /**
         * 提交响应还没回来、还不知道自己 prompt_id 时收到的事件。
         *
         * 这条连接是全项目共用的（ComfyUI 主面板也在这个 clientId 下发任务），
         * 所以在拿到自己的 id 之前不能乱认事件；先存下来，等 id 到手再按 id 过滤一遍。
         */
        TArray<FShineComfySocket::FPromptEvent> DeferredSocketEvents;

        /** 这次任务是不是走 WebSocket 收结果（连不上就退回轮询）。 */
        bool bUsingWebSocket = false;

        /** WebSocket 报回来的进度，给状态栏用，不写进 LogText。 */
        FString ProgressText;

        /** executed 事件里带回的输出图。 */
        FString SocketImageFileName;
        FString SocketImageSubfolder;
        FString SocketImageType;

        /** WebSocket 静默计时，用来决定什么时候退回轮询。 */
        float SecondsWithoutSocketEvent = 0.0f;

        FTSTicker::FDelegateHandle PollHandle;
        FTSTicker::FDelegateHandle WatchdogHandle;
        int32 PollAttempts = 0;
        bool bFinished = false;
        bool bResultRequested = false;

        TArray<FColor> ResultPixels;
        int32 ResultWidth = 0;
        int32 ResultHeight = 0;
    };
}

namespace ShineAIPaintImage
{
    bool EncodePng(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPngBytes)
    {
        return EncodePngInternal(Pixels, Width, Height, OutPngBytes);
    }

    bool DecodePng(const TArray<uint8>& PngBytes, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
    {
        if (PngBytes.Num() == 0)
        {
            return false;
        }

        FImage DecodedImage;
        if (!FImageUtils::DecompressImage(PngBytes.GetData(), PngBytes.Num(), DecodedImage))
        {
            return false;
        }

        if (DecodedImage.SizeX <= 0 || DecodedImage.SizeY <= 0)
        {
            return false;
        }

        // 统一转成 BGRA8，AI 可能返回 RGB / 灰度图。
        FImage ConvertedImage;
        const FImage* SourceImage = &DecodedImage;
        if (DecodedImage.Format != ERawImageFormat::BGRA8 || DecodedImage.GammaSpace != EGammaSpace::sRGB)
        {
            DecodedImage.CopyTo(ConvertedImage, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
            SourceImage = &ConvertedImage;
        }

        const TArrayView64<const FColor> BgraPixels = SourceImage->AsBGRA8();
        if (BgraPixels.Num() <= 0)
        {
            return false;
        }

        OutWidth = SourceImage->SizeX;
        OutHeight = SourceImage->SizeY;
        OutPixels.SetNumUninitialized(static_cast<int32>(BgraPixels.Num()));
        FMemory::Memcpy(OutPixels.GetData(), BgraPixels.GetData(), static_cast<SIZE_T>(BgraPixels.Num()) * sizeof(FColor));
        return true;
    }

    bool EncodeMaskPng(const TArray<uint8>& MaskPixels, int32 Width, int32 Height, bool bWhiteIsEditable, TArray<uint8>& OutPngBytes)
    {
        if (Width <= 0 || Height <= 0 || MaskPixels.Num() != Width * Height)
        {
            return false;
        }

        TArray<FColor> GrayscalePixels;
        GrayscalePixels.SetNumUninitialized(Width * Height);
        for (int32 Index = 0; Index < GrayscalePixels.Num(); ++Index)
        {
            const uint8 MaskValue = MaskPixels[Index];
            const uint8 GrayValue = bWhiteIsEditable ? static_cast<uint8>(255 - MaskValue) : MaskValue;
            GrayscalePixels[Index] = FColor(GrayValue, GrayValue, GrayValue, 255);
        }

        return EncodePngInternal(GrayscalePixels, Width, Height, OutPngBytes);
    }

    bool SavePngToFile(const TArray<uint8>& PngBytes, const FString& FilePath)
    {
        if (PngBytes.Num() == 0 || FilePath.IsEmpty())
        {
            return false;
        }

        return FFileHelper::SaveArrayToFile(PngBytes, *FilePath);
    }
}

void FShineAIPaintService::RequestTexture(
    const TArray<FColor>& BasePixels,
    int32 Width,
    int32 Height,
    const TArray<uint8>& MaskPixels,
    const FShineAIPaintSettings& Settings,
    FProgress&& Progress,
    FCompletion&& Completion)
{
    TSharedRef<FShineAIPaintJob> Job = MakeShared<FShineAIPaintJob>();

    // 立刻用一份拷贝启动，保证异步过程中画布继续被编辑也不会串数据。
    Job->Start(BasePixels, Width, Height, MaskPixels, Settings, MoveTemp(Progress), MoveTemp(Completion));
}

void FShineAIPaintService::FetchCheckpoints(const FString& ServiceUrl, FOnCheckpointsFetched&& Completion)
{
    TSharedRef<FOnCheckpointsFetched> SharedCompletion = MakeShared<FOnCheckpointsFetched>(MoveTemp(Completion));

    const FString BaseUrl = FShineComfyClient::NormalizeBaseUrl(ServiceUrl);
    const FString Url = FShineComfyClient::BuildApiUrl(BaseUrl, TEXT("object_info/CheckpointLoaderSimple"));

    FShineHttpClient::GetJsonObject(Url, [SharedCompletion](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
    {
        TArray<FString> Checkpoints;

        if (!Response.bSuccess || !JsonObject.IsValid())
        {
            (*SharedCompletion)(false, Checkpoints, Response.ErrorMessage.IsEmpty() ? TEXT("无法连接 ComfyUI。") : Response.ErrorMessage);
            return;
        }

        const TSharedPtr<FJsonObject>* NodeObject = nullptr;
        if (JsonObject->TryGetObjectField(TEXT("CheckpointLoaderSimple"), NodeObject) && NodeObject)
        {
            const TSharedPtr<FJsonObject>* InputObject = nullptr;
            if ((*NodeObject)->TryGetObjectField(TEXT("input"), InputObject) && InputObject)
            {
                const TSharedPtr<FJsonObject>* RequiredObject = nullptr;
                if ((*InputObject)->TryGetObjectField(TEXT("required"), RequiredObject) && RequiredObject)
                {
                    const TArray<TSharedPtr<FJsonValue>>* CkptField = nullptr;
                    if ((*RequiredObject)->TryGetArrayField(TEXT("ckpt_name"), CkptField) && CkptField && CkptField->Num() > 0)
                    {
                        const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
                        if ((*CkptField)[0].IsValid() && (*CkptField)[0]->TryGetArray(Options) && Options)
                        {
                            for (const TSharedPtr<FJsonValue>& Option : *Options)
                            {
                                FString Name;
                                if (Option.IsValid() && Option->TryGetString(Name))
                                {
                                    Checkpoints.Add(Name);
                                }
                            }
                        }
                    }
                }
            }
        }

        (*SharedCompletion)(true, Checkpoints, Checkpoints.Num() == 0 ? TEXT("ComfyUI 没有返回任何 checkpoint。") : FString());
    });
}
