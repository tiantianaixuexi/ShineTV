#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "Sockets.h"

/**
 * 极简 HTTP/1.1 服务端，用于承载 MCP 传输层。
 *
 * 线程模型（很重要）：
 *  - 监听 socket 由核心 Ticker 在游戏线程里轮询 accept（非阻塞）；
 *  - 收到连接后抛到线程池把请求读完（避免客户端卡住冻结编辑器）；
 *  - 读完后把请求放进待处理队列，**由 Ticker 在游戏线程的 Tick 上下文里执行**，
 *    而不是用 AsyncTask(GameThread)。原因是 AssetTools / Interchange 这类导入接口
 *    内部会 ProcessTasksUntilIdle，在 TaskGraph 的游戏线程任务里调用会触发
 *    "RecursionGuard" 断言直接崩掉编辑器；
 *  - 处理完写响应并唤醒仍在等待的线程池任务。
 */
class SHINEMCP_API FShineMCPHttpServer
{
public:
    struct FHttpRequest
    {
        FString Verb;
        FString Path;
        FString Query;
        TMap<FString, FString> Headers;
        FString Body;
        FString RemoteAddress;

        /** 从 query string 里取参数。 */
        FString GetQueryParam(const FString& Key, const FString& Default = FString()) const;
    };

    struct FHttpResponse
    {
        int32 StatusCode = 200;
        FString ContentType = TEXT("application/json; charset=utf-8");
        FString Body;
        /** true 表示写完后保持连接并由服务端接管该 socket（SSE 用）。 */
        bool bKeepAlive = false;
    };

    using FHandler = TFunction<void(const FHttpRequest& Request, FHttpResponse& OutResponse)>;

    FShineMCPHttpServer();
    ~FShineMCPHttpServer();

    bool Start(const FString& InAddress, int32 InPort, FString& OutErrorMessage);
    void Stop();
    bool IsRunning() const { return bRunning; }
    int32 GetPort() const { return BoundPort; }
    FString GetBaseUrl() const;

    void SetHandler(FHandler InHandler) { Handler = MoveTemp(InHandler); }
    void SetRequestTimeoutSeconds(int32 InSeconds) { RequestTimeoutSeconds = FMath::Clamp(InSeconds, 2, 600); }

    int32 GetTotalRequestCount() const { return TotalRequestCount.Load(); }
    int32 GetSseClientCount() const;

    /** SSE 通道：把一段事件文本推给所有已连接的 SSE 客户端（游戏线程调用）。 */
    void BroadcastSseEvent(const FString& EventName, const FString& Data);

private:
    /** 让工作线程在服务器停止后仍然安全地持有"存活"标记。 */
    struct FAliveToken
    {
        TAtomic<bool> bAlive{ true };
    };

    struct FSseClient
    {
        FSocket* Socket = nullptr;
        double LastKeepAliveSeconds = 0.0;
    };

    /** 一个已读完、等待游戏线程处理的请求。 */
    struct FPendingRequest
    {
        FSocket* Socket = nullptr;
        FHttpRequest Request;
        FHttpResponse Response;
        FEvent* CompletionEvent = nullptr;
    };

    bool Tick(float DeltaTime);
    void ProcessPendingRequests();

    /** 在工作线程上读一条完整 HTTP 请求（带超时）。 */
    static bool ReadHttpRequest(FSocket* Socket, int32 TimeoutSeconds, FHttpRequest& OutRequest, FString& OutError);
    /** 写一条 HTTP 响应。 */
    static bool WriteHttpResponse(FSocket* Socket, const FHttpResponse& Response);
    static void DestroyClientSocket(FSocket* Socket);

    void CloseSseClients();
    void PruneSseClients();

    FString ListenAddress;
    int32 BoundPort = 0;
    FSocket* ListenSocket = nullptr;
    FTSTicker::FDelegateHandle TickerHandle;
    FHandler Handler;
    bool bRunning = false;
    int32 RequestTimeoutSeconds = 30;
    TAtomic<int32> TotalRequestCount{ 0 };
    TSharedPtr<FAliveToken, ESPMode::ThreadSafe> AliveToken;

    mutable FCriticalSection PendingMutex;
    TArray<TSharedPtr<FPendingRequest, ESPMode::ThreadSafe>> PendingRequests;

    mutable FCriticalSection SseClientsMutex;
    TArray<TSharedPtr<FSseClient, ESPMode::ThreadSafe>> SseClients;
};
