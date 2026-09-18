#include "ShineMCPHttpServer.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
    constexpr int32 MaxRequestBytes = 64 * 1024 * 1024;

    const TCHAR* GetStatusText(int32 StatusCode)
    {
        switch (StatusCode)
        {
        case 200: return TEXT("OK");
        case 201: return TEXT("Created");
        case 202: return TEXT("Accepted");
        case 204: return TEXT("No Content");
        case 400: return TEXT("Bad Request");
        case 404: return TEXT("Not Found");
        case 405: return TEXT("Method Not Allowed");
        case 500: return TEXT("Internal Server Error");
        case 504: return TEXT("Gateway Timeout");
        default:  return TEXT("OK");
        }
    }

    bool SendAllBytes(FSocket* Socket, const uint8* Data, int64 Length, double DeadlineSeconds)
    {
        int64 Sent = 0;
        while (Sent < Length)
        {
            if (FPlatformTime::Seconds() > DeadlineSeconds)
            {
                return false;
            }

            const int32 ToSend = static_cast<int32>(FMath::Min<int64>(Length - Sent, 256 * 1024));
            int32 BytesSent = 0;
            if (Socket->Send(Data + Sent, ToSend, BytesSent) && BytesSent > 0)
            {
                Sent += BytesSent;
            }
            else
            {
                FPlatformProcess::Sleep(0.001f);
            }
        }
        return true;
    }

    int32 FindHeaderEnd(const TArray<uint8>& Buffer)
    {
        for (int32 Index = 0; Index + 3 < Buffer.Num(); ++Index)
        {
            if (Buffer[Index] == '\r' && Buffer[Index + 1] == '\n' && Buffer[Index + 2] == '\r' && Buffer[Index + 3] == '\n')
            {
                return Index + 4;
            }
        }
        return INDEX_NONE;
    }
}

FString FShineMCPHttpServer::FHttpRequest::GetQueryParam(const FString& Key, const FString& Default) const
{
    TArray<FString> Pairs;
    Query.ParseIntoArray(Pairs, TEXT("&"), true);
    for (const FString& Pair : Pairs)
    {
        FString Name;
        FString Value;
        if (Pair.Split(TEXT("="), &Name, &Value))
        {
            if (Name.Equals(Key, ESearchCase::IgnoreCase))
            {
                return Value;
            }
        }
        else if (Pair.Equals(Key, ESearchCase::IgnoreCase))
        {
            return FString();
        }
    }
    return Default;
}

FShineMCPHttpServer::FShineMCPHttpServer()
{
}

FShineMCPHttpServer::~FShineMCPHttpServer()
{
    Stop();
}

bool FShineMCPHttpServer::Start(const FString& InAddress, int32 InPort, FString& OutErrorMessage)
{
    if (bRunning)
    {
        return true;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        OutErrorMessage = TEXT("Socket 子系统不可用。");
        return false;
    }

    ListenAddress = InAddress.IsEmpty() ? TEXT("127.0.0.1") : InAddress;

    FIPv4Address BindAddress;
    if (!FIPv4Address::Parse(ListenAddress, BindAddress))
    {
        if (!FIPv4Address::Parse(TEXT("127.0.0.1"), BindAddress))
        {
            OutErrorMessage = FString::Printf(TEXT("监听地址无效: %s"), *ListenAddress);
            return false;
        }
        ListenAddress = TEXT("127.0.0.1");
    }

    FSocket* NewListenSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("ShineMCPListen"), false);
    if (!NewListenSocket)
    {
        OutErrorMessage = TEXT("创建监听 Socket 失败。");
        return false;
    }

    NewListenSocket->SetReuseAddr(true);

    bool bBound = false;
    int32 ActualPort = 0;

    for (int32 Attempt = 0; Attempt < 10; ++Attempt)
    {
        const int32 CandidatePort = InPort + Attempt;
        TSharedRef<FInternetAddr> EndpointAddress = SocketSubsystem->CreateInternetAddr();
        EndpointAddress->SetIp(BindAddress.Value);
        EndpointAddress->SetPort(CandidatePort);

        if (NewListenSocket->Bind(*EndpointAddress) && NewListenSocket->Listen(16))
        {
            bBound = true;
            ActualPort = CandidatePort;
            break;
        }
    }

    if (!bBound)
    {
        SocketSubsystem->DestroySocket(NewListenSocket);
        OutErrorMessage = FString::Printf(TEXT("绑定端口失败: %s:%d（其后 10 个端口也被占用）"), *ListenAddress, InPort);
        return false;
    }

    NewListenSocket->SetNonBlocking(true);
    ListenSocket = NewListenSocket;
    BoundPort = ActualPort;
    bRunning = true;
    AliveToken = MakeShared<FAliveToken, ESPMode::ThreadSafe>();

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FShineMCPHttpServer::Tick), 0.01f);

    return true;
}

