#pragma once

#include "CoreMinimal.h"
#include "Http/ShineHttpTypes.h"

class FJsonObject;
class FJsonValue;

class SHINEHTTP_API FShineHttpClient
{
public:
    using FOnTextResponse = TFunction<void(FShineHttpResponse&&)>;
    using FOnJsonObjectResponse = TFunction<void(FShineHttpResponse&&, TSharedPtr<FJsonObject>)>;
    using FOnJsonArrayResponse = TFunction<void(FShineHttpResponse&&, TArray<TSharedPtr<FJsonValue>>&&)>;
    using FOnBinaryResponse = TFunction<void(FShineHttpResponse&&, const TArray<uint8>&)>;

    static void Send(const FShineHttpRequest& Request, FOnTextResponse&& Callback);
    static void Get(const FString& Url, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers = {});
    static void PostJson(const FString& Url, const FString& JsonBody, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers = {});
    static void GetJsonObject(const FString& Url, FOnJsonObjectResponse&& Callback, const TMap<FString, FString>& Headers = {});
    static void GetJsonArray(const FString& Url, FOnJsonArrayResponse&& Callback, const TMap<FString, FString>& Headers = {});
    static void PostJsonObject(const FString& Url, const FString& JsonBody, FOnJsonObjectResponse&& Callback, const TMap<FString, FString>& Headers = {});

    /** 发送原始二进制 body（上传 PNG 等）。 */
    static void PostBinary(const FString& Url, const TArray<uint8>& Data, const FString& ContentType, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers = {});

    /** 以 multipart/form-data 发送字段（表单文本 + 文件）。 */
    static void PostMultipart(const FString& Url, const TArray<FShineHttpMultipartField>& Fields, FOnTextResponse&& Callback, const TMap<FString, FString>& Headers = {});

    /** 下载二进制内容，回调里直接拿到原始字节（放进 Response.ContentBytes）。 */
    static void GetBinary(const FString& Url, FOnBinaryResponse&& Callback, const TMap<FString, FString>& Headers = {});
};
