#include "Paint/ShineAIPaintSession.h"

#include "Asset/ShineAIPaintAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Paint/ShineAIPaintService.h"
#include "Paint/ShineAIPaintTextureUtils.h"

namespace
{
    /** 按 alpha 把 Source 混到 Target 上（alpha=1 时为完全替换）。 */
    FColor BlendColors(const FColor& Source, const FColor& Target, float Alpha)
    {
        const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
        const float Inv = 1.0f - Clamped;
        return FColor(
            static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Source.R * Clamped + Target.R * Inv), 0, 255)),
            static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Source.G * Clamped + Target.G * Inv), 0, 255)),
            static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Source.B * Clamped + Target.B * Inv), 0, 255)),
            255);
    }

    /** 只比 RGB：底色写入时 alpha 会被强制成 255，比 alpha 会误报"变了"。 */
    bool SameRgb(const FColor& A, const FColor& B)
    {
        return A.R == B.R && A.G == B.G && A.B == B.B;
    }

    FString MakeTargetLabel(const UObject* MeshAsset, const UObject* Owner)
    {
        const FString MeshName = MeshAsset ? MeshAsset->GetName() : TEXT("Mesh");
        return Owner ? FString::Printf(TEXT("%s  @ %s"), *MeshName, *Owner->GetName()) : MeshName;
    }

    /** 画布/预览用的临时显示贴图：可被 UpdateTextureRegions 原地刷新。 */
    UTexture2D* CreateDisplayTexture(int32 Width, int32 Height, bool bSRGB)
    {
        if (Width <= 0 || Height <= 0)
        {
            return nullptr;
        }

        UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (Texture)
        {
            // NeverStream=false 的贴图走 UpdateTextureRegions 会被引擎直接跳过，必须设上。
            Texture->SRGB = bSRGB;
            Texture->NeverStream = true;
            Texture->Filter = TF_Bilinear;
            Texture->AddressX = TA_Clamp;
            Texture->AddressY = TA_Clamp;
            Texture->UpdateResource();
        }

        return Texture;
    }
}

FShineAIPaintSession::FShineAIPaintSession(UShineAIPaintAsset* InAsset)
    : Asset(InAsset)
{
}

TSharedPtr<FShineAIPaintSession> FShineAIPaintSession::CreateForAsset(UShineAIPaintAsset* InAsset)
{
    if (!InAsset)
    {
        return nullptr;
    }

    return MakeShared<FShineAIPaintSession>(InAsset);
}

UTexture2D* FShineAIPaintSession::GetTargetTexture() const
{
    const UShineAIPaintAsset* AssetPtr = Asset.Get();
    return AssetPtr ? AssetPtr->TargetTexture : nullptr;
}

// ---------------------------------------------------------------------------
// 与资产同步
// ---------------------------------------------------------------------------

