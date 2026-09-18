#include "ShineMCPServer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ShineMCPPrivate.h"
#include "ShineMCPSettings.h"
#include "ShineMCPToolRegistry.h"

#define SHINE_MCP_PROTOCOL_VERSION TEXT("2025-06-18")
#define SHINE_MCP_SUPPORTED_PROTOCOLS TEXT("2025-06-18, 2025-03-26, 2024-11-05")

FShineMCPServer& FShineMCPServer::Get()
{
    static FShineMCPServer Instance;
    return Instance;
}

FString FShineMCPServer::GetServerName() const
{
    const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
    return Settings && !Settings->ServerName.IsEmpty() ? Settings->ServerName : TEXT("ShineMCP");
}

FString FShineMCPServer::GetServerVersion() const
{
    const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
    return Settings && !Settings->ServerVersion.IsEmpty() ? Settings->ServerVersion : TEXT("1.0.0");
}

bool FShineMCPServer::IsRunning() const
{
    return HttpServer.IsRunning();
}

int32 FShineMCPServer::GetPort() const
{
    return HttpServer.GetPort();
}

FString FShineMCPServer::GetUrl() const
{
    return HttpServer.GetBaseUrl() + TEXT("/mcp");
}

bool FShineMCPServer::Start(const FString& Address, int32 Port, FString& OutErrorMessage)
{
    if (HttpServer.IsRunning())
    {
        return true;
    }

    const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
    HttpServer.SetRequestTimeoutSeconds(Settings ? Settings->RequestTimeoutSeconds : 15);
    HttpServer.SetHandler(
        [this](const FShineMCPHttpServer::FHttpRequest& Request, FShineMCPHttpServer::FHttpResponse& OutResponse)
        {
            HandleHttpRequest(Request, OutResponse);
        });

    return HttpServer.Start(Address, Port, OutErrorMessage);
}

void FShineMCPServer::Stop()
{
    HttpServer.SetHandler(nullptr);
    HttpServer.Stop();
}

FString FShineMCPServer::HandleJsonRpc(const FString& RequestJson, const FString& TransportHint)
{
    TSharedPtr<FJsonObject> Root;
    if (!ShineMCPJson::Parse(RequestJson, Root) || !Root.IsValid())
    {
        TSharedPtr<FJsonObject> Error = ShineMCPJson::NewObject();
        Error->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
        Error->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
        TSharedPtr<FJsonObject> ErrorObject = ShineMCPJson::NewObject();
        ErrorObject->SetNumberField(TEXT("code"), -32700);
        ErrorObject->SetStringField(TEXT("message"), TEXT("Parse error"));
        Error->SetObjectField(TEXT("error"), ErrorObject);
        return ShineMCPJson::ToText(Error);
    }

    const FString Method = ShineMCPJson::GetString(Root, TEXT("method"));
    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    TSharedPtr<FJsonObject> Params;
    if (Root->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr && ParamsPtr->IsValid())
    {
        Params = *ParamsPtr;
    }

    TSharedPtr<FJsonValue> RequestId;
    if (Root->HasField(TEXT("id")))
    {
        RequestId = Root->TryGetField(TEXT("id"));
    }

    bool bIsNotification = false;
    const FString ResponseBody = HandleMethod(Method, Params, RequestId, bIsNotification);

    if (bIsNotification || ResponseBody.IsEmpty())
    {
        return FString();
    }

    // 通过 SSE 通道把响应也推给旧版 SSE 客户端。
    if (TransportHint == TEXT("sse"))
    {
        HttpServer.BroadcastSseEvent(TEXT("message"), ResponseBody);
    }

    return ResponseBody;
}

