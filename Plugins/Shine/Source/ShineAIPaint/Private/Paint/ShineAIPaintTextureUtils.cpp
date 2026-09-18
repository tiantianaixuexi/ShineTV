#include "Paint/ShineAIPaintTextureUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    /** 把写入限制在一张 2D、单 mip 的 BGRA8 源贴图上，别把别人的贴图结构改坏。 */
    constexpr int32 GMaxPaintableSize = 4096;
}

namespace ShineAIPaintTextureUtils
{
    bool IsPaintable(UTexture2D* Texture, FString& OutReason)
    {
        if (!Texture)
        {
            OutReason = TEXT("没有指定贴图。");
            return false;
        }

        const int32 SizeX = Texture->GetSizeX();
        const int32 SizeY = Texture->GetSizeY();
        if (SizeX <= 0 || SizeY <= 0)
        {
            OutReason = TEXT("贴图尺寸无效。");
            return false;
        }

        if (SizeX > GMaxPaintableSize || SizeY > GMaxPaintableSize)
        {
            OutReason = FString::Printf(TEXT("贴图 %dx%d 超过可绘制上限 %d。"), SizeX, SizeY, GMaxPaintableSize);
            return false;
        }

        if (!Texture->Source.IsValid())
        {
            OutReason = TEXT("这张贴图没有可编辑的源数据（可能是运行时生成的贴图）。");
            return false;
        }

        if (FTextureSource::IsHDR(Texture->Source.GetFormat(0)))
        {
            OutReason = TEXT("HDR 贴图（RGBA16F / RGBE 等）暂不支持直接绘制。");
            return false;
        }

        return true;
    }