void FShineAIPaintSession::LoadFromAsset()
{
    UShineAIPaintAsset* AssetPtr = Asset.Get();
    if (!AssetPtr)
    {
        return;
    }

    Settings = AssetPtr->Settings;

    // ---- 要画的贴图：从它的源数据读出像素 ----
    TArray<FColor> Pixels;
    int32 TextureWidth = 0;
    int32 TextureHeight = 0;
    FString ReadError;
    const bool bReadSucceeded = ShineAIPaintTextureUtils::ReadTexturePixels(
        AssetPtr->TargetTexture,
        Pixels,
        TextureWidth,
        TextureHeight,
        ReadError);

    Width = bReadSucceeded ? TextureWidth : 0;
    Height = bReadSucceeded ? TextureHeight : 0;
    TargetTextureIssue = bReadSucceeded ? FString() : ReadError;

    // 决定"涂改能不能实时进贴图"：只有未压缩 BGRA8 平台格式才支持原地更新。
    if (bReadSucceeded)
    {
        FString InPlaceReason;
        bCanUpdateTargetInPlace = ShineAIPaintTextureUtils::SupportsInPlaceUpdate(AssetPtr->TargetTexture, InPlaceReason);
        TargetUpdateHint = bCanUpdateTargetInPlace
            ? TEXT("涂改会实时写进贴图本身（未压缩，不重建）")
            : InPlaceReason;
    }
    else
    {
        bCanUpdateTargetInPlace = false;
        TargetUpdateHint.Reset();
    }

    // 先按新尺寸把缓冲建好（内部会清零），再把读出来的像素放回去。
    RebuildPixelBuffers();

    if (bReadSucceeded && Pixels.Num() == Width * Height)
    {
        BasePixels = MoveTemp(Pixels);
        InitialPixels = BasePixels;
    }

    // ---- 遮罩 ----
    AssetPtr->EnsureMaskBuffer(Width, Height);
    MaskPixels = AssetPtr->MaskPixels;
    if (MaskPixels.Num() != Width * Height)
    {
        MaskPixels.Reset();
        MaskPixels.SetNumZeroed(FMath::Max(Width * Height, 0));
    }

    MaskPaintedCount = 0;
    for (const uint8 MaskValue : MaskPixels)
    {
        if (MaskValue > 0)
        {
            ++MaskPaintedCount;
        }
    }

    BaseTexture.Reset();
    MaskTexture.Reset();
    bBaseTextureDirty = true;
    bMaskTextureDirty = true;

    // ---- 预览用网格体 ----
    Targets.Reset();
    for (const TSoftObjectPtr<UObject>& SoftMesh : AssetPtr->MeshAssets)
    {
        if (UObject* Mesh = SoftMesh.Get())
        {
            AddTargetUnique(Mesh);
        }
    }

    OnStructureChanged.Broadcast();
    OnPixelsChanged.Broadcast();
}

void FShineAIPaintSession::SaveToAsset()
{
    UShineAIPaintAsset* AssetPtr = Asset.Get();
    if (!AssetPtr)
    {
        return;
    }

    AssetPtr->Settings = Settings;

    // ---- 像素写回那张贴图本身 ----
    if (AssetPtr->TargetTexture && Width > 0 && Height > 0 && BasePixels.Num() == Width * Height)
    {
        FString WriteError;
        if (!ShineAIPaintTextureUtils::WriteTexturePixels(AssetPtr->TargetTexture, BasePixels, Width, Height, WriteError))
        {
            LastStatus = FString::Printf(TEXT("写回贴图失败：%s"), *WriteError);
        }

        // 重建完平台格式可能变了，重新判断一次能不能原地更新。
        FString InPlaceReason;
        bCanUpdateTargetInPlace = ShineAIPaintTextureUtils::SupportsInPlaceUpdate(AssetPtr->TargetTexture, InPlaceReason);
        TargetUpdateHint = bCanUpdateTargetInPlace
            ? TEXT("涂改会实时写进贴图本身（未压缩，不重建）")
            : InPlaceReason;
    }

    // ---- 遮罩 ----
    AssetPtr->EnsureMaskBuffer(Width, Height);
    if (AssetPtr->MaskPixels.Num() == MaskPixels.Num())
    {
        FMemory::Memcpy(AssetPtr->MaskPixels.GetData(), MaskPixels.GetData(), static_cast<SIZE_T>(MaskPixels.Num()) * sizeof(uint8));
    }

    SyncTargetsToAsset();
}

void FShineAIPaintSession::SyncTargetsToAsset()
{
    UShineAIPaintAsset* AssetPtr = Asset.Get();
    if (!AssetPtr)
    {
        return;
    }

    AssetPtr->MeshAssets.Reset();
    for (const FShineAIPaintTarget& Target : Targets)
    {
        if (UObject* Mesh = Target.MeshAsset.Get())
        {
            AssetPtr->MeshAssets.Add(TSoftObjectPtr<UObject>(Mesh));
        }
    }

    AssetPtr->MarkPackageDirty();
}

// ---------------------------------------------------------------------------
// 目标网格体
// ---------------------------------------------------------------------------

bool FShineAIPaintSession::IsSupportedMeshAsset(const UObject* Object)
{
    return Object && (Object->IsA<UStaticMesh>() || Object->IsA<USkeletalMesh>());
}

void FShineAIPaintSession::AddTargetUnique(UObject* MeshAsset)
{
    if (!MeshAsset)
    {
        return;
    }

    const bool bAlreadyThere = Targets.ContainsByPredicate(
        [MeshAsset](const FShineAIPaintTarget& Existing)
        {
            return Existing.MeshAsset.Get() == MeshAsset;
        });

    if (bAlreadyThere)
    {
        return;
    }

    FShineAIPaintTarget Target;
    Target.MeshAsset = MeshAsset;
    Target.DisplayName = MeshAsset->GetName();
    Targets.Add(MoveTemp(Target));
}