FString FShineMCPServer::HandleMethod(
    const FString& Method,
    const TSharedPtr<FJsonObject>& Params,
    const TSharedPtr<FJsonValue>& RequestId,
    bool& bOutIsNotification)
{
    bOutIsNotification = false;

    auto MakeResult = [&RequestId](const TSharedPtr<FJsonObject>& ResultObject) -> FString
    {
        TSharedPtr<FJsonObject> Response = ShineMCPJson::NewObject();
        Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
        Response->SetField(TEXT("id"), RequestId.IsValid() ? RequestId : MakeShared<FJsonValueNull>());
        Response->SetObjectField(TEXT("result"), ResultObject.IsValid() ? ResultObject : ShineMCPJson::NewObject());
        return ShineMCPJson::ToText(Response);
    };

    // ---- 通知类方法：没有响应 ----
    if (Method == TEXT("notifications/initialized") ||
        Method == TEXT("initialized") ||
        Method == TEXT("notifications/cancelled") ||
        Method == TEXT("notifications/roots/list_changed"))
    {
        bOutIsNotification = true;
        return FString();
    }

    if (Method == TEXT("initialize"))
    {
        FString RequestedProtocol = ShineMCPJson::GetString(Params, TEXT("protocolVersion"), SHINE_MCP_PROTOCOL_VERSION);

        TSharedPtr<FJsonObject> Result = ShineMCPJson::NewObject();
        Result->SetStringField(TEXT("protocolVersion"), RequestedProtocol.IsEmpty() ? SHINE_MCP_PROTOCOL_VERSION : RequestedProtocol);

        TSharedPtr<FJsonObject> Capabilities = ShineMCPJson::NewObject();
        TSharedPtr<FJsonObject> ToolsCapability = ShineMCPJson::NewObject();
        ToolsCapability->SetBoolField(TEXT("listChanged"), false);
        Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);
        Capabilities->SetObjectField(TEXT("resources"), ShineMCPJson::NewObject());
        Result->SetObjectField(TEXT("capabilities"), Capabilities);

        TSharedPtr<FJsonObject> ServerInfo = ShineMCPJson::NewObject();
        ServerInfo->SetStringField(TEXT("name"), GetServerName());
        ServerInfo->SetStringField(TEXT("title"), TEXT("Shine MCP for Unreal Engine"));
        ServerInfo->SetStringField(TEXT("version"), GetServerVersion());
        Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

        Result->SetStringField(TEXT("instructions"),
            TEXT("ShineMCP 把 Unreal Engine 5 编辑器暴露给 MCP 客户端：可以新建关卡、摆放场景、"
                 "捕获场景的颜色/深度/法线，并把结果送进 ComfyUI 生成图像。"
                 "常用顺序：ue_get_editor_state → ue_create_level → ue_spawn_actor → "
                 "ue_capture_scene → ue_comfy_upload_image → ue_create_comfy_graph → "
                 "ue_comfy_submit → ue_comfy_prompt_result → ue_comfy_download_image。"));

        return MakeResult(Result);
    }

    if (Method == TEXT("ping"))
    {
        return MakeResult(ShineMCPJson::NewObject());
    }

    if (Method == TEXT("tools/list"))
    {
        return MakeResult(FShineMCPToolRegistry::Get().BuildToolsListJson());
    }

    if (Method == TEXT("tools/call"))
    {
        return HandleToolCall(Params, RequestId);
    }

    if (Method == TEXT("resources/list"))
    {
        TSharedPtr<FJsonObject> Result = ShineMCPJson::NewObject();
        Result->SetArrayField(TEXT("resources"), TArray<TSharedPtr<FJsonValue>>());
        return MakeResult(Result);
    }

    if (Method == TEXT("prompts/list"))
    {
        TSharedPtr<FJsonObject> Result = ShineMCPJson::NewObject();
        Result->SetArrayField(TEXT("prompts"), TArray<TSharedPtr<FJsonValue>>());
        return MakeResult(Result);
    }

    if (Method == TEXT("logging/setLevel"))
    {
        bOutIsNotification = true;
        return FString();
    }

    // 未知方法：按 JSON-RPC 规范返回 method not found。
    TSharedPtr<FJsonObject> Response = ShineMCPJson::NewObject();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), RequestId.IsValid() ? RequestId : MakeShared<FJsonValueNull>());
    TSharedPtr<FJsonObject> ErrorObject = ShineMCPJson::NewObject();
    ErrorObject->SetNumberField(TEXT("code"), -32601);
    ErrorObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Method not found: %s"), *Method));
    Response->SetObjectField(TEXT("error"), ErrorObject);
    return ShineMCPJson::ToText(Response);
}

