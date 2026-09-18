#include "Http/ShineHttpClient.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FShineHttpResponse BuildFailedResponse(EShineHttpErrorCode ErrorCode, const FString& ErrorMessage, int32 StatusCode = 0)
    {
        FShineHttpResponse Response;
        Response.ErrorCode = ErrorCode;
        Response.ErrorMessage = ErrorMessage;
        Response.StatusCode = StatusCode;
        return Response;
    }

    void AppendUtf8(TArray<uint8>& OutBytes, const FString& Text)
    {
        FTCHARToUTF8 Converter(*Text);
        OutBytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
    }

    /** 按 RFC 1867 拼 multipart/form-data body。 */
    TArray<uint8> BuildMultipartBody(const FString& Boundary, const TArray<FShineHttpMultipartField>& Fields)
    {
        TArray<uint8> Body;
        for (const FShineHttpMultipartField& Field : Fields)
        {
            FString Header = TEXT("--") + Boundary + TEXT("\r\n");
            Header += FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\""), *Field.Name);
            if (!Field.FileName.IsEmpty())
            {
                Header += FString::Printf(TEXT("; filename=\"%s\""), *Field.FileName);
            }
            Header += TEXT("\r\n");

            if (!Field.FileName.IsEmpty())
            {
                Header += FString::Printf(
                    TEXT("Content-Type: %s\r\n"),
                    Field.ContentType.IsEmpty() ? TEXT("application/octet-stream") : *Field.ContentType);
            }

            Header += TEXT("\r\n");
            AppendUtf8(Body, Header);

            if (Field.FileName.IsEmpty())
            {
                AppendUtf8(Body, Field.Value);
            }
            else
            {
                Body.Append(Field.Data);
            }

            AppendUtf8(Body, TEXT("\r\n"));
        }

        AppendUtf8(Body, TEXT("--") + Boundary + TEXT("--\r\n"));
        return Body;
    }
}

void FShineHttpClient::Send(const FShineHttpRequest& Request, FOnTextResponse&& Callback)
{
    TSharedRef<FOnTextResponse> SharedCallback = MakeShared<FOnTextResponse>(MoveTemp(Callback));

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(Request.Url);
    HttpRequest->SetVerb(Request.Verb.IsEmpty() ? TEXT("GET") : Request.Verb);

    // 只有显式要求时才覆盖 HttpModule 的默认超时（30 秒）。
    if (Request.TimeoutSeconds > 0.0)
    {
        HttpRequest->SetTimeout(Request.TimeoutSeconds);
    }

    for (const TPair<FString, FString>& Header : Request.Headers)
    {
        HttpRequest->SetHeader(Header.Key, Header.Value);
    }

    if (Request.MultipartFields.Num() > 0)
    {
        const FString Boundary = TEXT("----ShineBoundary") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        HttpRequest->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
        HttpRequest->SetContent(BuildMultipartBody(Boundary, Request.MultipartFields));
    }
    else if (Request.ContentBytes.Num() > 0)
    {
        if (!Request.ContentType.IsEmpty())
        {
            HttpRequest->SetHeader(TEXT("Content-Type"), Request.ContentType);
        }
        HttpRequest->SetContent(Request.ContentBytes);
    }
    else
    {
        if (!Request.ContentType.IsEmpty())
        {
            HttpRequest->SetHeader(TEXT("Content-Type"), Request.ContentType);
        }

        if (!Request.Content.IsEmpty())
        {
            HttpRequest->SetContentAsString(Request.Content);
        }
    }

    HttpRequest->OnProcessRequestComplete().BindLambda([SharedCallback](FHttpRequestPtr SentRequest, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
    {
        if (!bConnectedSuccessfully || !HttpResponse.IsValid())
        {
            (*SharedCallback)(BuildFailedResponse(EShineHttpErrorCode::Network, TEXT("HTTP 请求失败，目标服务不可达。")));
            return;
        }

        FShineHttpResponse Response;
        Response.StatusCode = HttpResponse->GetResponseCode();
        Response.ContentBytes = HttpResponse->GetContent();
        Response.Content = HttpResponse->GetContentAsString();
        Response.bSuccess = EHttpResponseCodes::IsOk(Response.StatusCode);
        for (const FString& HeaderLine : HttpResponse->GetAllHeaders())
        {
            FString HeaderName;
            FString HeaderValue;
            if (HeaderLine.Split(TEXT(":"), &HeaderName, &HeaderValue))
            {
                Response.Headers.Add(HeaderName.TrimStartAndEnd(), HeaderValue.TrimStartAndEnd());
            }
        }

        if (!Response.bSuccess)
        {
            Response.ErrorCode = EShineHttpErrorCode::HttpStatus;
            Response.ErrorMessage = FString::Printf(TEXT("HTTP 请求失败，状态码 %d。"), Response.StatusCode);
        }

        (*SharedCallback)(MoveTemp(Response));
    });

    HttpRequest->ProcessRequest();
}

void FShineHttpClient::Get(const FString& Url, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers)
{
    FShineHttpRequest Request;
    Request.Url = Url;
    Request.Headers = Headers;
    Send(Request, MoveTemp(Callback));
}

void FShineHttpClient::PostJson(const FString& Url, const FString& JsonBody, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers)
{
    FShineHttpRequest Request;
    Request.Url = Url;
    Request.Verb = TEXT("POST");
    Request.Content = JsonBody;
    Request.ContentType = TEXT("application/json");
    Request.Headers = Headers;
    Send(Request, MoveTemp(Callback));
}

void FShineHttpClient::PostBinary(const FString& Url, const TArray<uint8>& Data, const FString& ContentType, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers)
{
    FShineHttpRequest Request;
    Request.Url = Url;
    Request.Verb = TEXT("POST");
    Request.ContentBytes = Data;
    Request.ContentType = ContentType.IsEmpty() ? TEXT("application/octet-stream") : ContentType;
    Request.Headers = Headers;
    Send(Request, MoveTemp(Callback));
}

void FShineHttpClient::PostMultipart(const FString& Url, const TArray<FShineHttpMultipartField>& Fields, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers)
{
    FShineHttpRequest Request;
    Request.Url = Url;
    Request.Verb = TEXT("POST");
    Request.MultipartFields = Fields;
    Request.Headers = Headers;
    Send(Request, MoveTemp(Callback));
}

void FShineHttpClient::GetJsonObject(const FString& Url, FOnJsonObjectResponse&& Callback, const TMap<FString, FString>& Headers)
{
    TSharedRef<FOnJsonObjectResponse> SharedCallback = MakeShared<FOnJsonObjectResponse>(MoveTemp(Callback));

    Get(Url, [SharedCallback](FShineHttpResponse&& Response)
    {
        TSharedPtr<FJsonObject> JsonObject;

        if (Response.bSuccess)
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
            if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
            {
                Response.bSuccess = false;
                Response.ErrorCode = EShineHttpErrorCode::JsonParse;
                Response.ErrorMessage = TEXT("响应 JSON Object 解析失败。");
            }
        }

        (*SharedCallback)(MoveTemp(Response), JsonObject);
    }, Headers);
}