int32 FShineAIPaintSession::AddTargetsFromSelection(bool bClearExisting)
{
    if (bClearExisting)
    {
        Targets.Reset();
    }

    const int32 PreviousCount = Targets.Num();

    if (GEditor)
    {
        // 1) 内容浏览器里直接选中的静态 / 骨骼网格资产
        if (USelection* SelectedObjects = GEditor->GetSelectedObjects())
        {
            for (int32 Index = 0; Index < SelectedObjects->Num(); ++Index)
            {
                UObject* Object = SelectedObjects->GetSelectedObject(Index);
                if (IsSupportedMeshAsset(Object))
                {
                    AddTargetUnique(Object);
                }
            }
        }

        // 2) 关卡里选中的 Actor 身上的网格组件（只取网格资产，不动关卡）
        for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
        {
            AActor* Actor = Cast<AActor>(*It);
            if (!Actor)
            {
                continue;
            }

            TArray<UStaticMeshComponent*> StaticComponents;
            Actor->GetComponents<UStaticMeshComponent>(StaticComponents);
            for (UStaticMeshComponent* Component : StaticComponents)
            {
                UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
                if (!Mesh)
                {
                    continue;
                }

                AddTargetUnique(Mesh);
                if (FShineAIPaintTarget* Added = Targets.FindByPredicate(
                    [Mesh](const FShineAIPaintTarget& Existing)
                    {
                        return Existing.MeshAsset.Get() == Mesh;
                    }))
                {
                    Added->DisplayName = MakeTargetLabel(Mesh, Actor);
                }
            }

            TArray<USkeletalMeshComponent*> SkeletalComponents;
            Actor->GetComponents<USkeletalMeshComponent>(SkeletalComponents);
            for (USkeletalMeshComponent* Component : SkeletalComponents)
            {
                USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
                if (!Mesh)
                {
                    continue;
                }

                AddTargetUnique(Mesh);
                if (FShineAIPaintTarget* Added = Targets.FindByPredicate(
                    [Mesh](const FShineAIPaintTarget& Existing)
                    {
                        return Existing.MeshAsset.Get() == Mesh;
                    }))
                {
                    Added->DisplayName = MakeTargetLabel(Mesh, Actor);
                }
            }
        }

        // 3) 细节面板里直接选中的网格组件
        for (FSelectionIterator It(GEditor->GetSelectedComponentIterator()); It; ++It)
        {
            UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(*It);
            if (!Component)
            {
                continue;
            }

            if (const UStaticMeshComponent* StaticComponent = Cast<UStaticMeshComponent>(Component))
            {
                AddTargetUnique(StaticComponent->GetStaticMesh());
            }
            else if (const USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(Component))
            {
                AddTargetUnique(SkeletalComponent->GetSkeletalMeshAsset());
            }
        }
    }

    const int32 AddedCount = Targets.Num() - PreviousCount;
    if (AddedCount > 0)
    {
        SyncTargetsToAsset();
        OnStructureChanged.Broadcast();
    }

    return AddedCount;
}

void FShineAIPaintSession::RemoveTarget(int32 Index)
{
    if (!Targets.IsValidIndex(Index))
    {
        return;
    }

    Targets.RemoveAt(Index);
    SyncTargetsToAsset();
    OnStructureChanged.Broadcast();
}

void FShineAIPaintSession::ClearTargets()
{
    if (Targets.Num() == 0)
    {
        return;
    }

    Targets.Reset();
    SyncTargetsToAsset();
    OnStructureChanged.Broadcast();
}

FString FShineAIPaintSession::BuildTargetsSummary() const
{
    if (Targets.Num() == 0)
    {
        return TEXT("（还没有指认网格体）");
    }

    TArray<FString> Lines;
    Lines.Reserve(Targets.Num());
    for (int32 Index = 0; Index < Targets.Num(); ++Index)
    {
        Lines.Add(FString::Printf(TEXT("%d. %s"), Index + 1, *Targets[Index].DisplayName));
    }

    return FString::Join(Lines, TEXT("\n"));
}

