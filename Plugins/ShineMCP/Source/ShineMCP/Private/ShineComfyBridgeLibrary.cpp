#include "ShineComfyBridgeLibrary.h"

#include "Comfy/ShineComfyPaths.h"

#include "Asset/ShineComfyAsset.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Graph/ShineComfyGraph.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShineMCPHttpClient.h"
#include "ShineMCPPrivate.h"
#include "ShineMCPSettings.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SavePackage.h"

namespace
{
    /** 把用户填的 BaseUrl 归一化成 "http://host:port"（去掉尾部斜杠与 /api 后缀）。 */
    FString NormalizeComfyBase(const FString& InBaseUrl)
    {
        FString Base = InBaseUrl;
        if (Base.IsEmpty())
        {
            const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
            Base = Settings ? Settings->ComfyBaseUrl : TEXT("http://127.0.0.1:8188");
        }

        while (Base.EndsWith(TEXT("/")))
        {
            Base.LeftChopInline(1);
        }
        if (Base.EndsWith(TEXT("/api"), ESearchCase::IgnoreCase))
        {
            Base.LeftChopInline(4);
        }
        return Base;
    }

    /**
     * 调 ComfyUI 接口。ComfyUI 同时挂载 "/xxx" 和 "/api/xxx"，这里先试 /api 前缀，
     * 404 再退回无前缀，避免不同版本差异。
     */
    FShineMCPHttpResult RequestComfy(
        const FString& Verb,
        const FString& InBaseUrl,
        const FString& RelativePath,
        const TArray<uint8>& Body = TArray<uint8>(),
        const FString& ContentType = FString(),
        int32 TimeoutSeconds = 60)
    {
        const FString Base = NormalizeComfyBase(InBaseUrl);

        FShineMCPHttpResult Result = FShineMCPHttpClient::Request(
            Verb, Base + TEXT("/api/") + RelativePath, TArray<FString>(), Body, ContentType, TimeoutSeconds);

        if (Result.StatusCode == 404)
        {
            Result = FShineMCPHttpClient::Request(
                Verb, Base + TEXT("/") + RelativePath, TArray<FString>(), Body, ContentType, TimeoutSeconds);
        }

        if (!Result.bSuccess && Result.Error.IsEmpty())
        {
            Result.Error = FString::Printf(TEXT("HTTP %d"), Result.StatusCode);
        }
        return Result;
    }

    FShineMCPHttpResult PostJsonComfy(const FString& InBaseUrl, const FString& RelativePath, const FString& JsonBody, int32 TimeoutSeconds = 60)
    {
        TArray<uint8> Body;
        FTCHARToUTF8 Utf8(*JsonBody);
        Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

        return RequestComfy(TEXT("POST"), InBaseUrl, RelativePath, Body, TEXT("application/json"), TimeoutSeconds);
    }

    /** 把包路径补全成对象路径：/Game/A/B -> /Game/A/B.B */
    FString ToObjectPath(const FString& InPath)
    {
        FString Path = InPath;
        Path.TrimStartAndEndInline();
        if (Path.Contains(TEXT(".")))
        {
            return Path;
        }

        return FString::Printf(TEXT("%s.%s"), *Path, *FPaths::GetBaseFilename(Path));
    }

    UShineComfyAsset* LoadComfyAsset(const FString& AssetPath, FString& OutError)
    {
        const FString ObjectPath = ToObjectPath(AssetPath);
        UShineComfyAsset* Asset = LoadObject<UShineComfyAsset>(nullptr, *ObjectPath);
        if (!Asset)
        {
            OutError = FString::Printf(TEXT("加载 Shine Comfy 图失败: %s"), *ObjectPath);
        }
        return Asset;
    }

    FString ErrorText(const FString& Message, const FString& Detail = FString())
    {
        TSharedPtr<FJsonObject> Error = ShineMCPJson::Error(Message);
        if (!Detail.IsEmpty())
        {
            Error->SetStringField(TEXT("detail"), Detail);
        }
        return ShineMCPJson::ToText(Error);
    }

