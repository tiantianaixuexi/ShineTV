#include "Comfy/ShineComfyClient.h"

#include "Comfy/Builders/Models/ShineComfyModelLibraryBuilder.h"
#include "Comfy/Builders/Nodes/ShineComfyNodeDefinitionBuilder.h"
#include "Comfy/Network/ShineComfyHttpRoutes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "Http/ShineHttpClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    struct FShineComfyModelsCacheEntry
    {
        FDateTime FetchedAt;
        TArray<FShineComfyModelFolder> Folders;
    };

    struct FShineComfyNodesCacheEntry
    {
        FDateTime FetchedAt;
        TArray<FShineComfyNodeDefinition> Nodes;
    };

    TMap<FString, FShineComfyModelsCacheEntry> GModelsCache;
    TMap<FString, FShineComfyNodesCacheEntry> GNodesCache;

    FTimespan ResolveModelsCacheTtl(const FShineComfyFetchOptions& Options)
    {
        return Options.CacheTtl > FTimespan::Zero() ? Options.CacheTtl : FTimespan::FromMinutes(5.0);
    }

    FTimespan ResolveNodesCacheTtl(const FShineComfyFetchOptions& Options)
    {
        return Options.CacheTtl > FTimespan::Zero() ? Options.CacheTtl : FTimespan::FromMinutes(2.0);
    }

    bool TryServeModelsCache(const FString& NormalizedBaseUrl, const FShineComfyFetchOptions& Options, FShineComfyModelsResult& OutResult)
    {
        const FShineComfyModelsCacheEntry* CacheEntry = GModelsCache.Find(NormalizedBaseUrl);
        if (!CacheEntry || Options.bForceRefresh)
        {
            return false;
        }

        if ((FDateTime::UtcNow() - CacheEntry->FetchedAt) > ResolveModelsCacheTtl(Options))
        {
            return false;
        }

        OutResult.bSuccess = true;
        OutResult.bFromCache = true;
        OutResult.FetchedAt = CacheEntry->FetchedAt;
        OutResult.Folders = CacheEntry->Folders;
        return true;
    }

    bool TryServeNodesCache(const FString& NormalizedBaseUrl, const FShineComfyFetchOptions& Options, FShineComfyNodesResult& OutResult)
    {
        const FShineComfyNodesCacheEntry* CacheEntry = GNodesCache.Find(NormalizedBaseUrl);
        if (!CacheEntry || Options.bForceRefresh)
        {
            return false;
        }

        if ((FDateTime::UtcNow() - CacheEntry->FetchedAt) > ResolveNodesCacheTtl(Options))
        {
            return false;
        }

        OutResult.bSuccess = true;
        OutResult.bFromCache = true;
        OutResult.FetchedAt = CacheEntry->FetchedAt;
        OutResult.Nodes = CacheEntry->Nodes;
        return true;
    }

    bool TryParseQueueEntry(const TArray<TSharedPtr<FJsonValue>>& QueueTuple, EShineComfyQueueTaskState State, int32 QueueIndex, FShineComfyQueueEntry& OutEntry)
    {
        if (QueueTuple.Num() < 2 || !QueueTuple[0].IsValid() || !QueueTuple[1].IsValid())
        {
            return false;
        }

        FString PromptId;
        if (!QueueTuple[1]->TryGetString(PromptId))
        {
            return false;
        }

        OutEntry.PromptId = MoveTemp(PromptId);
        OutEntry.State = State;
        OutEntry.QueueIndex = QueueIndex;
        OutEntry.Number = QueueTuple[0]->AsNumber();

        if (QueueTuple.Num() > 3)
        {
            const TSharedPtr<FJsonObject>* ExtraDataObject = nullptr;
            if (QueueTuple[3].IsValid() && QueueTuple[3]->TryGetObject(ExtraDataObject) && ExtraDataObject && ExtraDataObject->IsValid())
            {
                const TSharedPtr<FJsonObject>& ExtraData = *ExtraDataObject;
                if (ExtraData->HasTypedField<EJson::Number>(TEXT("create_time")))
                {
                    OutEntry.CreatedAtMillis = static_cast<int64>(ExtraData->GetNumberField(TEXT("create_time")));
                }
            }
        }

        return true;
    }

    FString SerializeJsonObject(const TSharedPtr<FJsonObject>& JsonObject);

    int64 TryGetNestedNumberField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
    {
        return JsonObject.IsValid() && JsonObject->HasTypedField<EJson::Number>(FieldName)
            ? static_cast<int64>(JsonObject->GetNumberField(FieldName))
            : 0;
    }

    FString BuildHistoryImageLabel(const FShineComfyHistoryImage& Image)
    {
        if (Image.Subfolder.IsEmpty())
        {
            return Image.FileName;
        }

        return FString::Printf(TEXT("%s/%s"), *Image.Subfolder, *Image.FileName);
    }

    bool TryParseHistoryEntry(const FString& PromptId, const TSharedPtr<FJsonObject>& HistoryObject, FShineComfyHistoryEntry& OutEntry)
    {
        if (!HistoryObject.IsValid())
        {
            return false;
        }

        OutEntry.PromptId = PromptId;
        OutEntry.RawJson = SerializeJsonObject(HistoryObject);

        const TArray<TSharedPtr<FJsonValue>>* PromptTuple = nullptr;
        if (HistoryObject->TryGetArrayField(TEXT("prompt"), PromptTuple) && PromptTuple && PromptTuple->Num() > 3)
        {
            const TSharedPtr<FJsonObject>* ExtraDataObject = nullptr;
            if ((*PromptTuple)[3].IsValid() && (*PromptTuple)[3]->TryGetObject(ExtraDataObject) && ExtraDataObject && ExtraDataObject->IsValid())
            {
                OutEntry.CreatedAtMillis = TryGetNestedNumberField(*ExtraDataObject, TEXT("create_time"));
            }
        }

        const TSharedPtr<FJsonObject>* StatusObject = nullptr;
        if (HistoryObject->TryGetObjectField(TEXT("status"), StatusObject) && StatusObject && StatusObject->IsValid())
        {
            const TSharedPtr<FJsonObject>& Status = *StatusObject;
            Status->TryGetStringField(TEXT("status_str"), OutEntry.StatusText);
            OutEntry.CompletedAtMillis = TryGetNestedNumberField(Status, TEXT("completed_at"));
            OutEntry.bFailed = OutEntry.StatusText.Contains(TEXT("error"), ESearchCase::IgnoreCase)
                || OutEntry.StatusText.Contains(TEXT("fail"), ESearchCase::IgnoreCase);
        }

        const TSharedPtr<FJsonObject>* OutputsObject = nullptr;
        if (HistoryObject->TryGetObjectField(TEXT("outputs"), OutputsObject) && OutputsObject && OutputsObject->IsValid())
        {
            TArray<FString> ImageLabels;
            TArray<FString> VideoLabels;

            // ComfyUI 的产出键不止 "images"：
            //   SaveVideo 走 ui.PreviewVideo，序列化结果是
            //       {"images": [...], "animated": [true]}
            //   （comfy_api/latest/_ui.py:432-437）—— 也就是说**视频和图片落在同一个
            //   "images" 键里**，光看键名分不出来，只能靠扩展名判。
            //   老式的 SaveWEBM / VHS_VideoCombine 用 "gifs"，PreviewAudio 用 "audio"。
            //   Kind = Other 表示"这个键里可能混着多种，交给扩展名判定"。
            struct FOutputKeyHint
            {
                const TCHAR* Key;
                EShineComfyMediaKind Kind;
            };
            static const FOutputKeyHint KeyHints[] =
            {
                { TEXT("images"), EShineComfyMediaKind::Other },
                { TEXT("gifs"),   EShineComfyMediaKind::Video },
                { TEXT("videos"), EShineComfyMediaKind::Video },
                { TEXT("video"),  EShineComfyMediaKind::Video },
                { TEXT("audio"),  EShineComfyMediaKind::Audio }
            };

            for (const TPair<FString, TSharedPtr<FJsonValue>>& OutputPair : (*OutputsObject)->Values)
            {
                const TSharedPtr<FJsonObject>* OutputNodeObject = nullptr;
                if (!OutputPair.Value.IsValid() || !OutputPair.Value->TryGetObject(OutputNodeObject) || !OutputNodeObject || !OutputNodeObject->IsValid())
                {
                    continue;
                }

                for (const FOutputKeyHint& Hint : KeyHints)
                {
                    const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
                    if (!(*OutputNodeObject)->TryGetArrayField(Hint.Key, ItemsArray) || !ItemsArray)
                    {
                        continue;
                    }

                    for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
                    {
                        const TSharedPtr<FJsonObject>* ItemObject = nullptr;
                        if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObject) || !ItemObject || !ItemObject->IsValid())
                        {
                            continue;
                        }

                        FShineComfyHistoryMedia& Media = OutEntry.Media.AddDefaulted_GetRef();
                        // 留下产出它的节点 id：一次提交里每个分镜都有自己的 SaveVideo 节点，
                        // 不留这个就只能靠产物顺序猜哪段是哪段（见 FShineComfyHistoryMedia::NodeId）。
                        Media.NodeId = OutputPair.Key;
                        (*ItemObject)->TryGetStringField(TEXT("filename"), Media.FileName);
                        (*ItemObject)->TryGetStringField(TEXT("subfolder"), Media.Subfolder);
                        (*ItemObject)->TryGetStringField(TEXT("type"), Media.Type);

                        if (Media.FileName.IsEmpty())
                        {
                            OutEntry.Media.RemoveAt(OutEntry.Media.Num() - 1);
                            continue;
                        }

                        Media.Kind = Hint.Kind != EShineComfyMediaKind::Other
                            ? Hint.Kind
                            : FShineComfyClient::ClassifyMediaKind(Media.FileName);

                        // Images 只收真正的图片：以前 mp4 也会被塞进来，下游会拿它当纹理
                        // 去加载然后失败。视频统一从 Media 里取。
                        if (Media.IsImage())
                        {
                            FShineComfyHistoryImage& Image = OutEntry.Images.AddDefaulted_GetRef();
                            Image.FileName = Media.FileName;
                            Image.Subfolder = Media.Subfolder;
                            Image.Type = Media.Type;
                            ImageLabels.Add(BuildHistoryImageLabel(Image));
                        }
                        else if (Media.IsVideo())
                        {
                            VideoLabels.Add(Media.BuildRelativePath());
                        }
                    }
                }
            }

            // 摘要必须把视频说出来，否则纯视频任务在面板上会显示成"没有输出"。
            if (ImageLabels.Num() > 0 || VideoLabels.Num() > 0)
            {
                TArray<FString> Parts;
                if (ImageLabels.Num() > 0)
                {
                    Parts.Add(FString::Printf(TEXT("%d 张图：%s"), ImageLabels.Num(), *FString::Join(ImageLabels, TEXT("，"))));
                }
                if (VideoLabels.Num() > 0)
                {
                    Parts.Add(FString::Printf(TEXT("%d 段视频：%s"), VideoLabels.Num(), *FString::Join(VideoLabels, TEXT("，"))));
                }

                OutEntry.SummaryText = FString::Printf(TEXT("输出 %s"), *FString::Join(Parts, TEXT("；")));
            }
        }

        if (OutEntry.StatusText.IsEmpty())
        {
            OutEntry.StatusText = OutEntry.Media.Num() > 0 ? TEXT("已完成") : TEXT("历史结果");
        }

        if (OutEntry.SummaryText.IsEmpty())
        {
            OutEntry.SummaryText = OutEntry.Media.Num() == 0
                ? TEXT("该历史任务没有媒体输出，面板保留了原始记录。")
                : FString::Printf(TEXT("输出 %d 个媒体文件。"), OutEntry.Media.Num());
        }

        return true;
    }

    FString SerializeJsonObject(const TSharedPtr<FJsonObject>& JsonObject)
    {
        FString JsonText;
        if (!JsonObject.IsValid())
        {
            return JsonText;
        }

        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        return JsonText;
    }

    FString SerializeJsonValue(const TSharedPtr<FJsonValue>& JsonValue)
    {
        FString JsonText;
        if (!JsonValue.IsValid())
        {
            return JsonText;
        }

        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
        FJsonSerializer::Serialize(JsonValue.ToSharedRef(), FString(), Writer);
        return JsonText;
    }
}