void FShineAIPaintSession::SetBusy(bool bInBusy)
{
    if (bBusy == bInBusy)
    {
        return;
    }

    bBusy = bInBusy;
    OnStructureChanged.Broadcast();
}

void FShineAIPaintSession::SetLastStatus(const FString& InStatus)
{
    LastStatus = InStatus;
    OnStructureChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// 图层数据
// ---------------------------------------------------------------------------

void FShineAIPaintSession::RebuildPixelBuffers()
{
    const int32 PixelCount = FMath::Max(Width, 0) * FMath::Max(Height, 0);

    BasePixels.Reset();
    InitialPixels.Reset();
    MaskPixels.Reset();

    if (PixelCount > 0)
    {
        BasePixels.SetNumUninitialized(PixelCount);
        InitialPixels.SetNumUninitialized(PixelCount);
        MaskPixels.SetNumUninitialized(PixelCount);
        FMemory::Memzero(BasePixels.GetData(), static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
        FMemory::Memzero(InitialPixels.GetData(), static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
        FMemory::Memzero(MaskPixels.GetData(), static_cast<SIZE_T>(PixelCount) * sizeof(uint8));
    }

    MaskPaintedCount = 0;
    bBaseTextureDirty = true;
    bMaskTextureDirty = true;
}

void FShineAIPaintSession::ResetBasePixels(const FColor& FillColor)
{
    if (BasePixels.Num() != Width * Height || Width <= 0)
    {
        return;
    }

    const FColor Opaque(FillColor.R, FillColor.G, FillColor.B, 255);
    bool bChanged = false;
    for (int32 Index = 0; Index < BasePixels.Num(); ++Index)
    {
        if (!SameRgb(BasePixels[Index], Opaque))
        {
            bChanged = true;
        }

        BasePixels[Index] = Opaque;
        InitialPixels[Index] = Opaque;
    }

    if (bChanged)
    {
        MarkPixelsChanged();
    }

    bBaseTextureDirty = true;
    OnPixelsChanged.Broadcast();
}

void FShineAIPaintSession::ClearMask()
{
    if (MaskPaintedCount <= 0)
    {
        // 已经是空的：别刷贴图，也别推进版本号。
        return;
    }

    FMemory::Memzero(MaskPixels.GetData(), static_cast<SIZE_T>(MaskPixels.Num()) * sizeof(uint8));
    MaskPaintedCount = 0;
    bMaskTextureDirty = true;
    MarkPixelsChanged();
    OnPixelsChanged.Broadcast();
}

UTexture2D* FShineAIPaintSession::GetBaseTexture()
{
    if (Width <= 0 || Height <= 0)
    {
        return nullptr;
    }

    if (!BaseTexture.IsValid() || BaseTexture->GetSizeX() != Width || BaseTexture->GetSizeY() != Height)
    {
        BaseTexture = TStrongObjectPtr<UTexture2D>(CreateDisplayTexture(Width, Height, true));
        bBaseTextureDirty = true;
    }

    if (bBaseTextureDirty)
    {
        UploadBaseTexture();
    }

    return BaseTexture.Get();
}

UTexture2D* FShineAIPaintSession::GetMaskTexture()
{
    if (Width <= 0 || Height <= 0)
    {
        return nullptr;
    }

    if (!MaskTexture.IsValid() || MaskTexture->GetSizeX() != Width || MaskTexture->GetSizeY() != Height)
    {
        MaskTexture = TStrongObjectPtr<UTexture2D>(CreateDisplayTexture(Width, Height, false));
        bMaskTextureDirty = true;
    }

    if (bMaskTextureDirty)
    {
        UploadMaskTexture();
    }

    return MaskTexture.Get();
}

void FShineAIPaintSession::UploadBaseTexture()
{
    if (!BaseTexture.IsValid() || BasePixels.Num() != Width * Height || Width <= 0)
    {
        return;
    }

    const int32 NumBytes = Width * Height * sizeof(FColor);
    uint8* SourceData = static_cast<uint8*>(FMemory::Malloc(NumBytes));
    FMemory::Memcpy(SourceData, BasePixels.GetData(), NumBytes);

    // 引擎只保存 Regions 的裸指针（渲染命令是延迟执行的），
    // 所以 region 必须堆分配，并在 cleanup 回调里跟 SrcData 一起释放，否则就是野指针。
    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
    BaseTexture->UpdateTextureRegions(
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

    bBaseTextureDirty = false;
}

void FShineAIPaintSession::UploadMaskTexture()
{
    if (!MaskTexture.IsValid() || MaskPixels.Num() != Width * Height || Width <= 0)
    {
        return;
    }

    // 遮罩叠加层：颜色随语义变化，alpha 表示遮罩强度（没画的区域完全透明）。
    const bool bEditableMode = (Settings.MaskMode == EShineAIPaintMaskMode::Editable);
    const uint8 TintR = bEditableMode ? 40 : 255;
    const uint8 TintG = bEditableMode ? 220 : 110;
    const uint8 TintB = bEditableMode ? 70 : 20;

    const int32 NumBytes = Width * Height * sizeof(FColor);
    uint8* SourceData = static_cast<uint8*>(FMemory::Malloc(NumBytes));
    FColor* OverlayPixels = reinterpret_cast<FColor*>(SourceData);
    for (int32 Index = 0; Index < Width * Height; ++Index)
    {
        const uint8 MaskValue = MaskPixels[Index];
        OverlayPixels[Index] = FColor(
            TintR,
            TintG,
            TintB,
            static_cast<uint8>(FMath::Min(static_cast<int32>(MaskValue) * 0.62f, 255.0f)));
    }

    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
    MaskTexture->UpdateTextureRegions(
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

    bMaskTextureDirty = false;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

bool FShineAIPaintSession::StampBrush(const FVector2D& FromUV, const FVector2D& ToUV, float InBrushRadiusUV, const FLinearColor& Color, EShineAIPaintTool Tool)
{
    if (Width <= 0 || Height <= 0 || BasePixels.Num() != Width * Height)
    {
        return false;
    }

    // 只有真的写脏了像素才算"变了"：动态更新靠这个信号决定要不要发请求，
    // 所以在原值上"画了个一样的东西"（比如用同样的颜色又抹一遍）不该被算作改动。
    bool bChanged = false;

    const FVector2D TextureSize(static_cast<double>(Width), static_cast<double>(Height));
    const FVector2D From = FromUV * TextureSize;
    const FVector2D To = ToUV * TextureSize;

    const float RadiusPixels = FMath::Max(InBrushRadiusUV * Width, 1.0f);
    const float Distance = static_cast<float>(FVector2D::Distance(From, To));

    // 步距取半径的 0.35 倍：既够平滑，又不会因为一次大跨度拖动而算爆。
    const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Distance / FMath::Max(RadiusPixels * 0.35f, 1.0f)), 1, 128);

    const FColor PaintColor(
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Color.R * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Color.G * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Color.B * 255.0f), 0, 255)),
        255);

    for (int32 Step = 0; Step <= StepCount; ++Step)
    {
        const double StepAlpha = static_cast<double>(Step) / static_cast<double>(StepCount);
        const FVector2D Center = FMath::Lerp(From, To, StepAlpha);

        const int32 MinX = FMath::Max(0, FMath::FloorToInt(Center.X - RadiusPixels));
        const int32 MaxX = FMath::Min(Width - 1, FMath::CeilToInt(Center.X + RadiusPixels));
        const int32 MinY = FMath::Max(0, FMath::FloorToInt(Center.Y - RadiusPixels));
        const int32 MaxY = FMath::Min(Height - 1, FMath::CeilToInt(Center.Y + RadiusPixels));

        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                const double DeltaX = (X + 0.5) - Center.X;
                const double DeltaY = (Y + 0.5) - Center.Y;
                const double DistanceToCenter = FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
                if (DistanceToCenter > RadiusPixels)
                {
                    continue;
                }

                const float Linear = FMath::Clamp(1.0f - static_cast<float>(DistanceToCenter) / RadiusPixels, 0.0f, 1.0f);
                const float Falloff = Linear * Linear * (3.0f - 2.0f * Linear);

                const int32 Index = Y * Width + X;

                if (Tool == EShineAIPaintTool::Paint)
                {
                    const FColor Blended = BlendColors(PaintColor, BasePixels[Index], Falloff);
                    if (!SameRgb(Blended, BasePixels[Index]))
                    {
                        BasePixels[Index] = Blended;
                        bChanged = true;
                    }
                }
                else if (Tool == EShineAIPaintTool::Erase)
                {
                    const FColor Blended = BlendColors(InitialPixels[Index], BasePixels[Index], Falloff);
                    if (!SameRgb(Blended, BasePixels[Index]))
                    {
                        BasePixels[Index] = Blended;
                        bChanged = true;
                    }
                }
                else if (Tool == EShineAIPaintTool::MaskPaint)
                {
                    const uint8 Before = MaskPixels[Index];
                    const uint8 After = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(255.0f * Falloff + Before * (1.0f - Falloff)), 0, 255));
                    if (After != Before)
                    {
                        MaskPixels[Index] = After;
                        bChanged = true;
                        if (Before == 0)
                        {
                            ++MaskPaintedCount;
                        }
                    }
                }
                else
                {
                    const uint8 Before = MaskPixels[Index];
                    const uint8 After = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Before * (1.0f - Falloff)), 0, 255));
                    if (After != Before)
                    {
                        MaskPixels[Index] = After;
                        bChanged = true;
                        if (After == 0 && MaskPaintedCount > 0)
                        {
                            --MaskPaintedCount;
                        }
                    }
                }
            }
        }
    }

    return bChanged;
}

