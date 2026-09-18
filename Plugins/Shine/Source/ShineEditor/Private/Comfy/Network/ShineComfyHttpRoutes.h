#pragma once

#include "CoreMinimal.h"

namespace ShineComfyHttpRoutes
{
    FString NormalizeBaseUrl(const FString& BaseUrl);
    FString BuildApiUrl(const FString& BaseUrl, const FString& RelativePath);
}