FString FShineComfyClient::NormalizeBaseUrl(const FString& BaseUrl)
{
    return ShineComfyHttpRoutes::NormalizeBaseUrl(BaseUrl);
}

FString FShineComfyClient::BuildApiUrl(const FString& BaseUrl, const FString& RelativePath)
{
    return ShineComfyHttpRoutes::BuildApiUrl(BaseUrl, RelativePath);
}

FString FShineComfyClient::BuildWebSocketUrl(const FString& BaseUrl, const FString& ClientId)
{
    FString WebSocketUrl = NormalizeBaseUrl(BaseUrl);
    if (WebSocketUrl.StartsWith(TEXT("https://")))
    {
        WebSocketUrl = TEXT("wss://") + WebSocketUrl.RightChop(8);
    }
    else if (WebSocketUrl.StartsWith(TEXT("http://")))
    {
        WebSocketUrl = TEXT("ws://") + WebSocketUrl.RightChop(7);
    }

    return FString::Printf(TEXT("%s/ws?clientId=%s"), *WebSocketUrl, *FGenericPlatformHttp::UrlEncode(ClientId));
}

void FShineComfyClient::FetchModels(const FString& BaseUrl, FOnModelsFetched&& Callback)
{
    FetchModels(BaseUrl, FShineComfyFetchOptions(), MoveTemp(Callback));
}