void FShineAIPaintSession::FinalizeStroke(EShineAIPaintTool Tool, bool bChanged)
{
    // 没改动就不刷贴图、也不推进版本号：拖动时这里每帧都会调，能省一点是一点。
    if (!bChanged)
    {
        return;
    }

    MarkPixelsChanged();

    if (Tool == EShineAIPaintTool::MaskPaint || Tool == EShineAIPaintTool::MaskErase)
    {
        bMaskTextureDirty = true;
    }
    else
    {
        bBaseTextureDirty = true;
    }

    OnPixelsChanged.Broadcast();
}

void FShineAIPaintSession::PaintStroke(const FVector2D& FromUV, const FVector2D& ToUV, float InBrushRadiusUV, const FLinearColor& Color, EShineAIPaintTool Tool)
{
    const bool bChanged = StampBrush(FromUV, ToUV, InBrushRadiusUV, Color, Tool);
    FinalizeStroke(Tool, bChanged);
}

void FShineAIPaintSession::PaintStrokeWrapped(const FVector2D& FromUV, const FVector2D& ToUV, float InBrushRadiusUV, const FLinearColor& Color, EShineAIPaintTool Tool)
{
    // UV 是 0..1 的平铺空间：笔迹越过边界时，相邻的贴图副本上也要留一份，
    // 否则跨 UV 缝 / 跨边界的笔画在模型另一侧会断掉。
    const double Radius = static_cast<double>(InBrushRadiusUV);
    const double MinU = FMath::Min(FromUV.X, ToUV.X);
    const double MaxU = FMath::Max(FromUV.X, ToUV.X);
    const double MinV = FMath::Min(FromUV.Y, ToUV.Y);
    const double MaxV = FMath::Max(FromUV.Y, ToUV.Y);

    TArray<double, TInlineAllocator<3>> OffsetsU;
    OffsetsU.Add(0.0);
    if (MinU - Radius < 0.0)
    {
        OffsetsU.Add(1.0);
    }
    if (MaxU + Radius > 1.0)
    {
        OffsetsU.Add(-1.0);
    }

    TArray<double, TInlineAllocator<3>> OffsetsV;
    OffsetsV.Add(0.0);
    if (MinV - Radius < 0.0)
    {
        OffsetsV.Add(1.0);
    }
    if (MaxV + Radius > 1.0)
    {
        OffsetsV.Add(-1.0);
    }

    bool bChanged = false;

    for (const double OffsetU : OffsetsU)
    {
        for (const double OffsetV : OffsetsV)
        {
            if (OffsetU == 0.0 && OffsetV == 0.0)
            {
                continue;
            }

            const FVector2D Offset(OffsetU, OffsetV);
            bChanged |= StampBrush(FromUV + Offset, ToUV + Offset, InBrushRadiusUV, Color, Tool);
        }
    }

    bChanged |= StampBrush(FromUV, ToUV, InBrushRadiusUV, Color, Tool);
    FinalizeStroke(Tool, bChanged);
}