void FShineMCPHttpServer::Stop()
{
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    if (AliveToken.IsValid())
    {
        AliveToken->bAlive = false;
    }

    // 让还在等待的请求立刻收到 503，而不是一直等到超时。
    {
        TArray<TSharedPtr<FPendingRequest, ESPMode::ThreadSafe>> Local;
        {
            FScopeLock Lock(&PendingMutex);
            Local = MoveTemp(PendingRequests);
            PendingRequests.Reset();
        }

        for (const TSharedPtr<FPendingRequest, ESPMode::ThreadSafe>& Pending : Local)
        {
            if (!Pending.IsValid())
            {
                continue;
            }

            FHttpResponse Response;
            Response.StatusCode = 503;
            Response.Body = TEXT("{\"error\":\"ShineMCP 服务正在关闭\"}");
            WriteHttpResponse(Pending->Socket, Response);
            DestroyClientSocket(Pending->Socket);
            Pending->Socket = nullptr;

            if (Pending->CompletionEvent)
            {
                Pending->CompletionEvent->Trigger();
            }
        }
    }

    CloseSseClients();

    if (ListenSocket)
    {
        FSocket* Socket = ListenSocket;
        ListenSocket = nullptr;
        DestroyClientSocket(Socket);
    }

    bRunning = false;
    BoundPort = 0;
}

FString FShineMCPHttpServer::GetBaseUrl() const
{
    const FString Host = (ListenAddress.IsEmpty() || ListenAddress == TEXT("0.0.0.0")) ? TEXT("127.0.0.1") : ListenAddress;
    return FString::Printf(TEXT("http://%s:%d"), *Host, BoundPort);
}

void FShineMCPHttpServer::DestroyClientSocket(FSocket* Socket)
{
    if (!Socket)
    {
        return;
    }

    Socket->Close();
    if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
    {
        SocketSubsystem->DestroySocket(Socket);
    }
}

int32 FShineMCPHttpServer::GetSseClientCount() const
{
    FScopeLock Lock(&SseClientsMutex);
    return SseClients.Num();
}

void FShineMCPHttpServer::CloseSseClients()
{
    FScopeLock Lock(&SseClientsMutex);
    for (const TSharedPtr<FSseClient, ESPMode::ThreadSafe>& Client : SseClients)
    {
        if (Client.IsValid())
        {
            DestroyClientSocket(Client->Socket);
            Client->Socket = nullptr;
        }
    }
    SseClients.Reset();
}

void FShineMCPHttpServer::PruneSseClients()
{
    FScopeLock Lock(&SseClientsMutex);
    for (int32 Index = SseClients.Num() - 1; Index >= 0; --Index)
    {
        const TSharedPtr<FSseClient, ESPMode::ThreadSafe>& Client = SseClients[Index];
        const bool bDead = !Client.IsValid() || !Client->Socket
            || Client->Socket->GetConnectionState() == ESocketConnectionState::SCS_NotConnected;
        if (bDead)
        {
            if (Client.IsValid())
            {
                DestroyClientSocket(Client->Socket);
                Client->Socket = nullptr;
            }
            SseClients.RemoveAt(Index);
        }
    }
}