    UTexture2D* CreatePaintableTextureAsset(
        const FString& PackagePath,
        const FString& BaseName,
        int32 Size,
        const FColor& FillColor,
        FString& OutErrorMessage)
    {
        const int32 ClampedSize = FMath::Clamp(Size > 0 ? Size : 1024, 64, GMaxPaintableSize);
        const FString SanitizedBase = BaseName.IsEmpty() ? TEXT("ShinePaintedTexture") : BaseName;
        const FString SafePackagePath = PackagePath.IsEmpty() ? TEXT("/Game/ShineAIPaint") : PackagePath;

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        IAssetTools& AssetTools = AssetToolsModule.Get();

        FString UniquePackageName;
        FString UniqueAssetName;
        AssetTools.CreateUniqueAssetName(SafePackagePath / SanitizedBase, TEXT(""), UniquePackageName, UniqueAssetName);

        UPackage* Package = CreatePackage(*UniquePackageName);
        if (!Package)
        {
            OutErrorMessage = FString::Printf(TEXT("创建包失败：%s"), *UniquePackageName);
            return nullptr;
        }

        UTexture2D* Texture = NewObject<UTexture2D>(Package, *UniqueAssetName, RF_Public | RF_Standalone | RF_Transactional);
        if (!Texture)
        {
            OutErrorMessage = TEXT("创建贴图对象失败。");
            return nullptr;
        }

        FImage Image;
        Image.Init(ClampedSize, ClampedSize, 1, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

        FColor* Pixels = reinterpret_cast<FColor*>(Image.RawData.GetData());
        const int64 PixelCount = Image.RawData.Num() / static_cast<int64>(sizeof(FColor));
        const FColor Opaque(FillColor.R, FillColor.G, FillColor.B, 255);
        for (int64 Index = 0; Index < PixelCount; ++Index)
        {
            Pixels[Index] = Opaque;
        }

        Texture->PreEditChange(nullptr);
        Texture->Source.Init(Image);
        Texture->SRGB = true;
        // TC_VectorDisplacementmap 编出来的平台格式是未压缩 NameBGRA8（见 Texture.cpp 的
        // GetTextureFormatName），只有这种格式才能用 UpdateTextureRegions 原地改像素。
        Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
        Texture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
        Texture->LODGroup = TEXTUREGROUP_World;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Wrap;
        Texture->AddressY = TA_Wrap;
        Texture->NeverStream = true;
        Texture->UpdateResource();
        Texture->PostEditChange();
        Texture->MarkPackageDirty();

        FAssetRegistryModule::AssetCreated(Texture);

        // 立刻落盘，省得用户还要手动存一遍才在内容浏览器里看到。
        const FString PackageFileName = FPackageName::LongPackageNameToFilename(
            Package->GetName(),
            FPackageName::GetAssetPackageExtension());

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        UPackage::SavePackage(Package, Texture, *PackageFileName, SaveArgs);

        return Texture;
    }

    bool ReadTexturePixels(
        UTexture2D* Texture,
        TArray<FColor>& OutPixels,
        int32& OutWidth,
        int32& OutHeight,
        FString& OutErrorMessage)
    {
        if (!IsPaintable(Texture, OutErrorMessage))
        {
            return false;
        }

        FImage SourceImage;
        if (!Texture->Source.GetMipImage(SourceImage, 0))
        {
            OutErrorMessage = TEXT("读取贴图源数据失败。");
            return false;
        }

        if (SourceImage.SizeX <= 0 || SourceImage.SizeY <= 0)
        {
            OutErrorMessage = TEXT("贴图源数据尺寸无效。");
            return false;
        }

        // 统一转成 BGRA8，后续绘制只认这一种格式。
        if (SourceImage.Format == ERawImageFormat::BGRA8)
        {
            const TArrayView64<const FColor> BgraPixels = SourceImage.AsBGRA8();
            OutPixels.SetNumUninitialized(static_cast<int32>(BgraPixels.Num()));
            FMemory::Memcpy(OutPixels.GetData(), BgraPixels.GetData(), static_cast<SIZE_T>(BgraPixels.Num()) * sizeof(FColor));
        }
        else
        {
            FImage ConvertedImage;
            SourceImage.CopyTo(ConvertedImage, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
            const TArrayView64<const FColor> BgraPixels = ConvertedImage.AsBGRA8();
            OutPixels.SetNumUninitialized(static_cast<int32>(BgraPixels.Num()));
            FMemory::Memcpy(OutPixels.GetData(), BgraPixels.GetData(), static_cast<SIZE_T>(BgraPixels.Num()) * sizeof(FColor));
        }

        OutWidth = SourceImage.SizeX;
        OutHeight = SourceImage.SizeY;
        return OutPixels.Num() == OutWidth * OutHeight;
    }

    bool SupportsInPlaceUpdate(UTexture2D* Texture, FString& OutReason)
    {
        if (!Texture)
        {
            OutReason = TEXT("没有指定贴图。");
            return false;
        }

        const FTexturePlatformData* PlatformData = Texture->GetPlatformData();
        if (!PlatformData || PlatformData->Mips.Num() == 0)
        {
            OutReason = TEXT("贴图还没有构建好的平台数据。");
            return false;
        }

        const EPixelFormat Format = PlatformData->PixelFormat;
        if (Format != PF_B8G8R8A8 && Format != PF_R8G8B8A8)
        {
            OutReason = FString::Printf(
                TEXT("贴图平台格式是 %s（压缩格式），只能改源数据后重建；想原地更新请用“新建一张贴图”（未压缩 BGRA8）。"),
                GPixelFormats[Format].Name);
            return false;
        }

        if (PlatformData->Mips[0].SizeX != Texture->GetSizeX() || PlatformData->Mips[0].SizeY != Texture->GetSizeY())
        {
            OutReason = TEXT("贴图 mip 尺寸跟贴图尺寸不一致。");
            return false;
        }

        return true;
    }

    bool SyncTextureSource(
        UTexture2D* Texture,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage)
    {
        if (!Texture)
        {
            OutErrorMessage = TEXT("没有指定贴图。");
            return false;
        }

        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            OutErrorMessage = TEXT("要写入的像素数据尺寸不匹配。");
            return false;
        }

        const SIZE_T NumBytes = static_cast<SIZE_T>(Pixels.Num()) * sizeof(FColor);
        FTextureSource& Source = Texture->Source;

        const bool bStructureMatches = Source.IsValid()
            && Source.GetSizeX() == Width
            && Source.GetSizeY() == Height
            && Source.GetNumMips() == 1
            && Source.GetFormat(0) == TSF_BGRA8;

        if (!bStructureMatches)
        {
            // 结构不一致（首次、或源是别的格式/带 mip 链）：用 FImage 重建源。
            // Init 会按数据哈希换 GUID，所以 DDC 会失效并在下次重建。
            FImage Image;
            Image.Init(Width, Height, 1, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
            FMemory::Memcpy(Image.RawData.GetData(), Pixels.GetData(), NumBytes);
            Source.Init(Image);
            return true;
        }

        // 结构一致：直接锁 mip 写，最便宜（LockMip 会自动换 GUID）。
        uint8* MipData = Source.LockMip(0);
        if (!MipData)
        {
            OutErrorMessage = TEXT("锁定贴图源数据失败。");
            return false;
        }

        FMemory::Memcpy(MipData, Pixels.GetData(), NumBytes);
        Source.UnlockMip(0);
        return true;
    }

    bool UpdateTextureInPlace(
        UTexture2D* Texture,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage)
    {
        if (!SupportsInPlaceUpdate(Texture, OutErrorMessage))
        {
            return false;
        }

        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            OutErrorMessage = TEXT("要写入的像素数据尺寸不匹配。");
            return false;
        }

        // 流式贴图走 UpdateTextureRegions 会被引擎静默跳过，所以先临时关掉流式。
        // bTemporarilyDisableStreaming 是 transient 的，保存前引擎会自己清掉。
        Texture->TemporarilyDisableStreaming();

        const int32 NumBytes = Width * Height * sizeof(FColor);
        uint8* SourceData = static_cast<uint8*>(FMemory::Malloc(NumBytes));
        FMemory::Memcpy(SourceData, Pixels.GetData(), NumBytes);

        // 引擎只保存 Regions 裸指针（渲染命令延迟执行），region 必须堆分配并在 cleanup 里释放。
        FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
        Texture->UpdateTextureRegions(
            0,
            1,
            Region,
            Width * sizeof(FColor),
            sizeof(FColor),
            SourceData,
            [](uint8* Data, const FUpdateTextureRegion2D* Regions)
            {
                FMemory::Free(Data);
                delete Regions;
            });

        // CPU 源数据也跟上（便宜，不触发重建），这样保存下来的 .uasset 内容是新的。
        FString SyncError;
        SyncTextureSource(Texture, Pixels, Width, Height, SyncError);

        Texture->MarkPackageDirty();
        return true;
    }

    bool WriteTexturePixels(
        UTexture2D* Texture,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage)
    {
        if (!Texture)
        {
            OutErrorMessage = TEXT("没有指定贴图。");
            return false;
        }

        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            OutErrorMessage = TEXT("要写入的像素数据尺寸不匹配。");
            return false;
        }

        Texture->PreEditChange(nullptr);

        if (!SyncTextureSource(Texture, Pixels, Width, Height, OutErrorMessage))
        {
            return false;
        }

        // 让平台数据跟源数据对齐：未压缩贴图这一步只是搬一次 mip，压缩贴图才会真的重新压一次。
        Texture->UpdateResource();
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        return true;
    }

    void CollectMaterialTextures(
        UMaterialInterface* Material,
        TArray<FName>& OutParameterNames,
        TArray<UTexture2D*>& OutTextures)
    {
        OutParameterNames.Reset();
        OutTextures.Reset();

        if (!Material)
        {
            return;
        }

        TMap<FMaterialParameterInfo, FMaterialParameterMetadata> Parameters;
        Material->GetAllParametersOfType(EMaterialParameterType::Texture, Parameters);

        for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : Parameters)
        {
            UTexture2D* Texture2D = Cast<UTexture2D>(Pair.Value.Value.Texture);
            if (!Texture2D)
            {
                continue;
            }

            if (OutTextures.Contains(Texture2D))
            {
                continue;
            }

            OutParameterNames.Add(Pair.Key.Name);
            OutTextures.Add(Texture2D);
        }
    }
}