void FShineAIPaintSession::FillAll(const FLinearColor& Color)
{
    const FColor FillColor(
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Color.R * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Color.G * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Color.B * 255.0f), 0, 255)),
        255);

    ResetBasePixels(FillColor);
}

void FShineAIPaintSession::BeginStroke()
{
    bStrokeActive = true;
}

void FShineAIPaintSession::EndStroke()
{
    if (!bStrokeActive)
    {
        return;
    }

    bStrokeActive = false;

    UTexture2D* Target = GetTargetTexture();
    if (Target && Width > 0 && Height > 0 && BasePixels.Num() == Width * Height)
    {
        FString ErrorMessage;
        if (ShineAIPaintTextureUtils::UpdateTextureInPlace(Target, BasePixels, Width, Height, ErrorMessage))
        {
            // 未压缩贴图：这一笔已经直接进了贴图本身（GPU + 源数据），没有重建。
            TargetUpdateHint = TEXT("涂改会实时写进贴图本身（未压缩，不重建）");
        }
        else
        {
            // 压缩格式：原地写不进去，等"写入贴图"/保存时走源数据 + 重建。
            bCanUpdateTargetInPlace = false;
            TargetUpdateHint = ErrorMessage;
        }
    }

    // 抬笔之后才考虑动态更新：一笔中间不发请求。
    MaybeRequestLiveAIUpdate();
}

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------