    FString HttpErrorText(const FString& What, const FShineMCPHttpResult& Result)
    {
        return ErrorText(FString::Printf(TEXT("%s 失败: %s"), *What, *Result.Error),
            Result.Body.Left(600));
    }
}

// ---------------------------------------------------------------------- 连接

FString UShineComfyBridgeLibrary::PingComfy(const FString& BaseUrl)
{
    const FString Base = NormalizeComfyBase(BaseUrl);
    const FShineMCPHttpResult Result = RequestComfy(TEXT("GET"), Base, TEXT("system_stats"), TArray<uint8>(), FString(), 15);

    if (!Result.bSuccess)
    {
        return HttpErrorText(FString::Printf(TEXT("连接 ComfyUI (%s)"), *Base), Result);
    }

    TSharedPtr<FJsonObject> Root;
    if (!ShineMCPJson::Parse(Result.Body, Root) || !Root.IsValid())
    {
        return ErrorText(TEXT("system_stats 返回不是合法 JSON。"), Result.Body.Left(400));
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("baseUrl"), Base);

    const TSharedPtr<FJsonObject>* SystemObject = nullptr;
    if (Root->TryGetObjectField(TEXT("system"), SystemObject) && SystemObject && SystemObject->IsValid())
    {
        Out->SetStringField(TEXT("comfyuiVersion"), ShineMCPJson::GetString(*SystemObject, TEXT("comfyui_version")));
        Out->SetStringField(TEXT("pythonVersion"), ShineMCPJson::GetString(*SystemObject, TEXT("python_version")));
        Out->SetStringField(TEXT("pytorchVersion"), ShineMCPJson::GetString(*SystemObject, TEXT("pytorch_version")));
        Out->SetNumberField(TEXT("ramFreeGB"), ShineMCPJson::GetNumber(*SystemObject, TEXT("ram_free")) / (1024.0 * 1024.0 * 1024.0));
    }

    const TArray<TSharedPtr<FJsonValue>>* Devices = nullptr;
    if (Root->TryGetArrayField(TEXT("devices"), Devices) && Devices && Devices->Num() > 0)
    {
        const TSharedPtr<FJsonObject>* DeviceObject = nullptr;
        if ((*Devices)[0]->TryGetObject(DeviceObject) && DeviceObject && DeviceObject->IsValid())
        {
            Out->SetStringField(TEXT("device"), ShineMCPJson::GetString(*DeviceObject, TEXT("name")));
            Out->SetNumberField(TEXT("vramFreeGB"), ShineMCPJson::GetNumber(*DeviceObject, TEXT("vram_free")) / (1024.0 * 1024.0 * 1024.0));
        }
    }

    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::GetComfyObjectInfo(const FString& BaseUrl, const FString& ClassType)
{
    const FShineMCPHttpResult Result = RequestComfy(TEXT("GET"), BaseUrl, FString::Printf(TEXT("object_info/%s"), *ClassType), TArray<uint8>(), FString(), 30);

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("classType"), ClassType);

    if (!Result.bSuccess)
    {
        return HttpErrorText(FString::Printf(TEXT("查询节点 %s"), *ClassType), Result);
    }

    TSharedPtr<FJsonObject> Root;
    if (ShineMCPJson::Parse(Result.Body, Root) && Root.IsValid() && Root->HasField(ClassType))
    {
        Out->SetBoolField(TEXT("exists"), true);
        Out->SetStringField(TEXT("objectInfo"), Result.Body);
    }
    else
    {
        Out->SetBoolField(TEXT("exists"), false);
    }

    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::ListCheckpoints(const FString& BaseUrl)
{
    const FShineMCPHttpResult Result = RequestComfy(TEXT("GET"), BaseUrl, TEXT("object_info/CheckpointLoaderSimple"), TArray<uint8>(), FString(), 30);
    if (!Result.bSuccess)
    {
        return HttpErrorText(TEXT("查询 checkpoint 列表"), Result);
    }

    TSharedPtr<FJsonObject> Root;
    if (!ShineMCPJson::Parse(Result.Body, Root) || !Root.IsValid())
    {
        return ErrorText(TEXT("object_info/CheckpointLoaderSimple 解析失败。"));
    }

    TArray<TSharedPtr<FJsonValue>> Names;

    const TSharedPtr<FJsonObject>* NodeObject = nullptr;
    const TSharedPtr<FJsonObject>* InputObject = nullptr;
    const TSharedPtr<FJsonObject>* RequiredObject = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* CkptOptions = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* CkptOptionsNested = nullptr;

    if (Root->TryGetObjectField(TEXT("CheckpointLoaderSimple"), NodeObject) && NodeObject && NodeObject->IsValid() &&
        (*NodeObject)->TryGetObjectField(TEXT("input"), InputObject) && InputObject && InputObject->IsValid() &&
        (*InputObject)->TryGetObjectField(TEXT("required"), RequiredObject) && RequiredObject && RequiredObject->IsValid() &&
        (*RequiredObject)->TryGetArrayField(TEXT("ckpt_name"), CkptOptionsNested) && CkptOptionsNested && CkptOptionsNested->Num() > 0 &&
        (*CkptOptionsNested)[0]->TryGetArray(CkptOptions) && CkptOptions)
    {
        for (const TSharedPtr<FJsonValue>& Value : *CkptOptions)
        {
            FString Name;
            if (Value.IsValid() && Value->TryGetString(Name))
            {
                Names.Add(MakeShared<FJsonValueString>(Name));
            }
        }
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetArrayField(TEXT("checkpoints"), Names);
    return ShineMCPJson::ToText(Out);
}

// -------------------------------------------------------------- 图片与任务

FString UShineComfyBridgeLibrary::UploadImage(const FString& BaseUrl, const FString& FilePath, const FString& Subfolder, bool bOverwrite)
{
    TArray<uint8> FileData;
    FString LoadError;
    if (!FShineMCPHttpClient::LoadFileBytes(FilePath, FileData, LoadError))
    {
        return ErrorText(LoadError);
    }

    TArray<TPair<FString, FString>> TextFields;
    TextFields.Emplace(TEXT("type"), TEXT("input"));
    TextFields.Emplace(TEXT("subfolder"), Subfolder);
    TextFields.Emplace(TEXT("overwrite"), bOverwrite ? TEXT("true") : TEXT("false"));

    TArray<FShineMCPHttpClient::FMultipartFile> Files;
    FShineMCPHttpClient::FMultipartFile File;
    File.FieldName = TEXT("image");
    File.FileName = FPaths::GetCleanFilename(FilePath);
    File.ContentType = TEXT("image/png");
    File.Data = MoveTemp(FileData);
    Files.Add(MoveTemp(File));

    const FString Base = NormalizeComfyBase(BaseUrl);
    FShineMCPHttpResult Result = FShineMCPHttpClient::UploadMultipart(Base + TEXT("/api/upload/image"), TextFields, Files, 120);
    if (Result.StatusCode == 404)
    {
        Result = FShineMCPHttpClient::UploadMultipart(Base + TEXT("/upload/image"), TextFields, Files, 120);
    }

    if (!Result.bSuccess)
    {
        return HttpErrorText(TEXT("上传图片"), Result);
    }

    TSharedPtr<FJsonObject> Root;
    if (!ShineMCPJson::Parse(Result.Body, Root) || !Root.IsValid())
    {
        return ErrorText(TEXT("upload/image 返回不是合法 JSON。"), Result.Body.Left(400));
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("name"), ShineMCPJson::GetString(Root, TEXT("name")));
    Out->SetStringField(TEXT("subfolder"), ShineMCPJson::GetString(Root, TEXT("subfolder")));
    Out->SetStringField(TEXT("type"), ShineMCPJson::GetString(Root, TEXT("type"), TEXT("input")));
    Out->SetStringField(TEXT("localPath"), FilePath);
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::SubmitPrompt(const FString& BaseUrl, const FString& PromptJson, const FString& ClientId)
{
    TSharedPtr<FJsonObject> PromptObject;
    if (!ShineMCPJson::Parse(PromptJson, PromptObject) || !PromptObject.IsValid())
    {
        return ErrorText(TEXT("prompt JSON 解析失败。"), PromptJson.Left(400));
    }

    TSharedPtr<FJsonObject> RequestObject = ShineMCPJson::NewObject();
    RequestObject->SetObjectField(TEXT("prompt"), PromptObject);
    RequestObject->SetStringField(TEXT("client_id"), ClientId.IsEmpty() ? FString(TEXT("shine-mcp")) : ClientId);

    const FShineMCPHttpResult Result = PostJsonComfy(BaseUrl, TEXT("prompt"), ShineMCPJson::ToText(RequestObject), 120);

    TSharedPtr<FJsonObject> ResponseObject;
    ShineMCPJson::Parse(Result.Body, ResponseObject);

    if (!Result.bSuccess)
    {
        TSharedPtr<FJsonObject> Out = ShineMCPJson::Error(FString::Printf(TEXT("提交任务失败: %s"), *Result.Error));
        Out->SetNumberField(TEXT("statusCode"), Result.StatusCode);
        if (ResponseObject.IsValid())
        {
            const TSharedPtr<FJsonObject>* NodeErrors = nullptr;
            if (ResponseObject->TryGetObjectField(TEXT("node_errors"), NodeErrors) && NodeErrors && NodeErrors->IsValid())
            {
                Out->SetObjectField(TEXT("nodeErrors"), *NodeErrors);
            }
            Out->SetStringField(TEXT("rawResponse"), Result.Body.Left(4000));
        }
        return ShineMCPJson::ToText(Out);
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("promptId"), ShineMCPJson::GetString(ResponseObject, TEXT("prompt_id")));
    Out->SetNumberField(TEXT("number"), ShineMCPJson::GetNumber(ResponseObject, TEXT("number")));
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::GetQueueStatus(const FString& BaseUrl)
{
    const FShineMCPHttpResult Result = RequestComfy(TEXT("GET"), BaseUrl, TEXT("queue"), TArray<uint8>(), FString(), 20);
    if (!Result.bSuccess)
    {
        return HttpErrorText(TEXT("查询队列"), Result);
    }

    TSharedPtr<FJsonObject> Root;
    if (!ShineMCPJson::Parse(Result.Body, Root) || !Root.IsValid())
    {
        return ErrorText(TEXT("queue 返回不是合法 JSON。"), Result.Body.Left(400));
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();

    const TArray<TSharedPtr<FJsonValue>>* Running = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Pending = nullptr;
    if (Root->TryGetArrayField(TEXT("queue_running"), Running) && Running)
    {
        Out->SetNumberField(TEXT("running"), Running->Num());
    }
    if (Root->TryGetArrayField(TEXT("queue_pending"), Pending) && Pending)
    {
        Out->SetNumberField(TEXT("pending"), Pending->Num());
    }

    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::GetPromptResult(const FString& BaseUrl, const FString& PromptId)
{
    if (PromptId.IsEmpty())
    {
        return ErrorText(TEXT("promptId 不能为空。"));
    }

    const FShineMCPHttpResult Result = RequestComfy(TEXT("GET"), BaseUrl,
        FString::Printf(TEXT("history/%s"), *PromptId), TArray<uint8>(), FString(), 30);

    if (!Result.bSuccess)
    {
        return HttpErrorText(TEXT("查询 history"), Result);
    }

    TSharedPtr<FJsonObject> Root;
    ShineMCPJson::Parse(Result.Body, Root);

    const TSharedPtr<FJsonObject>* Entry = nullptr;
    if (Root.IsValid() && Root->TryGetObjectField(PromptId, Entry) && Entry && Entry->IsValid())
    {
        TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
        Out->SetStringField(TEXT("promptId"), PromptId);

        FString StatusStr = TEXT("success");
        bool bCompleted = true;
        const TSharedPtr<FJsonObject>* StatusObject = nullptr;
        if ((*Entry)->TryGetObjectField(TEXT("status"), StatusObject) && StatusObject && StatusObject->IsValid())
        {
            StatusStr = ShineMCPJson::GetString(*StatusObject, TEXT("status_str"), TEXT("success"));
            bCompleted = ShineMCPJson::GetBool(*StatusObject, TEXT("completed"), true);
        }

        Out->SetStringField(TEXT("status"), bCompleted ? TEXT("done") : TEXT("running"));
        Out->SetStringField(TEXT("statusStr"), StatusStr);

        const bool bIsError = StatusStr.Equals(TEXT("error"), ESearchCase::IgnoreCase);
        Out->SetBoolField(TEXT("isError"), bIsError);

        // 出错时把 ComfyUI 的真实异常带出来：只看 status_str 没法定位问题。
        if (bIsError && StatusObject && StatusObject->IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
            if ((*StatusObject)->TryGetArrayField(TEXT("messages"), Messages) && Messages)
            {
                for (const TSharedPtr<FJsonValue>& MessageValue : *Messages)
                {
                    const TArray<TSharedPtr<FJsonValue>>* MessageArray = nullptr;
                    if (!MessageValue.IsValid() || !MessageValue->TryGetArray(MessageArray) || !MessageArray || MessageArray->Num() < 2)
                    {
                        continue;
                    }

                    FString EventName;
                    (*MessageArray)[0]->TryGetString(EventName);
                    if (!EventName.Equals(TEXT("execution_error"), ESearchCase::IgnoreCase))
                    {
                        continue;
                    }

                    const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
                    if (!(*MessageArray)[1]->TryGetObject(ErrorObject) || !ErrorObject || !ErrorObject->IsValid())
                    {
                        continue;
                    }

                    Out->SetStringField(TEXT("exceptionType"), ShineMCPJson::GetString(*ErrorObject, TEXT("exception_type")));
                    Out->SetStringField(TEXT("exceptionMessage"), ShineMCPJson::GetString(*ErrorObject, TEXT("exception_message")));
                    Out->SetStringField(TEXT("failedNodeId"), ShineMCPJson::GetString(*ErrorObject, TEXT("node_id")));
                    Out->SetStringField(TEXT("failedNodeType"), ShineMCPJson::GetString(*ErrorObject, TEXT("node_type")));

                    const TArray<TSharedPtr<FJsonValue>>* TracebackArray = nullptr;
                    if ((*ErrorObject)->TryGetArrayField(TEXT("traceback"), TracebackArray) && TracebackArray)
                    {
                        FString Traceback;
                        for (const TSharedPtr<FJsonValue>& Line : *TracebackArray)
                        {
                            FString LineText;
                            if (Line.IsValid() && Line->TryGetString(LineText))
                            {
                                Traceback += LineText;
                            }
                        }
                        Out->SetStringField(TEXT("traceback"), Traceback.Right(3000));
                    }

                    break;
                }
            }
        }

        TArray<TSharedPtr<FJsonValue>> Images;

        const TSharedPtr<FJsonObject>* Outputs = nullptr;
        if ((*Entry)->TryGetObjectField(TEXT("outputs"), Outputs) && Outputs && Outputs->IsValid())
        {
            for (const auto& NodePair : (*Outputs)->Values)
            {
                const TSharedPtr<FJsonObject>* NodeObject = nullptr;
                if (!NodePair.Value.IsValid() || !NodePair.Value->TryGetObject(NodeObject) || !NodeObject || !NodeObject->IsValid())
                {
                    continue;
                }

                for (const FString& Key : { FString(TEXT("images")), FString(TEXT("gifs")), FString(TEXT("video")) })
                {
                    const TArray<TSharedPtr<FJsonValue>>* ImageArray = nullptr;
                    if (!(*NodeObject)->TryGetArrayField(Key, ImageArray) || !ImageArray)
                    {
                        continue;
                    }

                    for (const TSharedPtr<FJsonValue>& ImageValue : *ImageArray)
                    {
                        const TSharedPtr<FJsonObject>* ImageObject = nullptr;
                        if (!ImageValue.IsValid() || !ImageValue->TryGetObject(ImageObject) || !ImageObject || !ImageObject->IsValid())
                        {
                            continue;
                        }

                        const FString ImageFilename = ShineMCPJson::GetString(*ImageObject, TEXT("filename"));
                        const FString ImageSubfolder = ShineMCPJson::GetString(*ImageObject, TEXT("subfolder"));

                        TSharedPtr<FJsonObject> ImageInfo = ShineMCPJson::NewObject();
                        ImageInfo->SetStringField(TEXT("nodeId"), NodePair.Key);
                        ImageInfo->SetStringField(TEXT("filename"), ImageFilename);
                        ImageInfo->SetStringField(TEXT("subfolder"), ImageSubfolder);
                        ImageInfo->SetStringField(TEXT("type"), ShineMCPJson::GetString(*ImageObject, TEXT("type"), TEXT("output")));

                        // ComfyUI 写出的原图在磁盘上的位置：配了 output 目录就直接读它，
                        // 不需要下载副本、也不需要导入成 UE 资产。
                        const FString LocalPath = ShineComfyPaths::MakeLocalImagePath(ImageSubfolder, ImageFilename);
                        if (!LocalPath.IsEmpty() && FPaths::FileExists(LocalPath))
                        {
                            ImageInfo->SetStringField(TEXT("localPath"), LocalPath);
                        }

                        Images.Add(MakeShared<FJsonValueObject>(ImageInfo));
                    }
                }
            }
        }

        Out->SetArrayField(TEXT("images"), Images);
        return ShineMCPJson::ToText(Out);
    }

    // history 里还没有 → 看看是否还在队列里
    const FShineMCPHttpResult QueueResult = RequestComfy(TEXT("GET"), BaseUrl, TEXT("queue"), TArray<uint8>(), FString(), 20);

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("promptId"), PromptId);
    Out->SetArrayField(TEXT("images"), TArray<TSharedPtr<FJsonValue>>());

    bool bQueued = false;
    TSharedPtr<FJsonObject> QueueRoot;
    if (QueueResult.bSuccess && ShineMCPJson::Parse(QueueResult.Body, QueueRoot) && QueueRoot.IsValid())
    {
        for (const FString& Key : { FString(TEXT("queue_running")), FString(TEXT("queue_pending")) })
        {
            const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
            if (!QueueRoot->TryGetArrayField(Key, Entries) || !Entries)
            {
                continue;
            }

            for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
            {
                const TArray<TSharedPtr<FJsonValue>>* EntryArray = nullptr;
                if (EntryValue.IsValid() && EntryValue->TryGetArray(EntryArray) && EntryArray && EntryArray->Num() > 1)
                {
                    FString QueuedId;
                    if ((*EntryArray)[1]->TryGetString(QueuedId) && QueuedId == PromptId)
                    {
                        bQueued = true;
                        Out->SetStringField(TEXT("status"), Key == TEXT("queue_running") ? TEXT("running") : TEXT("pending"));
                        break;
                    }
                }
            }

            if (bQueued)
            {
                break;
            }
        }
    }

    if (!bQueued)
    {
        Out->SetStringField(TEXT("status"), TEXT("not_found"));
        Out->SetStringField(TEXT("hint"), TEXT("任务既不在 history 也不在 queue：可能刚提交还没入队，或已被清空。"));
    }

    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::DownloadImage(
    const FString& BaseUrl,
    const FString& Filename,
    const FString& Subfolder,
    const FString& Type,
    const FString& SavePath)
{
    if (Filename.IsEmpty())
    {
        return ErrorText(TEXT("filename 不能为空。"));
    }

    const FString Query = FString::Printf(TEXT("view?filename=%s&subfolder=%s&type=%s"),
        *Filename, *Subfolder, *Type);

    const FShineMCPHttpResult Result = RequestComfy(TEXT("GET"), BaseUrl, Query, TArray<uint8>(), FString(), 180);
    if (!Result.bSuccess)
    {
        return HttpErrorText(TEXT("下载图片"), Result);
    }

    FString TargetPath = SavePath;
    if (TargetPath.IsEmpty())
    {
        TargetPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShineComfy"), Filename);
    }
    TargetPath = FPaths::ConvertRelativePathToFull(TargetPath);

    FString SaveError;
    if (!FShineMCPHttpClient::SaveFileBytes(TargetPath, Result.RawBody, SaveError))
    {
        return ErrorText(SaveError);
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("path"), TargetPath);
    Out->SetNumberField(TEXT("bytes"), Result.RawBody.Num());
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::Interrupt(const FString& BaseUrl)
{
    const FShineMCPHttpResult Result = PostJsonComfy(BaseUrl, TEXT("interrupt"), TEXT("{}"), 20);
    if (!Result.bSuccess)
    {
        return HttpErrorText(TEXT("中断任务"), Result);
    }

    return ShineMCPJson::ToText(ShineMCPJson::Success());
}

// ------------------------------------------------------- Shine Comfy 图资产

FString UShineComfyBridgeLibrary::CreateComfyGraphAsset(const FString& PackagePath, const FString& AssetName, const FString& ComfyBaseUrl)
{
    FString SafePath = PackagePath.IsEmpty() ? TEXT("/Game/ShineAI/Comfy") : PackagePath;
    FString SafeName = AssetName.IsEmpty() ? TEXT("SA_ComfyGraph") : AssetName;

    while (SafePath.EndsWith(TEXT("/")))
    {
        SafePath.LeftChopInline(1);
    }

    if (!SafePath.StartsWith(TEXT("/")))
    {
        SafePath = TEXT("/") + SafePath;
    }

    const FString PackageName = FString::Printf(TEXT("%s/%s"), *SafePath, *SafeName);

    // 已存在就直接返回，保证幂等。
    if (FindObject<UObject>(nullptr, *ToObjectPath(PackageName)))
    {
        TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
        Out->SetStringField(TEXT("assetPath"), PackageName);
        Out->SetBoolField(TEXT("created"), false);
        Out->SetStringField(TEXT("message"), TEXT("资产已存在，直接复用。"));
        return ShineMCPJson::ToText(Out);
    }

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return ErrorText(FString::Printf(TEXT("创建包失败: %s"), *PackageName));
    }

    UShineComfyAsset* Asset = NewObject<UShineComfyAsset>(Package, *SafeName, RF_Public | RF_Standalone | RF_Transactional);
    if (!Asset)
    {
        return ErrorText(TEXT("创建 ShineComfyAsset 失败。"));
    }

    Asset->ComfyBaseUrl = NormalizeComfyBase(ComfyBaseUrl);
    Asset->GetOrCreateGraph();

    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();

    const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("assetPath"), PackageName);
    Out->SetStringField(TEXT("objectPath"), ToObjectPath(PackageName));
    Out->SetStringField(TEXT("comfyBaseUrl"), Asset->ComfyBaseUrl);
    Out->SetBoolField(TEXT("created"), true);
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::BuildComfyGraphFromJson(const FString& AssetPath, const FString& GraphJson)
{
    FString LoadError;
    UShineComfyAsset* Asset = LoadComfyAsset(AssetPath, LoadError);
    if (!Asset)
    {
        return ErrorText(LoadError);
    }

    UShineComfyGraph* Graph = Asset->GetOrCreateGraph();
    if (!Graph)
    {
        return ErrorText(TEXT("资产里没有图。"));
    }

    FString ImportError;
    if (!Graph->ImportGraphJson(GraphJson, ImportError))
    {
        return ErrorText(FString::Printf(TEXT("导入图 JSON 失败: %s"), *ImportError));
    }

    Asset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("assetPath"), AssetPath);
    Out->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::ExportComfyPromptJson(const FString& AssetPath)
{
    FString LoadError;
    UShineComfyAsset* Asset = LoadComfyAsset(AssetPath, LoadError);
    if (!Asset)
    {
        return ErrorText(LoadError);
    }

    UShineComfyGraph* Graph = Asset->GetOrCreateGraph();
    if (!Graph)
    {
        return ErrorText(TEXT("资产里没有图。"));
    }

    FString PromptJson;
    FString ExportError;
    if (!Graph->ExportComfyPromptToJson(PromptJson, ExportError))
    {
        return ErrorText(FString::Printf(TEXT("导出 prompt 失败: %s"), *ExportError));
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("assetPath"), AssetPath);
    Out->SetStringField(TEXT("baseUrl"), Asset->ComfyBaseUrl);
    Out->SetStringField(TEXT("promptJson"), PromptJson);
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::ExportComfyGraphDefinition(const FString& AssetPath)
{
    FString LoadError;
    UShineComfyAsset* Asset = LoadComfyAsset(AssetPath, LoadError);
    if (!Asset)
    {
        return ErrorText(LoadError);
    }

    UShineComfyGraph* Graph = Asset->GetOrCreateGraph();
    if (!Graph)
    {
        return ErrorText(TEXT("资产里没有图。"));
    }

    FString DefinitionJson;
    if (!Graph->ExportGraphDefinitionToJson(DefinitionJson))
    {
        return ErrorText(TEXT("导出图定义失败。"));
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("assetPath"), AssetPath);
    Out->SetStringField(TEXT("graphDefinition"), DefinitionJson);
    return ShineMCPJson::ToText(Out);
}

FString UShineComfyBridgeLibrary::OpenAsset(const FString& AssetPath)
{
    const FString ObjectPath = ToObjectPath(AssetPath);
    UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
    if (!Asset)
    {
        return ErrorText(FString::Printf(TEXT("资产不存在: %s"), *ObjectPath));
    }

    if (!GEditor)
    {
        return ErrorText(TEXT("GEditor 不可用。"));
    }

    GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("assetPath"), ObjectPath);
    return ShineMCPJson::ToText(Out);
}

// -------------------------------------------------------------- 结果回读

FString UShineComfyBridgeLibrary::ImportTextureFromFile(const FString& SourceFile, const FString& PackagePath, const FString& AssetName)
{
    if (!FPaths::FileExists(SourceFile))
    {
        return ErrorText(FString::Printf(TEXT("文件不存在: %s"), *SourceFile));
    }

    FString SafePath = PackagePath.IsEmpty() ? TEXT("/Game/ShineAI/Comfy/Output") : PackagePath;
    const FString SafeName = AssetName.IsEmpty() ? FPaths::GetBaseFilename(SourceFile) : AssetName;

    UAssetImportTask* Task = NewObject<UAssetImportTask>();
    Task->Filename = SourceFile;
    Task->DestinationPath = SafePath;
    Task->DestinationName = SafeName;
    Task->bReplaceExisting = true;
    Task->bAutomated = true;
    Task->bSave = true;
    Task->bAsync = false;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    TArray<UAssetImportTask*> ImportTasks;
    ImportTasks.Add(Task);
    AssetToolsModule.Get().ImportAssetTasks(ImportTasks);

    const TArray<UObject*>& Imported = Task->GetObjects();
    if (Imported.Num() == 0 || !Imported[0])
    {
        return ErrorText(FString::Printf(TEXT("导入纹理失败: %s"), *SourceFile));
    }

    TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
    Out->SetStringField(TEXT("assetPath"), Imported[0]->GetPathName());
    Out->SetStringField(TEXT("sourceFile"), SourceFile);
    return ShineMCPJson::ToText(Out);
}