void FShineMCPHttpServer::BroadcastSseEvent(const FString& EventName, const FString& Data)
{
    TArray<TSharedPtr<FSseClient, ESPMode::ThreadSafe>> Snapshot;
    {
        FScopeLock Lock(&SseClientsMutex);
        Snapshot = SseClients;
    }

    if (Snapshot.Num() == 0)
    {
        return;
    }

    FString Payload;
    if (!EventName.IsEmpty())
    {
        Payload += FString::Printf(TEXT("event: %s\n"), *EventName);
    }

    TArray<FString> Lines;
    Data.ParseIntoArrayLines(Lines, false);
    for (const FString& Line : Lines)
    {
        FString Sanitized = Line;
        Sanitized.ReplaceInline(TEXT("\r"), TEXT(""));
        Payload += FString::Printf(TEXT("data: %s\n"), *Sanitized);
    }
    Payload += TEXT("\n");

    FTCHARToUTF8 Utf8(*Payload);
    const double Deadline = FPlatformTime::Seconds() + 2.0;
    for (const TSharedPtr<FSseClient, ESPMode::ThreadSafe>& Client : Snapshot)
    {
        if (Client.IsValid() && Client->Socket)
        {
            SendAllBytes(Client->Socket, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Deadline);
        }
    }
}

void FShineMCPHttpServer::ProcessPendingRequests()
{
    TArray<TSharedPtr<FPendingRequest, ESPMode::ThreadSafe>> Local;
    {
        FScopeLock Lock(&PendingMutex);
        if (PendingRequests.Num() == 0)
        {
            return;
        }
        Local = MoveTemp(PendingRequests);
        PendingRequests.Reset();
    }

    for (const TSharedPtr<FPendingRequest, ESPMode::ThreadSafe>& Pending : Local)
    {
        if (!Pending.IsValid())
        {
            continue;
        }

        if (Handler)
        {
            Handler(Pending->Request, Pending->Response);
        }
        else
        {
            Pending->Response.StatusCode = 500;
            Pending->Response.Body = TEXT("{\"error\":\"no handler\"}");
        }

        const bool bWrote = WriteHttpResponse(Pending->Socket, Pending->Response);
        if (bWrote && Pending->Response.bKeepAlive && Pending->Socket)
        {
            // SSE：交给事件流接管，不关闭连接。
            TSharedPtr<FSseClient, ESPMode::ThreadSafe> SseClient = MakeShared<FSseClient, ESPMode::ThreadSafe>();
            SseClient->Socket = Pending->Socket;
            SseClient->LastKeepAliveSeconds = FPlatformTime::Seconds();
            FScopeLock Lock(&SseClientsMutex);
            SseClients.Add(SseClient);
            Pending->Socket = nullptr;
        }
        else
        {
            DestroyClientSocket(Pending->Socket);
            Pending->Socket = nullptr;
        }

        if (Pending->CompletionEvent)
        {
            Pending->CompletionEvent->Trigger();
        }
    }
}