FString FShineMCPServer::HandleToolCall(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& RequestId)
{
    const FString ToolName = ShineMCPJson::GetString(Params, TEXT("name"));

    TSharedPtr<FJsonObject> Arguments;
    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    if (Params.IsValid() && Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr) && ArgumentsPtr && ArgumentsPtr->IsValid())
    {
        Arguments = *ArgumentsPtr;
    }

    bool bFound = false;
    const FShineMCPResult CallResult = FShineMCPToolRegistry::Get().CallTool(ToolName, Arguments, bFound);

    TSharedPtr<FJsonObject> Result = ShineMCPJson::NewObject();
    TArray<TSharedPtr<FJsonValue>> ContentArray;

    if (!bFound)
    {
        Result->SetBoolField(TEXT("isError"), true);
        TSharedPtr<FJsonObject> Content = ShineMCPJson::NewObject();
        Content->SetStringField(TEXT("type"), TEXT("text"));
        Content->SetStringField(TEXT("text"), FString::Printf(TEXT("未知工具: %s"), *ToolName));
        ContentArray.Add(MakeShared<FJsonValueObject>(Content));
        Result->SetArrayField(TEXT("content"), ContentArray);

        TSharedPtr<FJsonObject> Response = ShineMCPJson::NewObject();
        Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
        Response->SetField(TEXT("id"), RequestId.IsValid() ? RequestId : MakeShared<FJsonValueNull>());
        Response->SetObjectField(TEXT("result"), Result);
        return ShineMCPJson::ToText(Response);
    }

    Result->SetBoolField(TEXT("isError"), !CallResult.bSuccess);

    TSharedPtr<FJsonObject> Content = ShineMCPJson::NewObject();
    Content->SetStringField(TEXT("type"), TEXT("text"));
    Content->SetStringField(TEXT("text"), CallResult.Text);
    ContentArray.Add(MakeShared<FJsonValueObject>(Content));
    Result->SetArrayField(TEXT("content"), ContentArray);

    TSharedPtr<FJsonObject> Response = ShineMCPJson::NewObject();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), RequestId.IsValid() ? RequestId : MakeShared<FJsonValueNull>());
    Response->SetObjectField(TEXT("result"), Result);
    return ShineMCPJson::ToText(Response);
}

FString FShineMCPServer::CallToolViaJsonRpc(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Params = ShineMCPJson::NewObject();
    Params->SetStringField(TEXT("name"), ToolName);
    Params->SetObjectField(TEXT("arguments"), Arguments.IsValid() ? Arguments : ShineMCPJson::NewObject());

    TSharedPtr<FJsonObject> Request = ShineMCPJson::NewObject();
    Request->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Request->SetNumberField(TEXT("id"), ++RequestCounter);
    Request->SetStringField(TEXT("method"), TEXT("tools/call"));
    Request->SetObjectField(TEXT("params"), Params);

    return HandleJsonRpc(ShineMCPJson::ToText(Request), TEXT("http"));
}

