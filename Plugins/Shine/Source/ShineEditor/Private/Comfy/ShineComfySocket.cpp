#include "Comfy/ShineComfySocket.h"

#include "Comfy/ShineComfyClient.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"

namespace
{
    /** ComfyUI 的字段可能是字符串也可能是数字，还可能是 null；拿不到就返回空串。 */
    FString ReadStringField(const TSharedPtr<FJsonObject>& Object, const FString& Field)
    {
        if (!Object.IsValid())
        {
            return FString();
        }

        FString Value;
        if (Object->TryGetStringField(Field, Value))
        {
            return Value;
        }

        if (Object->HasTypedField<EJson::Number>(Field))
        {
            return FString::Printf(TEXT("%.0f"), Object->GetNumberField(Field));
        }

        return FString();
    }

    void ReadIntField(const TSharedPtr<FJsonObject>& Object, const FString& Field, int32& OutNumber)
    {
        OutNumber = 0;
        if (Object.IsValid() && Object->HasTypedField<EJson::Number>(Field))
        {
            OutNumber = static_cast<int32>(Object->GetNumberField(Field));
        }
    }
}

FShineComfySocket& FShineComfySocket::Get()
{
    static FShineComfySocket Instance;
    return Instance;
}

const FString& FShineComfySocket::GetClientId()
{
    // 懒生成：第一次要 id 的时候就得有，不能等连接建好才给，
    // 否则调用方提交任务时拿到空 id，事件就回不来了。
    if (ClientId.IsEmpty())
    {
        ClientId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    }

    return ClientId;
}

void FShineComfySocket::EnsureConnected(const FString& BaseUrl)
{
    const FString NormalizedUrl = FShineComfyClient::NormalizeBaseUrl(BaseUrl);
    if (NormalizedUrl.IsEmpty())
    {
        return;
    }

    // 换了服务地址：旧连接没意义了，重来一条。
    if (!ServiceUrl.Equals(NormalizedUrl, ESearchCase::IgnoreCase))
    {
        TeardownSocket();
        ServiceUrl = NormalizedUrl;
    }

    GetClientId();

    if (!Socket.IsValid())
    {
        SetupSocket();
    }

    if (Socket.IsValid() && !bConnected && !bConnecting)
    {
        bConnecting = true;
        Socket->Connect();
    }

    ScheduleReconnect();
}

void FShineComfySocket::Shutdown()
{
    bShutdown = true;
    TeardownSocket();

    if (ReconnectHandle.IsValid())
    {
        FTSTicker::RemoveTicker(ReconnectHandle);
        ReconnectHandle.Reset();
    }

    ServiceUrl.Empty();
    WebSocketUrl.Empty();
    ClientId.Empty();
}

void FShineComfySocket::SetupSocket()
{
    if (ServiceUrl.IsEmpty())
    {
        return;
    }

    WebSocketUrl = FShineComfyClient::BuildWebSocketUrl(ServiceUrl, GetClientId());

    FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    Socket = FWebSocketsModule::Get().CreateWebSocket(WebSocketUrl);
    if (!Socket.IsValid())
    {
        return;
    }

    // 这个对象是静态单例、生命周期贯穿整个进程，所以 lambda 里直接捕 this 是安全的。
    Socket->OnConnected().AddLambda([this]()
    {
        HandleConnected();
    });

    Socket->OnConnectionError().AddLambda([this](const FString& Error)
    {
        HandleConnectionError(Error);
    });

    Socket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean)
    {
        HandleClosed(StatusCode, Reason, bWasClean);
    });

    Socket->OnMessage().AddLambda([this](const FString& Message)
    {
        HandleRawMessage(Message);
    });
}

void FShineComfySocket::TeardownSocket()
{
    bConnected = false;
    bConnecting = false;

    if (!Socket.IsValid())
    {
        return;
    }

    Socket->OnConnected().Clear();
    Socket->OnConnectionError().Clear();
    Socket->OnClosed().Clear();
    Socket->OnMessage().Clear();

    if (Socket->IsConnected())
    {
        Socket->Close();
    }

    Socket.Reset();
}

void FShineComfySocket::HandleConnected()
{
    bConnected = true;
    bConnecting = false;
    ConnectedEvent.Broadcast();
}

void FShineComfySocket::HandleConnectionError(const FString& ErrorMessage)
{
    // ComfyUI 没开是常态，不打扰用户，等下一次重连。
    bConnected = false;
    bConnecting = false;
    ConnectionErrorEvent.Broadcast(ErrorMessage);
    ScheduleReconnect();
}

void FShineComfySocket::HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    bConnected = false;
    bConnecting = false;
    ClosedEvent.Broadcast(StatusCode, Reason, bWasClean);
    ScheduleReconnect();
}

void FShineComfySocket::ScheduleReconnect()
{
    if (bShutdown || ReconnectHandle.IsValid() || ServiceUrl.IsEmpty())
    {
        return;
    }

    ReconnectHandle = FTSTicker::GetCoreTicker().AddTicker(
        TEXT("ShineComfySocketReconnect"),
        ReconnectIntervalSeconds,
        [this](float DeltaTime) -> bool
        {
            return ReconnectTick(DeltaTime);
        });
}

bool FShineComfySocket::ReconnectTick(float /*DeltaTime*/)
{
    if (bShutdown || ServiceUrl.IsEmpty())
    {
        ReconnectHandle.Reset();
        return false;
    }

    if (bConnected)
    {
        // 连着呢，重连 ticker 留着只会空转；下次断开时会重新排期。
        ReconnectHandle.Reset();
        return false;
    }

    if (!Socket.IsValid())
    {
        SetupSocket();
    }

    if (Socket.IsValid() && !bConnecting)
    {
        bConnecting = true;
        Socket->Connect();
    }

    return true;
}