bool FShineMCPHttpServer::Tick(float DeltaTime)
{
    (void)DeltaTime;

    if (!bRunning || !ListenSocket)
    {
        return false;
    }

    ProcessPendingRequests();
    PruneSseClients();

    // SSE 心跳
    {
        const double Now = FPlatformTime::Seconds();
        TArray<TSharedPtr<FSseClient, ESPMode::ThreadSafe>> Snapshot;
        {
            FScopeLock Lock(&SseClientsMutex);
            Snapshot = SseClients;
        }

        const ANSICHAR* Heartbeat = ": keep-alive\n\n";
        const double Deadline = Now + 1.0;
        for (const TSharedPtr<FSseClient, ESPMode::ThreadSafe>& Client : Snapshot)
        {
            if (Client.IsValid() && Client->Socket && (Now - Client->LastKeepAliveSeconds) > 15.0)
            {
                Client->LastKeepAliveSeconds = Now;
                SendAllBytes(Client->Socket, reinterpret_cast<const uint8*>(Heartbeat), FCStringAnsi::Strlen(Heartbeat), Deadline);
            }
        }
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        return true;
    }

    while (true)
    {
        TSharedRef<FInternetAddr> RemoteAddressRef = SocketSubsystem->CreateInternetAddr();
        FSocket* ClientSocket = ListenSocket->Accept(*RemoteAddressRef, TEXT("ShineMCPClient"));
        if (!ClientSocket)
        {
            break;
        }

        ClientSocket->SetNoDelay(true);
        ClientSocket->SetNonBlocking(true);
        TotalRequestCount++;

        const FString RemoteAddressString = RemoteAddressRef->ToString(true);
        TSharedPtr<FAliveToken, ESPMode::ThreadSafe> Token = AliveToken;
        FShineMCPHttpServer* Server = this;
        const int32 ReadTimeout = RequestTimeoutSeconds;

        Async(EAsyncExecution::ThreadPool, [Server, Token, ClientSocket, RemoteAddressString, ReadTimeout]()
        {
            FHttpRequest Request;
            FString ReadError;
            if (!ReadHttpRequest(ClientSocket, ReadTimeout, Request, ReadError))
            {
                // 读失败：直接在线程池里回一个 400，不必占用游戏线程。
                FHttpResponse ErrorResponse;
                ErrorResponse.StatusCode = 400;
                ErrorResponse.Body = FString::Printf(TEXT("{\"error\":\"%s\"}"), *ReadError.ReplaceCharWithEscapedChar());
                WriteHttpResponse(ClientSocket, ErrorResponse);
                DestroyClientSocket(ClientSocket);
                return;
            }

            Request.RemoteAddress = RemoteAddressString;

            if (!Token.IsValid() || !Token->bAlive)
            {
                DestroyClientSocket(ClientSocket);
                return;
            }

            TSharedPtr<FPendingRequest, ESPMode::ThreadSafe> Pending = MakeShared<FPendingRequest, ESPMode::ThreadSafe>();
            Pending->Socket = ClientSocket;
            Pending->Request = MoveTemp(Request);
            Pending->CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);

            {
                FScopeLock Lock(&Server->PendingMutex);
                Server->PendingRequests.Add(Pending);
            }

            // 等游戏线程在 Tick 里处理完。给足够宽裕的时间，因为有些工具（导入资产、
            // 新建关卡）本身就要好几秒。
            const uint32 WaitMilliseconds = static_cast<uint32>(FMath::Max(30, ReadTimeout + 300)) * 1000u;
            const bool bCompleted = Pending->CompletionEvent->Wait(WaitMilliseconds);

            if (!bCompleted)
            {
                // 游戏线程一直没来取：如果还在队列里就由本线程代收尾。
                bool bStillQueued = false;
                {
                    FScopeLock Lock(&Server->PendingMutex);
                    bStillQueued = Server->PendingRequests.Remove(Pending) > 0;
                }

                if (bStillQueued)
                {
                    FHttpResponse TimeoutResponse;
                    TimeoutResponse.StatusCode = 504;
                    TimeoutResponse.Body = TEXT("{\"error\":\"编辑器游戏线程繁忙，请求超时\"}");
                    WriteHttpResponse(ClientSocket, TimeoutResponse);
                    DestroyClientSocket(ClientSocket);
                }
            }

            FPlatformProcess::ReturnSynchEventToPool(Pending->CompletionEvent);
            Pending->CompletionEvent = nullptr;
        });
    }

    return true;
}