void FShineMCPServer::HandleHttpRequest(const FShineMCPHttpServer::FHttpRequest& Request, FShineMCPHttpServer::FHttpResponse& OutResponse)
{
    const FString Path = Request.Path.ToLower();

    // --- CORS 预检 ---
    if (Request.Verb == TEXT("OPTIONS"))
    {
        OutResponse.StatusCode = 204;
        OutResponse.Body.Reset();
        return;
    }

    // --- 健康检查 ---
    if (Path == TEXT("/health") || Path == TEXT("/"))
    {
        TSharedPtr<FJsonObject> Health = ShineMCPJson::Success();
        Health->SetStringField(TEXT("server"), GetServerName());
        Health->SetStringField(TEXT("version"), GetServerVersion());
        Health->SetStringField(TEXT("protocolVersion"), SHINE_MCP_PROTOCOL_VERSION);
        Health->SetStringField(TEXT("supportedProtocols"), SHINE_MCP_SUPPORTED_PROTOCOLS);
        Health->SetNumberField(TEXT("port"), HttpServer.GetPort());
        Health->SetNumberField(TEXT("toolCount"), FShineMCPToolRegistry::Get().GetTools().Num());
        Health->SetNumberField(TEXT("sseClients"), HttpServer.GetSseClientCount());
        Health->SetNumberField(TEXT("requests"), HttpServer.GetTotalRequestCount());
        OutResponse.ContentType = TEXT("application/json; charset=utf-8");
        OutResponse.Body = ShineMCPJson::ToText(Health);
        return;
    }

    // --- SSE 传输（旧版 MCP 客户端） ---
    if (Path == TEXT("/sse") || Path == TEXT("/mcp/sse"))
    {
        if (Request.Verb != TEXT("GET"))
        {
            OutResponse.StatusCode = 405;
            OutResponse.Body = TEXT("{\"error\":\"SSE 只支持 GET\"}");
            return;
        }

        OutResponse.StatusCode = 200;
        OutResponse.ContentType = TEXT("text/event-stream; charset=utf-8");
        OutResponse.bKeepAlive = true;

        FString Endpoint;
        if (Request.Headers.Contains(TEXT("host")))
        {
            Endpoint = FString::Printf(TEXT("http://%s/mcp"), *Request.Headers[TEXT("host")]);
        }
        else
        {
            Endpoint = HttpServer.GetBaseUrl() + TEXT("/mcp");
        }

        OutResponse.Body = FString::Printf(TEXT("event: endpoint\ndata: %s\n\n"), *Endpoint);
        return;
    }

    // --- 便捷 REST：GET /tools 列出工具 ---
    if (Path == TEXT("/tools") && Request.Verb == TEXT("GET"))
    {
        OutResponse.ContentType = TEXT("application/json; charset=utf-8");
        OutResponse.Body = ShineMCPJson::ToText(FShineMCPToolRegistry::Get().BuildToolsListJson());
        return;
    }

    // --- 便捷 REST：POST /tools/<name> 直接调用工具（自动化脚本用） ---
    if (Path.StartsWith(TEXT("/tools/")) && Request.Verb == TEXT("POST"))
    {
        const FString ToolName = Request.Path.RightChop(7);

        TSharedPtr<FJsonObject> Arguments;
        if (!Request.Body.TrimStartAndEnd().IsEmpty())
        {
            ShineMCPJson::Parse(Request.Body, Arguments);
        }

        bool bFound = false;
        const FShineMCPResult CallResult = FShineMCPToolRegistry::Get().CallTool(ToolName, Arguments, bFound);

        OutResponse.StatusCode = bFound ? (CallResult.bSuccess ? 200 : 500) : 404;
        OutResponse.ContentType = TEXT("application/json; charset=utf-8");

        if (!bFound)
        {
            OutResponse.Body = ShineMCPJson::ToText(ShineMCPJson::Error(FString::Printf(TEXT("未知工具: %s"), *ToolName)));
            return;
        }

        // 工具本身返回 JSON 时原样透传，否则包一层。
        TSharedPtr<FJsonObject> Parsed;
        if (ShineMCPJson::Parse(CallResult.Text, Parsed) && Parsed.IsValid())
        {
            OutResponse.Body = CallResult.Text;
        }
        else
        {
            TSharedPtr<FJsonObject> Wrapper = ShineMCPJson::NewObject();
            Wrapper->SetBoolField(TEXT("success"), CallResult.bSuccess);
            Wrapper->SetStringField(TEXT("text"), CallResult.Text);
            OutResponse.Body = ShineMCPJson::ToText(Wrapper);
        }
        return;
    }

    // --- MCP 主入口：POST /mcp ---
    if (Path == TEXT("/mcp") || Path == TEXT("/messages") || Path == TEXT("/message"))
    {
        if (Request.Verb == TEXT("GET"))
        {
            // 部分客户端会先 GET 探测；给一个 405 + 说明。
            OutResponse.StatusCode = 405;
            OutResponse.ContentType = TEXT("application/json; charset=utf-8");
            OutResponse.Body = TEXT("{\"error\":\"请使用 POST /mcp 提交 JSON-RPC，或使用 GET /sse 建立 SSE 通道。\"}");
            return;
        }

        if (Request.Verb != TEXT("POST"))
        {
            OutResponse.StatusCode = 405;
            OutResponse.ContentType = TEXT("application/json; charset=utf-8");
            OutResponse.Body = TEXT("{\"error\":\"只支持 POST\"}");
            return;
        }

        const FString Transport = Request.GetQueryParam(TEXT("transport"), TEXT("http"));
        const FString ResponseBody = HandleJsonRpc(Request.Body, Transport);

        if (ResponseBody.IsEmpty())
        {
            OutResponse.StatusCode = 202;
            OutResponse.Body.Reset();
        }
        else
        {
            OutResponse.StatusCode = 200;
            OutResponse.ContentType = TEXT("application/json; charset=utf-8");
            OutResponse.Body = ResponseBody;
        }
        return;
    }

    OutResponse.StatusCode = 404;
    OutResponse.ContentType = TEXT("application/json; charset=utf-8");
    OutResponse.Body = ShineMCPJson::ToText(ShineMCPJson::Error(FString::Printf(TEXT("未知路径: %s"), *Request.Path)));
}