void FShineComfyClient::FetchModels(const FString& BaseUrl, const FShineComfyFetchOptions& Options, FOnModelsFetched&& Callback)
{
    struct FPendingModelsRequest : public TSharedFromThis<FPendingModelsRequest>
    {
        FOnModelsFetched Completion;
        FString NormalizedBaseUrl;
        FShineComfyFetchOptions Options;
        TArray<FShineComfyModelFolder> Folders;
        FString ErrorMessage;
        int32 RemainingFolders = 0;
        bool bCompleted = false;

        void Finish(bool bSuccess)
        {
            if (bCompleted)
            {
                return;
            }

            bCompleted = true;

            FShineComfyModelsResult Result;
            Result.bSuccess = bSuccess;
            Result.bFromCache = false;
            Result.FetchedAt = FDateTime::UtcNow();
            Result.ErrorMessage = ErrorMessage;
            Result.Folders = MoveTemp(Folders);

            if (bSuccess)
            {
                FShineComfyModelsCacheEntry& CacheEntry = GModelsCache.FindOrAdd(NormalizedBaseUrl);
                CacheEntry.FetchedAt = Result.FetchedAt;
                CacheEntry.Folders = Result.Folders;
            }
            else if (Options.bAllowStaleCacheOnError)
            {
                if (const FShineComfyModelsCacheEntry* CacheEntry = GModelsCache.Find(NormalizedBaseUrl))
                {
                    Result.bSuccess = true;
                    Result.bFromCache = true;
                    Result.FetchedAt = CacheEntry->FetchedAt;
                    Result.Folders = CacheEntry->Folders;
                    if (Result.ErrorMessage.IsEmpty())
                    {
                        Result.ErrorMessage = TEXT("模型接口请求失败，已回退到缓存结果。");
                    }
                }
            }

            Completion(MoveTemp(Result));
        }
    };

    TSharedRef<FPendingModelsRequest> State = MakeShared<FPendingModelsRequest>();
    State->Completion = MoveTemp(Callback);
    State->NormalizedBaseUrl = NormalizeBaseUrl(BaseUrl);
    State->Options = Options;

    FShineComfyModelsResult CachedResult;
    if (TryServeModelsCache(State->NormalizedBaseUrl, Options, CachedResult))
    {
        State->Completion(MoveTemp(CachedResult));
        return;
    }

    FShineHttpClient::GetJsonArray(BuildApiUrl(State->NormalizedBaseUrl, TEXT("models")), [State](FShineHttpResponse&& Response, TArray<TSharedPtr<FJsonValue>>&& JsonArray)
    {
        if (!Response.bSuccess)
        {
            State->ErrorMessage = Response.ErrorMessage.IsEmpty() ? TEXT("读取模型目录失败。") : Response.ErrorMessage;
            State->Finish(false);
            return;
        }

        TArray<FString> FolderNames;
        for (const TSharedPtr<FJsonValue>& EntryValue : JsonArray)
        {
            FString FolderName;
            if (EntryValue.IsValid() && EntryValue->TryGetString(FolderName))
            {
                FolderNames.Add(MoveTemp(FolderName));
            }
        }

        State->RemainingFolders = FolderNames.Num();
        if (State->RemainingFolders == 0)
        {
            State->Finish(true);
            return;
        }

        for (const FString& FolderName : FolderNames)
        {
            const FString EncodedFolderName = FGenericPlatformHttp::UrlEncode(FolderName);
            FShineHttpClient::GetJsonArray(BuildApiUrl(State->NormalizedBaseUrl, FString::Printf(TEXT("models/%s"), *EncodedFolderName)), [State, FolderName](FShineHttpResponse&& FolderResponse, TArray<TSharedPtr<FJsonValue>>&& FolderJsonArray)
            {
                FShineComfyModelFolder Folder;

                if (FolderResponse.bSuccess)
                {
                    FShineComfyModelLibraryBuilder::FillModelFolderFromJsonArray(FolderName, FolderJsonArray, Folder);
                    State->Folders.Add(MoveTemp(Folder));
                }
                else if (State->ErrorMessage.IsEmpty())
                {
                    State->ErrorMessage = FolderResponse.ErrorMessage.IsEmpty()
                        ? FString::Printf(TEXT("读取模型目录 %s 失败。"), *FolderName)
                        : FolderResponse.ErrorMessage;
                }

                --State->RemainingFolders;
                if (State->RemainingFolders == 0)
                {
                    State->Folders.Sort([](const FShineComfyModelFolder& Left, const FShineComfyModelFolder& Right)
                    {
                        return Left.FolderName < Right.FolderName;
                    });
                    State->Finish(State->ErrorMessage.IsEmpty());
                }
            });
        }
    });
}