bool FShineMCPHttpServer::ReadHttpRequest(FSocket* Socket, int32 TimeoutSeconds, FHttpRequest& OutRequest, FString& OutError)
{
    if (!Socket)
    {
        OutError = TEXT("无效连接。");
        return false;
    }

    const double Deadline = FPlatformTime::Seconds() + FMath::Max(2, TimeoutSeconds);
    TArray<uint8> Buffer;
    TArray<uint8> Chunk;
    Chunk.SetNumUninitialized(16 * 1024);

    int32 HeaderEnd = INDEX_NONE;
    int32 ContentLength = 0;

    while (true)
    {
        if (FPlatformTime::Seconds() > Deadline)
        {
            OutError = TEXT("读取请求超时。");
            return false;
        }

        if (HeaderEnd == INDEX_NONE)
        {
            HeaderEnd = FindHeaderEnd(Buffer);
            if (HeaderEnd != INDEX_NONE)
            {
                const FString HeaderText = FString(HeaderEnd, reinterpret_cast<const ANSICHAR*>(Buffer.GetData()));
                TArray<FString> Lines;
                HeaderText.ParseIntoArrayLines(Lines, false);
                for (int32 Index = 1; Index < Lines.Num(); ++Index)
                {
                    FString Key;
                    FString Value;
                    if (Lines[Index].Split(TEXT(":"), &Key, &Value))
                    {
                        if (Key.TrimStartAndEnd().Equals(TEXT("content-length"), ESearchCase::IgnoreCase))
                        {
                            ContentLength = FCString::Atoi(*Value.TrimStartAndEnd());
                        }
                    }
                }
            }
        }

        if (HeaderEnd != INDEX_NONE && Buffer.Num() >= HeaderEnd + ContentLength)
        {
            break;
        }

        if (Buffer.Num() > MaxRequestBytes)
        {
            OutError = TEXT("请求体过大。");
            return false;
        }

        int32 BytesRead = 0;
        if (Socket->Recv(Chunk.GetData(), Chunk.Num(), BytesRead, ESocketReceiveFlags::None) && BytesRead > 0)
        {
            Buffer.Append(Chunk.GetData(), BytesRead);
            continue;
        }

        if (Socket->GetConnectionState() == ESocketConnectionState::SCS_NotConnected)
        {
            if (HeaderEnd != INDEX_NONE)
            {
                break;
            }

            OutError = TEXT("连接已断开。");
            return false;
        }

        FPlatformProcess::Sleep(0.0005f);
    }

    const FString HeaderText = FString(HeaderEnd, reinterpret_cast<const ANSICHAR*>(Buffer.GetData()));
    TArray<FString> Lines;
    HeaderText.ParseIntoArrayLines(Lines, false);
    if (Lines.Num() == 0)
    {
        OutError = TEXT("请求行为空。");
        return false;
    }

    TArray<FString> RequestParts;
    Lines[0].ParseIntoArray(RequestParts, TEXT(" "), true);
    if (RequestParts.Num() < 2)
    {
        OutError = TEXT("请求行格式错误。");
        return false;
    }

    OutRequest.Verb = RequestParts[0].ToUpper();
    FString PathPart;
    FString QueryPart;
    if (RequestParts[1].Split(TEXT("?"), &PathPart, &QueryPart))
    {
        OutRequest.Path = PathPart;
        OutRequest.Query = QueryPart;
    }
    else
    {
        OutRequest.Path = RequestParts[1];
    }

    for (int32 Index = 1; Index < Lines.Num(); ++Index)
    {
        FString Key;
        FString Value;
        if (Lines[Index].Split(TEXT(":"), &Key, &Value))
        {
            OutRequest.Headers.Add(Key.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
        }
    }

    if (ContentLength > 0 && Buffer.Num() >= HeaderEnd + ContentLength)
    {
        OutRequest.Body = FString(ContentLength, reinterpret_cast<const ANSICHAR*>(Buffer.GetData() + HeaderEnd));
    }

    return true;
}

bool FShineMCPHttpServer::WriteHttpResponse(FSocket* Socket, const FHttpResponse& Response)
{
    if (!Socket)
    {
        return false;
    }

    FTCHARToUTF8 BodyUtf8(*Response.Body);
    const int32 BodyLength = BodyUtf8.Length();

    FString Header = FString::Printf(TEXT("HTTP/1.1 %d %s\r\n"), Response.StatusCode, GetStatusText(Response.StatusCode));
    Header += FString::Printf(TEXT("Content-Type: %s\r\n"), *Response.ContentType);
    if (Response.bKeepAlive)
    {
        Header += TEXT("Cache-Control: no-cache\r\n");
        Header += TEXT("Connection: keep-alive\r\n");
        Header += TEXT("X-Accel-Buffering: no\r\n");
    }
    else
    {
        Header += FString::Printf(TEXT("Content-Length: %d\r\n"), BodyLength);
        Header += TEXT("Connection: close\r\n");
    }
    Header += TEXT("Access-Control-Allow-Origin: *\r\n");
    Header += TEXT("Access-Control-Allow-Headers: *\r\n");
    Header += TEXT("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
    Header += TEXT("\r\n");

    FTCHARToUTF8 HeaderUtf8(*Header);
    TArray<uint8> OutBytes;
    OutBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
    OutBytes.Append(reinterpret_cast<const uint8*>(BodyUtf8.Get()), BodyUtf8.Length());

    const double Deadline = FPlatformTime::Seconds() + 20.0;
    return SendAllBytes(Socket, OutBytes.GetData(), OutBytes.Num(), Deadline);
}