bool FShineAIPaintSession::ApplyAIPixels(const TArray<FColor>& InPixels, int32 InWidth, int32 InHeight, FString& OutErrorMessage)
{
    if (InWidth <= 0 || InHeight <= 0 || InPixels.Num() != InWidth * InHeight)
    {
        OutErrorMessage = TEXT("AI 返回的图片数据无效。");
        return false;
    }

    if (BasePixels.Num() != Width * Height || MaskPixels.Num() != Width * Height || Width <= 0)
    {
        OutErrorMessage = TEXT("画布图层数据不完整。");
        return false;
    }

    const bool bMaskEmpty = (MaskPaintedCount <= 0);
    const bool bProtectMask = (Settings.MaskMode == EShineAIPaintMaskMode::Protect);

    for (int32 Y = 0; Y < Height; ++Y)
    {
        const int32 SourceY = FMath::Clamp(
            static_cast<int32>((static_cast<int64>(Y) * InHeight) / Height),
            0,
            InHeight - 1);

        for (int32 X = 0; X < Width; ++X)
        {
            const int32 SourceX = FMath::Clamp(
                static_cast<int32>((static_cast<int64>(X) * InWidth) / Width),
                0,
                InWidth - 1);

            const int32 TargetIndex = Y * Width + X;

            float BlendAlpha = 1.0f;
            if (!bMaskEmpty)
            {
                const float MaskValue = MaskPixels[TargetIndex] / 255.0f;
                BlendAlpha = bProtectMask ? (1.0f - MaskValue) : MaskValue;
            }

            if (BlendAlpha <= 0.0f)
            {
                continue;
            }

            const FColor& SourceColor = InPixels[SourceY * InWidth + SourceX];
            BasePixels[TargetIndex] = BlendAlpha >= 1.0f
                ? FColor(SourceColor.R, SourceColor.G, SourceColor.B, 255)
                : BlendColors(SourceColor, BasePixels[TargetIndex], BlendAlpha);
        }
    }

    MarkPixelsChanged();
    bBaseTextureDirty = true;
    UploadBaseTexture();
    OnPixelsChanged.Broadcast();
    return true;
}

void FShineAIPaintSession::SetLiveAIUpdateEnabled(bool bEnabled)
{
    if (bLiveAIUpdate == bEnabled)
    {
        return;
    }

    bLiveAIUpdate = bEnabled;

    if (bEnabled)
    {
        // 以"当前画面"为基准：打开开关本身不发请求，只有之后再画才会触发。
        AccountedAIRevision = PaintRevision;
        bLiveRequestPending = false;
        SetLastStatus(TEXT("动态更新已开启：每画完一笔自动重跑 AI。"));
    }
    else
    {
        bLiveRequestPending = false;
        SetLastStatus(TEXT("动态更新已关闭。"));
    }
}