void FShineComfySocket::HandleRawMessage(const FString& Message)
{
    // 先原样转发：主面板那类消费者要自己解析队列 / 历史。
    MessageEvent.Broadcast(Message);

    ParseAndBroadcastPromptEvent(Message);
}

void FShineComfySocket::ParseAndBroadcastPromptEvent(const FString& Message)
{
    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return;
    }

    FString MessageType;
    if (!RootObject->TryGetStringField(TEXT("type"), MessageType))
    {
        return;
    }

    // 只关心执行相关的事件；status / 预览之类的不是给任务用的。
    static const TSet<FString> HandledTypes = {
        TEXT("execution_start"),
        TEXT("executing"),
        TEXT("progress"),
        TEXT("progress_state"),
        TEXT("executed"),
        TEXT("execution_success"),
        TEXT("execution_error"),
        TEXT("execution_interrupted"),
        TEXT("execution_cached")
    };

    if (!HandledTypes.Contains(MessageType))
    {
        return;
    }

    const TSharedPtr<FJsonObject>* DataPtr = nullptr;
    if (!RootObject->TryGetObjectField(TEXT("data"), DataPtr) || !DataPtr || !DataPtr->IsValid())
    {
        return;
    }

    const TSharedPtr<FJsonObject>& Data = *DataPtr;

    FPromptEvent Event;
    Event.Type = MessageType;
    Data->TryGetStringField(TEXT("prompt_id"), Event.PromptId);
    Event.NodeId = ReadStringField(Data, TEXT("node"));

    if (MessageType == TEXT("progress"))
    {
        ReadIntField(Data, TEXT("value"), Event.ProgressValue);
        ReadIntField(Data, TEXT("max"), Event.ProgressMax);
    }
    else if (MessageType == TEXT("execution_cached"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (Data->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
        {
            Event.CachedNodeCount = Nodes->Num();

            // 只记数量没法定位，面板要显示"哪些节点被跳过"就必须留下具体 id。
            for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
            {
                FString CachedNodeId;
                if (NodeValue.IsValid() && NodeValue->TryGetString(CachedNodeId))
                {
                    Event.CachedNodeIds.Add(MoveTemp(CachedNodeId));
                }
            }
        }
    }
    else if (MessageType == TEXT("progress_state"))
    {
        // ComfyUI 0.3.x+ 的整张节点状态图：一次拿到所有节点的 state 与步进。
        const TSharedPtr<FJsonObject>* NodesObject = nullptr;
        if (Data->TryGetObjectField(TEXT("nodes"), NodesObject) && NodesObject && (*NodesObject).IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& NodePair : (*NodesObject)->Values)
            {
                const TSharedPtr<FJsonObject>* NodeObject = nullptr;
                if (!NodePair.Value.IsValid() || !NodePair.Value->TryGetObject(NodeObject) || !NodeObject || !(*NodeObject).IsValid())
                {
                    continue;
                }

                FShineComfyNodeProgress Progress;
                Progress.NodeId = NodePair.Key;
                (*NodeObject)->TryGetStringField(TEXT("state"), Progress.State);

                // value/max 实测既可能是整数也可能是浮点（已完成的节点报 1.0），
                // 所以统一按 double 读再截断 —— 用 TryGetIntegerField 遇到浮点会静默回 0。
                double NumberValue = 0.0;
                if ((*NodeObject)->TryGetNumberField(TEXT("value"), NumberValue))
                {
                    Progress.Value = static_cast<int32>(NumberValue);
                }
                if ((*NodeObject)->TryGetNumberField(TEXT("max"), NumberValue))
                {
                    Progress.Max = static_cast<int32>(NumberValue);
                }

                Event.NodeStates.Add(MoveTemp(Progress));
            }
        }
    }
    else if (MessageType == TEXT("executed"))
    {
        // executed 的 data.output.images[0] = { filename, subfolder, type }
        const TSharedPtr<FJsonObject>* OutputPtr = nullptr;
        if (Data->TryGetObjectField(TEXT("output"), OutputPtr) && OutputPtr && OutputPtr->IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
            if ((*OutputPtr)->TryGetArrayField(TEXT("images"), Images) && Images && Images->Num() > 0)
            {
                const TSharedPtr<FJsonObject>* ImagePtr = nullptr;
                if ((*Images)[0]->TryGetObject(ImagePtr) && ImagePtr && ImagePtr->IsValid())
                {
                    Event.ImageFileName = ReadStringField(*ImagePtr, TEXT("filename"));
                    Event.ImageSubfolder = ReadStringField(*ImagePtr, TEXT("subfolder"));
                    Event.ImageType = ReadStringField(*ImagePtr, TEXT("type"));
                }
            }
        }
    }
    else if (MessageType == TEXT("execution_error"))
    {
        Event.bFailed = true;
        // 必须留下是哪个节点炸的：H3 的 ref_images 键名写错时报的是
        // "execute() got an unexpected keyword argument"，只有 message 根本定位不到。
        Event.ErrorNodeId = ReadStringField(Data, TEXT("node_id"));
        Event.ErrorNodeType = ReadStringField(Data, TEXT("node_type"));
        Event.ErrorMessage = ReadStringField(Data, TEXT("exception_message"));
        if (Event.ErrorMessage.IsEmpty())
        {
            Event.ErrorMessage = ReadStringField(Data, TEXT("exception_type"));
        }
    }
    else if (MessageType == TEXT("execution_interrupted"))
    {
        Event.bFailed = true;
        Event.ErrorMessage = TEXT("任务被中断。");
    }

    PromptEvent.Broadcast(Event);
}
