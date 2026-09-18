#pragma once

#include "CoreMinimal.h"

enum class EShineHttpErrorCode : uint8
{
    None,
    Network,
    HttpStatus,
    JsonParse,
    InvalidResponse
};

/**
 * multipart/form-data 里的一个字段。
 * FileName 为空时是普通文本字段（用 Value），非空时是文件字段（用 Data）。
 */
struct FShineHttpMultipartField
{
    FString Name;
    FString Value;
    FString FileName;
    FString ContentType;
    TArray<uint8> Data;

    static FShineHttpMultipartField Text(const FString& InName, const FString& InValue)
    {
        FShineHttpMultipartField Field;
        Field.Name = InName;
        Field.Value = InValue;
        return Field;
    }

    static FShineHttpMultipartField File(const FString& InName, const FString& InFileName, TArray<uint8> InData, const FString& InContentType = TEXT("application/octet-stream"))
    {
        FShineHttpMultipartField Field;
        Field.Name = InName;
        Field.FileName = InFileName;
        Field.ContentType = InContentType;
        Field.Data = MoveTemp(InData);
        return Field;
    }
};

struct FShineHttpRequest
{
    FString Url;
    FString Verb = TEXT("GET");
    TMap<FString, FString> Headers;
    FString Content;
    FString ContentType;

    /** 原始二进制 body；非空时优先于 Content 发送。 */
    TArray<uint8> ContentBytes;

    /** 非空时以 multipart/form-data 发送（自动生成 boundary，忽略 Content / ContentBytes）。 */
    TArray<FShineHttpMultipartField> MultipartFields;

    /**
     * 本次请求的超时（秒）。<= 0 表示不设置，用 HttpModule 的默认值（30 秒）。
     *
     * 需要它的场景只有一个：**服务端不忙但一时不回**。例如 ComfyUI 启动后拉
     * ComfyRegistry（ComfyUI-Manager）时会把事件循环占住几分钟，此时 `/prompt`
     * 已经收下了请求、响应却要等很久 —— 默认 30 秒会把一次正常提交判成"服务不可达"，
     * 而请求其实已经在队列里了，所以这批调用**不能靠重试兜底**（会排两份），
     * 只能把等待时间放长。
     */
    double TimeoutSeconds = 0.0;
};

struct FShineHttpResponse
{
    bool bSuccess = false;
    int32 StatusCode = 0;
    EShineHttpErrorCode ErrorCode = EShineHttpErrorCode::None;
    FString ErrorMessage;
    FString Content;
    TMap<FString, FString> Headers;

    /** 原始响应字节，用于图片等二进制响应。 */
    TArray<uint8> ContentBytes;
};