void FShineAIPaintSession::MaybeRequestLiveAIUpdate()
{
    if (!bLiveAIUpdate)
    {
        return;
    }

    // 1) 先把"画布变了"这件事记下来。
    //    同一个版本只记一次，避免鼠标一路拖过来每帧都排一次队。
    if (PaintRevision != AccountedAIRevision)
    {
        AccountedAIRevision = PaintRevision;
        bLiveRequestPending = true;
    }

    if (!bLiveRequestPending)
    {
        // 像素没变过：不发请求。这条是"性能"的关键。
        return;
    }

    if (bBusy || bStrokeActive)
    {
        // 有请求在跑（或者还在画）：先挂着，等当前这次结果回来会再泵一次。
        return;
    }

    if (!HasPaintTarget())
    {
        return;
    }

    // 2) 队列一个一个传：认领掉这次待发，然后发出去。
    bLiveRequestPending = false;

    FString ErrorMessage;
    if (!RequestAIUpdate(ErrorMessage))
    {
        // 条件不满足（比如贴图本身有问题）：别反复重试，把待发清掉。
        SetLastStatus(ErrorMessage);
    }
}

FShineAIPaintSettings FShineAIPaintSession::BuildEffectiveSettings() const
{
    FShineAIPaintSettings Effective = Settings;

    if (const UShineAIPaintAsset* AssetPtr = Asset.Get())
    {
        Effective.ServiceUrl = AssetPtr->ResolveServiceUrl();
    }

    return Effective;
}

bool FShineAIPaintSession::RequestAIUpdate(FString& OutErrorMessage)
{
    if (bBusy)
    {
        OutErrorMessage = TEXT("正在等待上一次 AI 结果。");
        return false;
    }

    if (!HasPaintTarget())
    {
        OutErrorMessage = TargetTextureIssue.IsEmpty()
            ? TEXT("还没有可画的贴图：请先新建一张，或从材质的贴图参数里指认一张。")
            : TargetTextureIssue;
        return false;
    }

    SetBusy(true);
    LiveProgressText = TEXT("正在请求 AI…");
    SetLastStatus(TEXT("正在请求 AI…"));

    TWeakPtr<FShineAIPaintSession> WeakSelf = AsShared();
    FShineAIPaintService::RequestTexture(
        BasePixels,
        Width,
        Height,
        MaskPixels,
        BuildEffectiveSettings(),
        // 进度走 WebSocket 实时推回来；只写字段不广播，UI 那边是拉取式的。
        [WeakSelf](const FString& ProgressText)
        {
            if (const TSharedPtr<FShineAIPaintSession> Pinned = WeakSelf.Pin())
            {
                Pinned->LiveProgressText = ProgressText;
            }
        },
        [WeakSelf](FShineAIPaintResult&& Result)
        {
            const TSharedPtr<FShineAIPaintSession> Pinned = WeakSelf.Pin();
            if (!Pinned.IsValid())
            {
                return;
            }

            Pinned->SetBusy(false);
            Pinned->LiveProgressText.Reset();

            // 结果照旧应用 —— 即使等待期间用户又画了新笔迹。
            // 应用完再补发一次（有待发标记的话），画面会先跳到这一版，再跳到最新那版。
            if (Result.bSuccess)
            {
                FString ApplyError;
                if (Pinned->ApplyAIPixels(Result.Pixels, Result.Width, Result.Height, ApplyError))
                {
                    // 结果直接写回那张贴图，省得还要手动点一次。
                    Pinned->SaveToAsset();
                    Pinned->SetLastStatus(TEXT("AI 更新完成，已写入贴图（Ctrl+S 落盘）"));
                }
                else
                {
                    Pinned->SetLastStatus(FString::Printf(TEXT("应用结果失败：%s"), *ApplyError));
                }
            }
            else
            {
                Pinned->SetLastStatus(FString::Printf(TEXT("AI 失败：%s"), *Result.ErrorMessage));
            }

            // 队列一个一个传：有"待发"就再发一次，没有就停在这里。
            Pinned->MaybeRequestLiveAIUpdate();
        });

    return true;
}