void FShineHttpClient::GetJsonArray(const FString& Url, FOnJsonArrayResponse&& Callback, const TMap<FString, FString>& Headers)
{
    TSharedRef<FOnJsonArrayResponse> SharedCallback = MakeShared<FOnJsonArrayResponse>(MoveTemp(Callback));

    Get(Url, [SharedCallback](FShineHttpResponse&& Response)
    {
        TArray<TSharedPtr<FJsonValue>> JsonArray;

        if (Response.bSuccess)
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
            if (!FJsonSerializer::Deserialize(Reader, JsonArray))
            {
                Response.bSuccess = false;
                Response.ErrorCode = EShineHttpErrorCode::JsonParse;
                Response.ErrorMessage = TEXT("响应 JSON Array 解析失败。");
            }
        }

        (*SharedCallback)(MoveTemp(Response), MoveTemp(JsonArray));
    }, Headers);
}

void FShineHttpClient::PostJsonObject(const FString& Url, const FString& JsonBody, FOnJsonObjectResponse&& Callback, const TMap<FString, FString>& Headers)
{
    TSharedRef<FOnJsonObjectResponse> SharedCallback = MakeShared<FOnJsonObjectResponse>(MoveTemp(Callback));

    PostJson(Url, JsonBody, [SharedCallback](FShineHttpResponse&& Response)
    {
        TSharedPtr<FJsonObject> JsonObject;

        if (Response.bSuccess && !Response.Content.IsEmpty())
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
            if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
            {
                Response.bSuccess = false;
                Response.ErrorCode = EShineHttpErrorCode::JsonParse;
                Response.ErrorMessage = TEXT("响应 JSON Object 解析失败。");
            }
        }

        (*SharedCallback)(MoveTemp(Response), JsonObject);
    }, Headers);
}

void FShineHttpClient::GetBinary(const FString& Url, FOnBinaryResponse&& Callback, const TMap<FString, FString>& Headers)
{
    TSharedRef<FOnBinaryResponse> SharedCallback = MakeShared<FOnBinaryResponse>(MoveTemp(Callback));

    Get(Url, [SharedCallback](FShineHttpResponse&& Response)
    {
        const TArray<uint8> BinaryContent = Response.ContentBytes;
        (*SharedCallback)(MoveTemp(Response), BinaryContent);
    }, Headers);
}
