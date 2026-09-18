#include "Asset/ShineAIPaintAsset.h"

#include "Asset/ShineComfyAsset.h"
#include "Engine/Texture2D.h"

UShineAIPaintAsset::UShineAIPaintAsset()
{
    EnsureMaskBuffer(0, 0);
}

FString UShineAIPaintAsset::ResolveServiceUrl() const
{
    if (bUseComfyAssetUrl)
    {
        if (const UShineComfyAsset* Comfy = ComfyAsset.Get())
        {
            if (!Comfy->ComfyBaseUrl.IsEmpty())
            {
                return Comfy->ComfyBaseUrl;
            }
        }
    }

    return Settings.ServiceUrl;
}

bool UShineAIPaintAsset::IsUsingComfyAssetUrl() const
{
    return bUseComfyAssetUrl && ComfyAsset.Get() != nullptr;
}

FIntPoint UShineAIPaintAsset::GetTargetSize() const
{
    if (!TargetTexture)
    {
        return FIntPoint::ZeroValue;
    }

    return FIntPoint(TargetTexture->GetSizeX(), TargetTexture->GetSizeY());
}

void UShineAIPaintAsset::EnsureMaskBuffer(int32 Width, int32 Height)
{
    const int32 PixelCount = FMath::Max(Width, 0) * FMath::Max(Height, 0);
    if (MaskPixels.Num() == PixelCount)
    {
        return;
    }

    MaskPixels.Reset();
    if (PixelCount > 0)
    {
        MaskPixels.SetNumZeroed(PixelCount);
    }
}

void UShineAIPaintAsset::ClearMaskPixels()
{
    FMemory::Memzero(MaskPixels.GetData(), static_cast<SIZE_T>(MaskPixels.Num()) * sizeof(uint8));
}

void UShineAIPaintAsset::PostLoad()
{
    Super::PostLoad();

    // 遮罩尺寸跟着贴图走；贴图换了尺寸就重来一份。
    const FIntPoint Size = GetTargetSize();
    EnsureMaskBuffer(Size.X, Size.Y);
}
