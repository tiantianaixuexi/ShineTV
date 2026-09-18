#pragma once

#include "CoreMinimal.h"

/**
 * 阻塞式 HTTP 客户端。
 *
 * 存在的理由：MCP 工具必须"调用完就有结果"，而 UE 自带的 FHttpModule 是异步回调、
 * 且回调也在游戏线程，同步等待会死锁。这里直接用 Socket 做同步请求。
 * 单次请求耗时通常在毫秒级（本机服务），阻塞游戏线程是可接受的。
 */
struct FShineMCPHttpResult
{
    bool bSuccess = false;
    int32 StatusCode = 0;
    FString ContentType;
    FString Body;
    TArray<uint8> RawBody;
    FString Error;
};

class FShineMCPHttpClient
{
public:
    struct FMultipartFile
    {
        FString FieldName;
        FString FileName;
        FString ContentType;
        TArray<uint8> Data;
    };

    /** 发起一次请求。Url 形如 http://127.0.0.1:8188/api/prompt */
    static FShineMCPHttpResult Request(
        const FString& Verb,
        const FString& Url,
        const TArray<FString>& ExtraHeaders = TArray<FString>(),
        const TArray<uint8>& Body = TArray<uint8>(),
        const FString& ContentType = FString(),
        int32 TimeoutSeconds = 30);

    /** multipart/form-data 上传（ComfyUI 的 /upload/image 需要）。 */
    static FShineMCPHttpResult UploadMultipart(
        const FString& Url,
        const TArray<TPair<FString, FString>>& TextFields,
        const TArray<FMultipartFile>& Files,
        int32 TimeoutSeconds = 60);

    /** 解析 URL 为 host / port / path（path 含 query）。 */
    static bool ParseUrl(const FString& Url, FString& OutHost, int32& OutPort, FString& OutPath);

    /** 读取本地文件为字节数组。 */
    static bool LoadFileBytes(const FString& FilePath, TArray<uint8>& OutBytes, FString& OutError);

    /** 写入本地文件。 */
    static bool SaveFileBytes(const FString& FilePath, const TArray<uint8>& Bytes, FString& OutError);

    /** 从 URL 里取 Content-Disposition 的 filename（调试用）。 */
    static FString ExtractFilenameFromUrl(const FString& Url);
};
