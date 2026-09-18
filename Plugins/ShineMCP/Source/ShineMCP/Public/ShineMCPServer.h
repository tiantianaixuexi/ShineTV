#pragma once

#include "CoreMinimal.h"
#include "ShineMCPHttpServer.h"

class FJsonObject;

/**
 * MCP 协议层：在 FShineMCPHttpServer 之上实现 JSON-RPC 2.0 / MCP 语义。
 *
 * 支持的传输：
 *  - Streamable HTTP：POST /mcp，直接返回 JSON-RPC 响应；
 *  - HTTP + SSE（旧版）：GET /sse 建立事件流，客户端把请求 POST 到 /mcp，
 *    响应会同时通过 SSE 通道推回，兼容只支持 SSE 的客户端。
 */
class SHINEMCP_API FShineMCPServer
{
public:
    static FShineMCPServer& Get();

    bool Start(const FString& Address, int32 Port, FString& OutErrorMessage);
    void Stop();
    bool IsRunning() const;
    FString GetUrl() const;
    int32 GetPort() const;

    /** 处理一条 JSON-RPC 消息，返回响应 JSON 文本；若是通知则返回空字符串。 */
    FString HandleJsonRpc(const FString& RequestJson, const FString& TransportHint);

    /** 直接把工具调用包成 JSON-RPC，便于自动化脚本调用。 */
    FString CallToolViaJsonRpc(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);

    FString GetServerName() const;
    FString GetServerVersion() const;

private:
    void HandleHttpRequest(const FShineMCPHttpServer::FHttpRequest& Request, FShineMCPHttpServer::FHttpResponse& OutResponse);

    FString HandleMethod(const FString& Method, const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& RequestId, bool& bOutIsNotification);
    FString HandleToolCall(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& RequestId);

    FShineMCPHttpServer HttpServer;
    int32 RequestCounter = 0;
};