void FShineComfyClient::FetchNodes(const FString& BaseUrl, FOnNodesFetched&& Callback)
{
    FetchNodes(BaseUrl, FShineComfyFetchOptions(), MoveTemp(Callback));
}

void FShineComfyClient::FetchNodes(const FString& BaseUrl, const FShineComfyFetchOptions& Options, FOnNodesFetched&& Callback)
{
    TSharedRef<FOnNodesFetched> SharedCallback = MakeShared<FOnNodesFetched>(MoveTemp(Callback));
    const FString NormalizedBaseUrl = NormalizeBaseUrl(BaseUrl);

    FShineComfyNodesResult CachedResult;
    if (TryServeNodesCache(NormalizedBaseUrl, Options, CachedResult))
    {
        (*SharedCallback)(MoveTemp(CachedResult));
        return;
    }

    FShineHttpClient::GetJsonObject(BuildApiUrl(NormalizedBaseUrl, TEXT("object_info")), [SharedCallback, NormalizedBaseUrl, Options](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
    {
        FShineComfyNodesResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.bFromCache = false;
        Result.FetchedAt = FDateTime::UtcNow();
        Result.ErrorMessage = Response.ErrorMessage;

        if (!Response.bSuccess)
        {
            if (Options.bAllowStaleCacheOnError)
            {
                if (const FShineComfyNodesCacheEntry* CacheEntry = GNodesCache.Find(NormalizedBaseUrl))
                {
                    Result.bSuccess = true;
                    Result.bFromCache = true;
                    Result.FetchedAt = CacheEntry->FetchedAt;
                    Result.Nodes = CacheEntry->Nodes;
                    if (Result.ErrorMessage.IsEmpty())
                    {
                        Result.ErrorMessage = TEXT("节点接口请求失败，已回退到缓存结果。");
                    }
                }
            }
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        if (!ParseNodeDefinitions(JsonObject, Result.Nodes, Result.ErrorMessage))
        {
            Result.bSuccess = false;
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        FShineComfyNodesCacheEntry& CacheEntry = GNodesCache.FindOrAdd(NormalizedBaseUrl);
        CacheEntry.FetchedAt = Result.FetchedAt;
        CacheEntry.Nodes = Result.Nodes;
        Result.bSuccess = true;
        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::FetchQueue(const FString& BaseUrl, FOnQueueFetched&& Callback)
{
    TSharedRef<FOnQueueFetched> SharedCallback = MakeShared<FOnQueueFetched>(MoveTemp(Callback));
    FShineHttpClient::GetJsonObject(BuildApiUrl(NormalizeBaseUrl(BaseUrl), TEXT("queue")), [SharedCallback](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
    {
        FShineComfyQueueResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        if (!Response.bSuccess)
        {
            if (Result.ErrorMessage.IsEmpty())
            {
                Result.ErrorMessage = TEXT("读取 ComfyUI 队列失败。");
            }
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        if (!JsonObject.IsValid())
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("队列响应为空。");
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        auto ParseQueueArray = [&Result, &JsonObject](const FString& FieldName, EShineComfyQueueTaskState State, TArray<FShineComfyQueueEntry>& OutEntries)
        {
            const TArray<TSharedPtr<FJsonValue>>* QueueValues = nullptr;
            if (!Result.bSuccess || !JsonObject.IsValid() || !JsonObject->TryGetArrayField(FieldName, QueueValues) || !QueueValues)
            {
                return;
            }

            for (int32 QueueIndex = 0; QueueIndex < QueueValues->Num(); ++QueueIndex)
            {
                const TSharedPtr<FJsonValue>& QueueValue = (*QueueValues)[QueueIndex];
                const TArray<TSharedPtr<FJsonValue>>* QueueTuple = nullptr;
                if (!QueueValue.IsValid() || !QueueValue->TryGetArray(QueueTuple) || !QueueTuple)
                {
                    continue;
                }

                FShineComfyQueueEntry Entry;
                if (TryParseQueueEntry(*QueueTuple, State, QueueIndex, Entry))
                {
                    OutEntries.Add(MoveTemp(Entry));
                }
            }
        };

        ParseQueueArray(TEXT("queue_running"), EShineComfyQueueTaskState::Running, Result.Running);
        ParseQueueArray(TEXT("queue_pending"), EShineComfyQueueTaskState::Pending, Result.Pending);
        Result.QueueRemaining = Result.Running.Num() + Result.Pending.Num();
        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::FetchHistory(const FString& BaseUrl, int32 MaxItems, FOnHistoryFetched&& Callback)
{
    TSharedRef<FOnHistoryFetched> SharedCallback = MakeShared<FOnHistoryFetched>(MoveTemp(Callback));
    const FString HistoryPath = MaxItems > 0
        ? FString::Printf(TEXT("history?max_items=%d"), MaxItems)
        : TEXT("history");

    FShineHttpClient::GetJsonObject(BuildApiUrl(NormalizeBaseUrl(BaseUrl), HistoryPath), [SharedCallback](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
    {
        FShineComfyHistoryResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        if (!Response.bSuccess)
        {
            if (Result.ErrorMessage.IsEmpty())
            {
                Result.ErrorMessage = TEXT("读取 ComfyUI 历史结果失败。");
            }
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        if (!JsonObject.IsValid())
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("历史结果响应为空。");
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& HistoryPair : JsonObject->Values)
        {
            const TSharedPtr<FJsonObject>* HistoryObject = nullptr;
            if (!HistoryPair.Value.IsValid() || !HistoryPair.Value->TryGetObject(HistoryObject) || !HistoryObject || !HistoryObject->IsValid())
            {
                continue;
            }

            FShineComfyHistoryEntry Entry;
            if (TryParseHistoryEntry(HistoryPair.Key, *HistoryObject, Entry))
            {
                Result.Entries.Add(MoveTemp(Entry));
            }
        }

        Result.Entries.Sort([](const FShineComfyHistoryEntry& Left, const FShineComfyHistoryEntry& Right)
        {
            const int64 LeftTime = Left.CompletedAtMillis > 0 ? Left.CompletedAtMillis : Left.CreatedAtMillis;
            const int64 RightTime = Right.CompletedAtMillis > 0 ? Right.CompletedAtMillis : Right.CreatedAtMillis;
            return LeftTime > RightTime;
        });

        (*SharedCallback)(MoveTemp(Result));
    });
}

EShineComfyMediaKind FShineComfyClient::ClassifyMediaKind(const FString& FileName)
{
    const FString Extension = FPaths::GetExtension(FileName).ToLower();

    // gif 归到视频：ComfyUI 的 "gifs" 键历史上装的其实是 webm/mp4，而且动图在
    // 视频工作台里也该用播放器看，不该当静态纹理加载。
    if (Extension == TEXT("mp4") || Extension == TEXT("webm") || Extension == TEXT("mkv")
        || Extension == TEXT("mov") || Extension == TEXT("avi") || Extension == TEXT("gif"))
    {
        return EShineComfyMediaKind::Video;
    }

    if (Extension == TEXT("png") || Extension == TEXT("jpg") || Extension == TEXT("jpeg")
        || Extension == TEXT("webp") || Extension == TEXT("bmp") || Extension == TEXT("tga"))
    {
        return EShineComfyMediaKind::Image;
    }

    if (Extension == TEXT("flac") || Extension == TEXT("wav") || Extension == TEXT("mp3") || Extension == TEXT("ogg"))
    {
        return EShineComfyMediaKind::Audio;
    }

    return EShineComfyMediaKind::Other;
}

void FShineComfyClient::FetchHistoryForPrompt(const FString& BaseUrl, const FString& PromptId, FOnHistoryFetched&& Callback)
{
    TSharedRef<FOnHistoryFetched> SharedCallback = MakeShared<FOnHistoryFetched>(MoveTemp(Callback));

    FShineComfyHistoryResult EmptyResult;
    if (PromptId.IsEmpty())
    {
        EmptyResult.ErrorMessage = TEXT("prompt_id 为空。");
        (*SharedCallback)(MoveTemp(EmptyResult));
        return;
    }

    const FString HistoryPath = FString::Printf(TEXT("history/%s"), *PromptId);
    FShineHttpClient::GetJsonObject(BuildApiUrl(NormalizeBaseUrl(BaseUrl), HistoryPath), [SharedCallback](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
    {
        FShineComfyHistoryResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        if (!Result.bSuccess || !JsonObject.IsValid())
        {
            Result.bSuccess = false;
            if (Result.ErrorMessage.IsEmpty())
            {
                Result.ErrorMessage = TEXT("读取该任务的 history 失败。");
            }
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonObject->Values)
        {
            const TSharedPtr<FJsonObject>* HistoryObject = nullptr;
            if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(HistoryObject) || !HistoryObject || !(*HistoryObject).IsValid())
            {
                continue;
            }

            FShineComfyHistoryEntry Entry;
            if (TryParseHistoryEntry(Pair.Key, *HistoryObject, Entry))
            {
                Result.Entries.Add(MoveTemp(Entry));
            }
        }

        // Entries 为空【不是失败】：ComfyUI 只在任务真正结束时才写 history，
        // 返回空只说明"还没跑完"。调用方据此判断仍在执行中。
        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::DownloadMedia(const FString& BaseUrl, const FShineComfyHistoryMedia& Media, const FString& DestinationPath, FOnMediaDownloaded&& Callback)
{
    TSharedRef<FOnMediaDownloaded> SharedCallback = MakeShared<FOnMediaDownloaded>(MoveTemp(Callback));

    FShineComfyMediaDownloadResult EarlyOut;
    EarlyOut.LocalPath = DestinationPath;

    if (Media.FileName.IsEmpty() || DestinationPath.IsEmpty())
    {
        EarlyOut.ErrorMessage = TEXT("媒体文件名或目标路径为空。");
        (*SharedCallback)(MoveTemp(EarlyOut));
        return;
    }

    // 注意 /view 不带 /api 前缀：它是文件服务端点，不是 API 端点。
    const FString Url = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"),
        *NormalizeBaseUrl(BaseUrl),
        *FGenericPlatformHttp::UrlEncode(Media.FileName),
        *FGenericPlatformHttp::UrlEncode(Media.Subfolder),
        *FGenericPlatformHttp::UrlEncode(Media.Type.IsEmpty() ? TEXT("output") : Media.Type));

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    // 视频动辄几十上百 MB，默认超时不够。
    Request->SetTimeout(600.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [SharedCallback, DestinationPath](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            FShineComfyMediaDownloadResult Result;
            Result.LocalPath = DestinationPath;

            if (!bConnectedOk || !Response.IsValid())
            {
                Result.ErrorMessage = TEXT("连接 ComfyUI /view 失败。");
                (*SharedCallback)(MoveTemp(Result));
                return;
            }

            const int32 ResponseCode = Response->GetResponseCode();
            if (ResponseCode != 200)
            {
                Result.ErrorMessage = FString::Printf(TEXT("/view 返回 HTTP %d。"), ResponseCode);
                (*SharedCallback)(MoveTemp(Result));
                return;
            }

            const TArray<uint8>& Content = Response->GetContent();
            if (Content.Num() == 0)
            {
                Result.ErrorMessage = TEXT("/view 返回了空内容。");
                (*SharedCallback)(MoveTemp(Result));
                return;
            }

            IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestinationPath), true);
            if (!FFileHelper::SaveArrayToFile(Content, *DestinationPath))
            {
                Result.ErrorMessage = FString::Printf(TEXT("写入失败：%s"), *DestinationPath);
                (*SharedCallback)(MoveTemp(Result));
                return;
            }

            Result.bSuccess = true;
            Result.BytesWritten = Content.Num();
            (*SharedCallback)(MoveTemp(Result));
        });

    Request->ProcessRequest();
}

void FShineComfyClient::FreeVram(const FString& BaseUrl, FOnFreeVramCompleted&& Callback)
{
    TSharedRef<FOnFreeVramCompleted> SharedCallback = MakeShared<FOnFreeVramCompleted>(MoveTemp(Callback));

    // /api/free 是 POST、不需要请求体。这里给个空对象，避免有些版本对空 body 挑剔。
    FShineHttpClient::PostJson(BuildApiUrl(NormalizeBaseUrl(BaseUrl), TEXT("free")), TEXT("{}"), [SharedCallback](FShineHttpResponse&& Response)
    {
        FShineComfyFreeVramResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
        {
            Result.ErrorMessage = TEXT("调用 /api/free 失败。");
        }

        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::FetchSystemStats(const FString& BaseUrl, FOnSystemStatsFetched&& Callback)
{
    TSharedRef<FOnSystemStatsFetched> SharedCallback = MakeShared<FOnSystemStatsFetched>(MoveTemp(Callback));

    FShineHttpClient::GetJsonObject(BuildApiUrl(NormalizeBaseUrl(BaseUrl), TEXT("system_stats")), [SharedCallback](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
    {
        FShineComfySystemStatsResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        if (!Result.bSuccess || !JsonObject.IsValid())
        {
            Result.bSuccess = false;
            if (Result.ErrorMessage.IsEmpty())
            {
                Result.ErrorMessage = TEXT("读取 ComfyUI /api/system_stats 失败。");
            }

            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        // 多卡时只看第 0 块：H3 这套配置只跑一块卡，挑设备是上层的事。
        const TArray<TSharedPtr<FJsonValue>>* Devices = nullptr;
        if (!JsonObject->TryGetArrayField(TEXT("devices"), Devices) || !Devices || Devices->Num() == 0)
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("/api/system_stats 里没有设备信息。");
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        const TSharedPtr<FJsonObject>* DeviceObject = nullptr;
        if (!(*Devices)[0].IsValid() || !(*Devices)[0]->TryGetObject(DeviceObject) || !DeviceObject || !(*DeviceObject).IsValid())
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("/api/system_stats 的设备信息解析失败。");
            (*SharedCallback)(MoveTemp(Result));
            return;
        }

        (*DeviceObject)->TryGetStringField(TEXT("name"), Result.DeviceName);

        // vram_free 而不是 torch_vram_free：Dynamic VRAM(AIMDO) 的占用不在 torch 账上，
        // 只有驱动口径的空闲显存才反映"还能不能塞进一次 H3 采样"。
        double NumberValue = 0.0;
        if ((*DeviceObject)->TryGetNumberField(TEXT("vram_total"), NumberValue))
        {
            Result.VramTotalBytes = NumberValue;
        }
        if ((*DeviceObject)->TryGetNumberField(TEXT("vram_free"), NumberValue))
        {
            Result.VramFreeBytes = NumberValue;
        }

        if (!Result.HasDevice())
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("/api/system_stats 没有报出 vram_total，无法判断显存。");
        }

        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::SubmitPrompt(const FString& BaseUrl, const FShineComfyPromptSubmitRequest& Request, FOnPromptSubmitted&& Callback)
{
    TSharedRef<FOnPromptSubmitted> SharedCallback = MakeShared<FOnPromptSubmitted>(MoveTemp(Callback));

    TSharedPtr<FJsonObject> RequestObject = MakeShared<FJsonObject>();
    RequestObject->SetObjectField(TEXT("prompt"), Request.Prompt);
    if (!Request.ClientId.IsEmpty())
    {
        RequestObject->SetStringField(TEXT("client_id"), Request.ClientId);
    }
    if (!Request.PromptId.IsEmpty())
    {
        RequestObject->SetStringField(TEXT("prompt_id"), Request.PromptId);
    }
    if (Request.bFront)
    {
        RequestObject->SetBoolField(TEXT("front"), true);
    }
    if (Request.Number.IsSet())
    {
        RequestObject->SetNumberField(TEXT("number"), Request.Number.GetValue());
    }

    FShineHttpRequest SubmitRequest;
    SubmitRequest.Url = BuildApiUrl(NormalizeBaseUrl(BaseUrl), TEXT("prompt"));
    SubmitRequest.Verb = TEXT("POST");
    SubmitRequest.Content = SerializeJsonObject(RequestObject);
    SubmitRequest.ContentType = TEXT("application/json");

    // 提交这一步【不能】用 HttpModule 默认的 30 秒超时。
    //
    // 实测踩过一次：ComfyUI 起来后拉 ComfyRegistry（ComfyUI-Manager）会把事件循环占住几分钟，
    // 这期间 `/api/free` 与 `/upload/image` 都秒回，`/prompt` 却卡满 30 秒报"目标服务不可达"——
    // 而服务端日志里当时**没有** got prompt（它是二十多分钟后事件循环空下来才收下的），
    // 队列里因此躺着一份谁也不知道的 prompt。所以这里只能等：
    // 请求可能已经进去了，靠重试兜底只会排出两份图。
    SubmitRequest.TimeoutSeconds = 300.0;

    FShineHttpClient::Send(SubmitRequest, [SharedCallback](FShineHttpResponse&& Response)
    {
        FShineComfyPromptSubmitResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        TSharedPtr<FJsonObject> JsonObject;
        if (!Response.Content.IsEmpty())
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
            FJsonSerializer::Deserialize(Reader, JsonObject);
        }

        if (JsonObject.IsValid())
        {
            JsonObject->TryGetStringField(TEXT("prompt_id"), Result.PromptId);
            if (JsonObject->HasTypedField<EJson::Number>(TEXT("number")))
            {
                Result.Number = JsonObject->GetNumberField(TEXT("number"));
            }

            // node_errors 是"每个节点的校验错误"。空对象是正常情况，但序列化出来会带换行，
            // 直接比较字符串会把 {} 误判成"有错"，所以这里按字段数判断。
            const TSharedPtr<FJsonObject>* NodeErrorsObject = nullptr;
            if (JsonObject->TryGetObjectField(TEXT("node_errors"), NodeErrorsObject)
                && NodeErrorsObject && (*NodeErrorsObject).IsValid() && (*NodeErrorsObject)->Values.Num() > 0)
            {
                Result.RawNodeErrorsJson = SerializeJsonValue(JsonObject->TryGetField(TEXT("node_errors")));
            }

            if (!Result.bSuccess)
            {
                if (JsonObject->HasTypedField<EJson::Object>(TEXT("error")))
                {
                    Result.ErrorMessage = SerializeJsonValue(JsonObject->TryGetField(TEXT("error")));
                }
                else if (JsonObject->HasTypedField<EJson::String>(TEXT("error")))
                {
                    JsonObject->TryGetStringField(TEXT("error"), Result.ErrorMessage);
                }
            }
        }

        if (Result.bSuccess && Result.PromptId.IsEmpty())
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("ComfyUI 未返回 prompt_id。");
        }

        if (Result.ErrorMessage.IsEmpty() && !Result.bSuccess)
        {
            Result.ErrorMessage = TEXT("提交 ComfyUI 任务失败。");
        }

        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::UploadImage(const FString& BaseUrl, const FString& FileName, const TArray<uint8>& PngBytes, bool bIsMask, FOnUploadCompleted&& Callback)
{
    TSharedRef<FOnUploadCompleted> SharedCallback = MakeShared<FOnUploadCompleted>(MoveTemp(Callback));

    const FString Url = BuildApiUrl(NormalizeBaseUrl(BaseUrl), bIsMask ? TEXT("upload/mask") : TEXT("upload/image"));

    TArray<FShineHttpMultipartField> Fields;
    Fields.Add(FShineHttpMultipartField::File(TEXT("image"), FileName, PngBytes, TEXT("image/png")));
    Fields.Add(FShineHttpMultipartField::Text(TEXT("overwrite"), TEXT("true")));
    Fields.Add(FShineHttpMultipartField::Text(TEXT("type"), TEXT("input")));

    FShineHttpClient::PostMultipart(Url, Fields, [SharedCallback](FShineHttpResponse&& Response)
    {
        FShineComfyUploadResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;

        TSharedPtr<FJsonObject> JsonObject;
        if (!Response.Content.IsEmpty())
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
            FJsonSerializer::Deserialize(Reader, JsonObject);
        }

        if (JsonObject.IsValid())
        {
            JsonObject->TryGetStringField(TEXT("name"), Result.Name);
            JsonObject->TryGetStringField(TEXT("subfolder"), Result.Subfolder);
            JsonObject->TryGetStringField(TEXT("type"), Result.Type);

            if (JsonObject->HasTypedField<EJson::String>(TEXT("error")))
            {
                JsonObject->TryGetStringField(TEXT("error"), Result.ErrorMessage);
                Result.bSuccess = false;
            }
        }

        if (Result.bSuccess && Result.Name.IsEmpty())
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("ComfyUI 上传成功但没有返回文件名。");
        }

        if (Result.ErrorMessage.IsEmpty() && !Result.bSuccess)
        {
            Result.ErrorMessage = TEXT("上传图片到 ComfyUI 失败。");
        }

        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::DeleteQueueItems(const FString& BaseUrl, const TArray<FString>& PromptIds, FOnQueueOperationCompleted&& Callback)
{
    TSharedRef<FOnQueueOperationCompleted> SharedCallback = MakeShared<FOnQueueOperationCompleted>(MoveTemp(Callback));

    TSharedPtr<FJsonObject> RequestObject = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> DeleteValues;
    DeleteValues.Reserve(PromptIds.Num());
    for (const FString& PromptId : PromptIds)
    {
        DeleteValues.Add(MakeShared<FJsonValueString>(PromptId));
    }
    RequestObject->SetArrayField(TEXT("delete"), DeleteValues);

    FShineHttpClient::PostJson(BuildApiUrl(NormalizeBaseUrl(BaseUrl), TEXT("queue")), SerializeJsonObject(RequestObject), [SharedCallback](FShineHttpResponse&& Response)
    {
        FShineComfyQueueOperationResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;
        if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
        {
            Result.ErrorMessage = TEXT("删除 ComfyUI 队列任务失败。");
        }
        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::InterruptPrompt(const FString& BaseUrl, const FString& PromptId, FOnQueueOperationCompleted&& Callback)
{
    TSharedRef<FOnQueueOperationCompleted> SharedCallback = MakeShared<FOnQueueOperationCompleted>(MoveTemp(Callback));

    TSharedPtr<FJsonObject> RequestObject = MakeShared<FJsonObject>();
    if (!PromptId.IsEmpty())
    {
        RequestObject->SetStringField(TEXT("prompt_id"), PromptId);
    }

    FShineHttpClient::PostJson(BuildApiUrl(NormalizeBaseUrl(BaseUrl), TEXT("interrupt")), SerializeJsonObject(RequestObject), [SharedCallback](FShineHttpResponse&& Response)
    {
        FShineComfyQueueOperationResult Result;
        Result.bSuccess = Response.bSuccess;
        Result.ErrorMessage = Response.ErrorMessage;
        if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
        {
            Result.ErrorMessage = TEXT("停止 ComfyUI 运行中任务失败。");
        }
        (*SharedCallback)(MoveTemp(Result));
    });
}

void FShineComfyClient::ApplyModelLibraryToNodeDefinitions(TArray<FShineComfyNodeDefinition>& NodeDefinitions, const TArray<FShineComfyModelFolder>& ModelFolders)
{
    FShineComfyModelLibraryBuilder::ApplyModelLibraryToNodeDefinitions(NodeDefinitions, ModelFolders);
}

bool FShineComfyClient::ParseNodeDefinitions(const TSharedPtr<FJsonObject>& RootObject, TArray<FShineComfyNodeDefinition>& OutNodes, FString& OutErrorMessage)
{
    return FShineComfyNodeDefinitionBuilder::ParseNodeDefinitions(RootObject, OutNodes, OutErrorMessage);
}