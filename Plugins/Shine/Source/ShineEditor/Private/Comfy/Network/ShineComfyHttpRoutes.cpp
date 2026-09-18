#include "Comfy/Network/ShineComfyHttpRoutes.h"

namespace ShineComfyHttpRoutes
{
    FString NormalizeBaseUrl(const FString& BaseUrl)
    {
        FString Result = BaseUrl.TrimStartAndEnd();
        while (Result.EndsWith(TEXT("/")))
        {
            Result.LeftChopInline(1, EAllowShrinking::No);
        }

        if (Result.IsEmpty())
        {
            Result = TEXT("http://127.0.0.1:8188");
        }

        return Result;
    }

    FString BuildApiUrl(const FString& BaseUrl, const FString& RelativePath)
    {
        return FString::Printf(TEXT("%s/api/%s"), *NormalizeBaseUrl(BaseUrl), *RelativePath);
    }
}