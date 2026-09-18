#pragma once

#include "CoreMinimal.h"

struct FEdGraphEditAction;
class UShineTextureAsset;

namespace ShineTexturePreviewRenderer
{
    void RefreshAssetPreview(UShineTextureAsset* Asset, const FEdGraphEditAction* InAction = nullptr);
}