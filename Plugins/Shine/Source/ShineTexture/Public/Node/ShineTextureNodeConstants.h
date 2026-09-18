#pragma once

#include "CoreMinimal.h"

#define SHINETEXTURE_DEFAULT_INTERNAL_PREVIEW_RESOLUTION 512
#define SHINETEXTURE_MAX_OUTPUT_PREVIEW_RESOLUTION 4096

namespace ShineTextureNodeConstants
{
    inline constexpr int32 DefaultInternalPreviewResolution = SHINETEXTURE_DEFAULT_INTERNAL_PREVIEW_RESOLUTION;
    inline constexpr int32 MaxOutputPreviewResolution = SHINETEXTURE_MAX_OUTPUT_PREVIEW_RESOLUTION;

    inline FIntPoint DefaultInlinePreviewSize()
    {
        return FIntPoint(DefaultInternalPreviewResolution, DefaultInternalPreviewResolution);
    }
}
