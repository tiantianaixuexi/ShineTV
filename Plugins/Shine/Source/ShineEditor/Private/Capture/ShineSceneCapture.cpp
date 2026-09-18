#include "Capture/ShineSceneCapture.h"

#include "Capture/ShineCapturePreview.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorViewportClient.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "ImageWriteBlueprintLibrary.h"
#include "LevelEditorViewport.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "SceneTypes.h"
#include "UnrealClient.h"

namespace
{
    enum class ECaptureChannel : uint8
    {
        Color,
        Depth,
        Normal
    };

    const TCHAR* GetChannelSuffix(ECaptureChannel Channel)
    {
        switch (Channel)
        {
        case ECaptureChannel::Color:  return TEXT("Color");
        case ECaptureChannel::Depth:  return TEXT("Depth");
        default:                      return TEXT("Normal");
        }
    }

    ESceneCaptureSource GetChannelCaptureSource(ECaptureChannel Channel)
    {
        switch (Channel)
        {
        case ECaptureChannel::Color:  return ESceneCaptureSource::SCS_FinalColorLDR;
        case ECaptureChannel::Depth:  return ESceneCaptureSource::SCS_SceneDepth;
        default:                      return ESceneCaptureSource::SCS_Normal;
        }
    }

    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    FString SanitizeFilePrefix(const FString& InPrefix)
    {
        FString Sanitized = InPrefix.IsEmpty() ? TEXT("SceneView") : InPrefix;
        const TCHAR* Invalid = TEXT("\\/:*?\"<>| ");
        for (const TCHAR* Ch = Invalid; *Ch; ++Ch)
        {
            const TCHAR Single[2] = { *Ch, TEXT('\0') };
            Sanitized.ReplaceInline(Single, TEXT("_"));
        }
        return Sanitized;
    }

    /** 通过反射改 FPostProcessSettings 的字段，避免不同引擎版本的成员类型差异。 */
    void SetPostProcessOverride(USceneCaptureComponent2D* CaptureComponent, const TCHAR* PropertyName, bool bBoolValue, float FloatValue)
    {
        if (!CaptureComponent)
        {
            return;
        }

        FPostProcessSettings& Settings = CaptureComponent->PostProcessSettings;
        UScriptStruct* SettingsStruct = FPostProcessSettings::StaticStruct();

        if (FBoolProperty* OverrideProperty = FindFProperty<FBoolProperty>(SettingsStruct, FName(*(FString(TEXT("bOverride_")) + PropertyName))))
        {
            OverrideProperty->SetPropertyValue_InContainer(&Settings, bBoolValue);
        }

        if (FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(SettingsStruct, FName(PropertyName)))
        {
            FloatProperty->SetPropertyValue_InContainer(&Settings, FloatValue);
        }
    }

    /** 锁定曝光，避免单帧捕获时自动曝光把画面拉爆或压黑。 */
    void LockExposure(USceneCaptureComponent2D* CaptureComponent)
    {
        SetPostProcessOverride(CaptureComponent, TEXT("AutoExposureMinBrightness"), true, 1.0f);
        SetPostProcessOverride(CaptureComponent, TEXT("AutoExposureMaxBrightness"), true, 1.0f);
        SetPostProcessOverride(CaptureComponent, TEXT("AutoExposureBias"), true, 0.0f);
        SetPostProcessOverride(CaptureComponent, TEXT("BloomIntensity"), true, 0.25f);
    }

    bool WriteGrayPng(const TArray<float>& Luminance, int32 Width, int32 Height, const FString& FilePath)
    {
        TArray<FColor> Colors;
        Colors.SetNumUninitialized(Width * Height);
        for (int32 Index = 0; Index < Colors.Num(); ++Index)
        {
            const uint8 Value = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Luminance[Index] * 255.0f), 0, 255));
            Colors[Index] = FColor(Value, Value, Value, 255);
        }

        TArray64<uint8> PngData;
        FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), PngData);
        return FFileHelper::SaveArrayToFile(PngData, *FilePath);
    }

    bool WriteRgbPng(const TArray<FLinearColor>& Pixels, int32 Width, int32 Height, const FString& FilePath)
    {
        TArray<FColor> Colors;
        Colors.SetNumUninitialized(Width * Height);
        for (int32 Index = 0; Index < Colors.Num(); ++Index)
        {
            const FLinearColor& Pixel = Pixels[Index];
            Colors[Index] = FColor(
                static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixel.R * 255.0f), 0, 255)),
                static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixel.G * 255.0f), 0, 255)),
                static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixel.B * 255.0f), 0, 255)),
                255);
        }

        TArray64<uint8> PngData;
        FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), PngData);
        return FFileHelper::SaveArrayToFile(PngData, *FilePath);
    }

    FShineCaptureFile& GetChannelFile(FShineCaptureResult& Result, ECaptureChannel Channel)
    {
        switch (Channel)
        {
        case ECaptureChannel::Color:  return Result.Color;
        case ECaptureChannel::Depth:  return Result.Depth;
        default:                      return Result.Normal;
        }
    }
}

FString FShineCaptureResult::ToLogText() const
{
    if (!bSuccess)
    {
        return FString::Printf(TEXT("捕获失败：%s"), *ErrorMessage);
    }

    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("目录：%s"), *Directory));
    if (!Color.Path.IsEmpty())
    {
        Lines.Add(FString::Printf(TEXT("  color  -> %s"), *Color.Path));
    }
    if (!Depth.Path.IsEmpty())
    {
        Lines.Add(FString::Printf(TEXT("  depth  -> %s"), *Depth.Path));
    }
    if (!Normal.Path.IsEmpty())
    {
        Lines.Add(FString::Printf(TEXT("  normal -> %s"), *Normal.Path));
    }
    return FString::Join(Lines, TEXT("\n"));
}

namespace ShineSceneCapture
{
    FLevelEditorViewportClient* GetActiveViewportClient()
    {
        if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
        {
            return GCurrentLevelEditingViewportClient;
        }

        // 鼠标不在视口上（例如刚点完面板按钮）时，GCurrent* 可能没被设置，
        // 这时退回到"最后一次键盘操作过的视口"，比随便挑第一个更接近用户在看的那一个。
        if (GLastKeyLevelEditingViewportClient && GLastKeyLevelEditingViewportClient->Viewport)
        {
            return GLastKeyLevelEditingViewportClient;
        }

        if (GEditor)
        {
            for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
            {
                if (Candidate && Candidate->Viewport && Candidate->IsPerspective())
                {
                    return Candidate;
                }
            }
        }

        return nullptr;
    }

    bool HasEditorViewport()
    {
        return GetActiveViewportClient() != nullptr;
    }

    FVector GetEditorViewportLocation()
    {
        if (FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient())
        {
            return ViewportClient->GetViewLocation();
        }

        return FVector(-900.0f, -1050.0f, 650.0f);
    }

    FRotator GetEditorViewportRotation()
    {
        if (FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient())
        {
            return ViewportClient->GetViewRotation();
        }

        return FRotator(-18.8f, 49.4f, 0.0f);
    }

    float GetEditorViewportFOV()
    {
        if (FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient())
        {
            const float FOV = ViewportClient->ViewFOV;
            if (FOV > 1.0f)
            {
                return FOV;
            }
        }

        return 55.0f;
    }

    FString BuildDefaultCaptureDirectory()
    {
        return FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("ShineCapture"),
            FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    }

    FString MakeUniquePrefix(const FString& Prefix)
    {
        // ComfyUI 的 LoadImage 用"文件名"当输入做缓存键：同名文件哪怕内容变了，
        // 也会直接复用上一次的执行结果。所以每次捕获都必须换一个文件名。
        const FDateTime Now = FDateTime::Now();
        return FString::Printf(
            TEXT("%s_%s_%03d"),
            *SanitizeFilePrefix(Prefix),
            *Now.ToString(TEXT("%Y%m%d_%H%M%S")),
            Now.GetMillisecond());
    }

    FShineCaptureResult CaptureFromEditorViewport(
        int32 Width,
        int32 Height,
        const FString& OutputDirectory,
        const FString& FilePrefix,
        bool bCaptureColor,
        bool bCaptureDepth,
        bool bCaptureNormal,
        bool bShowPreview)
    {
        FShineCaptureRequest Request;
        Request.Width = Width;
        Request.Height = Height;
        Request.OutputDirectory = OutputDirectory;
        Request.FilePrefix = FilePrefix;
        Request.bCaptureColor = bCaptureColor;
        Request.bCaptureDepth = bCaptureDepth;
        Request.bCaptureNormal = bCaptureNormal;
        Request.bShowPreview = bShowPreview;
        Request.Location = GetEditorViewportLocation();
        Request.Rotation = GetEditorViewportRotation();
        Request.FOVAngle = GetEditorViewportFOV();

        // 把"用的是哪个机位"写进结果和预览面板，方便一眼核对
        // "预览里的画面"和"视口里看到的画面"是不是同一个机位。
        Request.bCameraFromViewport = HasEditorViewport();
        Request.CameraLabel = Request.bCameraFromViewport
            ? FString::Printf(
                TEXT("机位：当前关卡视口  P=%.1f  Y=%.1f  FOV=%.0f  (X=%.0f Y=%.0f Z=%.0f)"),
                Request.Rotation.Pitch, Request.Rotation.Yaw, Request.FOVAngle,
                Request.Location.X, Request.Location.Y, Request.Location.Z)
            : FString::Printf(
                TEXT("机位：兜底机位（没拿到视口相机）  P=%.1f  Y=%.1f  FOV=%.0f"),
                Request.Rotation.Pitch, Request.Rotation.Yaw, Request.FOVAngle);

        return Capture(Request);
    }

    FShineCaptureResult Capture(const FShineCaptureRequest& Request)
    {
        FShineCaptureResult Result;

        UWorld* World = GetEditorWorld();
        if (!World)
        {
            Result.ErrorMessage = TEXT("拿不到编辑器世界，请确认编辑器已打开一个关卡。");
            return Result;
        }

        const int32 SafeWidth = FMath::Clamp(Request.Width > 0 ? Request.Width : 768, 64, 4096);
        const int32 SafeHeight = FMath::Clamp(Request.Height > 0 ? Request.Height : 768, 64, 4096);
        const float SafeFOV = FMath::Clamp(Request.FOVAngle > 1.0f ? Request.FOVAngle : 55.0f, 5.0f, 170.0f);
        const FString UniquePrefix = MakeUniquePrefix(Request.FilePrefix);

        FString Directory = Request.OutputDirectory;
        if (Directory.IsEmpty())
        {
            Directory = BuildDefaultCaptureDirectory();
        }
        Directory = FPaths::ConvertRelativePathToFull(Directory);
        IFileManager::Get().MakeDirectory(*Directory, true);

        TArray<ECaptureChannel> Channels;
        if (Request.bCaptureColor)
        {
            Channels.Add(ECaptureChannel::Color);
        }
        if (Request.bCaptureDepth)
        {
            Channels.Add(ECaptureChannel::Depth);
        }
        if (Request.bCaptureNormal)
        {
            Channels.Add(ECaptureChannel::Normal);
        }

        if (Channels.Num() == 0)
        {
            Result.ErrorMessage = TEXT("至少要选择一个捕获通道（Color/Depth/Normal）。");
            return Result;
        }

        FActorSpawnParameters SpawnParameters;
        SpawnParameters.ObjectFlags = RF_Transient;
        SpawnParameters.bHideFromSceneOutliner = true;

        ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(
            ASceneCapture2D::StaticClass(),
            FTransform(Request.Rotation, Request.Location),
            SpawnParameters);

        if (!CaptureActor)
        {
            Result.ErrorMessage = TEXT("创建 SceneCapture2D 失败。");
            return Result;
        }

        CaptureActor->SetFlags(RF_Transient);
        CaptureActor->SetActorLabel(TEXT("Shine_SceneCapture"));

        USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
        if (!CaptureComponent)
        {
            CaptureActor->Destroy();
            Result.ErrorMessage = TEXT("SceneCapture2D 缺少 CaptureComponent2D。");
            return Result;
        }

        // 先把相机位置/朝向摆好，否则首次 CaptureScene 用的是 Actor 的默认朝向。
        CaptureActor->SetActorLocationAndRotation(Request.Location, Request.Rotation);

        CaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
        CaptureComponent->FOVAngle = SafeFOV;
        CaptureComponent->bCaptureEveryFrame = false;
        CaptureComponent->bCaptureOnMovement = false;
        CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
        CaptureComponent->CaptureSortPriority = 1000;
        LockExposure(CaptureComponent);

        // 关键：刚 Spawn 出来的组件，渲染代理要等一次 EndOfFrame 更新才会建好。
        // 少了这一步，紧接着的 CaptureScene 会拍到"空场景"（图片纯色/空白）。
        World->SendAllEndOfFrameUpdates();
        FlushRenderingCommands();

        Result.Directory = Directory;
        Result.UniquePrefix = UniquePrefix;
        Result.Width = SafeWidth;
        Result.Height = SafeHeight;
        Result.Location = Request.Location;
        Result.Rotation = Request.Rotation;
        Result.CameraLabel = Request.CameraLabel;
        Result.bCameraFromViewport = Request.bCameraFromViewport;
        Result.bSuccess = true;

        for (ECaptureChannel Channel : Channels)
        {
            const bool bIsColor = (Channel == ECaptureChannel::Color);

            UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(CaptureActor, NAME_None, RF_Transient);
            if (!RenderTarget)
            {
                continue;
            }

            // 颜色用 8 位 sRGB 直接落盘；深度/法线是数据，必须用浮点目标再自己编码，
            // 否则引擎会把它当颜色写进 8 位目标，结果是一片白。
            RenderTarget->RenderTargetFormat = bIsColor ? RTF_RGBA8_SRGB : RTF_RGBA32f;
            RenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
            RenderTarget->bForceLinearGamma = bIsColor;
            RenderTarget->InitAutoFormat(SafeWidth, SafeHeight);
            RenderTarget->UpdateResourceImmediate(true);

            CaptureComponent->TextureTarget = RenderTarget;
            CaptureComponent->CaptureSource = GetChannelCaptureSource(Channel);
            CaptureComponent->ShowFlags.SetPostProcessing(true);
            CaptureComponent->ShowFlags.SetMotionBlur(false);
            CaptureComponent->ShowFlags.SetBloom(bIsColor);
            CaptureComponent->ShowFlags.SetTemporalAA(false);

            CaptureComponent->CaptureScene();
            FlushRenderingCommands();

            const FString FilePath = FPaths::Combine(
                Directory,
                FString::Printf(TEXT("%s_%s.png"), *UniquePrefix, GetChannelSuffix(Channel)));

            FShineCaptureFile& ChannelFile = GetChannelFile(Result, Channel);
            ChannelFile.Path = FilePath;

            bool bWritten = false;

            if (bIsColor)
            {
                FImageWriteOptions Options;
                Options.Format = EDesiredImageFormat::PNG;
                Options.bOverwriteFile = true;
                Options.bAsync = false;
                Options.CompressionQuality = 95;
                UImageWriteBlueprintLibrary::ExportToDisk(RenderTarget, FilePath, Options);
                bWritten = FPaths::FileExists(FilePath);
            }
            else
            {
                FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
                TArray<FLinearColor> Pixels;
                if (Resource && Resource->ReadLinearColorPixels(Pixels) && Pixels.Num() == SafeWidth * SafeHeight)
                {
                    double MinValue = TNumericLimits<double>::Max();
                    double MaxValue = -TNumericLimits<double>::Max();
                    double Sum = 0.0;
                    int32 Count = 0;

                    if (Channel == ECaptureChannel::Depth)
                    {
                        // SCS_SceneDepth 写的是线性深度（厘米）。天空/远平面会是极大值，
                        // 这里先统计有效范围，再按 min..max 归一化成"近亮远暗"的深度图。
                        TArray<float> Luminance;
                        Luminance.SetNumUninitialized(Pixels.Num());

                        const double ValidMax = 1.0e7;
                        for (const FLinearColor& Pixel : Pixels)
                        {
                            const double Depth = Pixel.R;
                            if (Depth > 0.0 && Depth < ValidMax)
                            {
                                MinValue = FMath::Min(MinValue, Depth);
                                MaxValue = FMath::Max(MaxValue, Depth);
                                Sum += Depth;
                                ++Count;
                            }
                        }

                        if (Count == 0)
                        {
                            MinValue = 0.0;
                            MaxValue = 1.0;
                        }

                        const double Range = FMath::Max(MaxValue - MinValue, 1.0);
                        for (int32 Index = 0; Index < Pixels.Num(); ++Index)
                        {
                            const double Depth = Pixels[Index].R;
                            const double Clamped = FMath::Clamp((Depth - MinValue) / Range, 0.0, 1.0);
                            // 近处亮、远处暗（ControlNet depth 的标准约定）
                            Luminance[Index] = static_cast<float>(1.0 - Clamped);
                        }

                        bWritten = WriteGrayPng(Luminance, SafeWidth, SafeHeight, FilePath);
                        ChannelFile.bHasStats = true;
                    }
                    else
                    {
                        // SCS_Normal 输出的是世界法线原始分量 [-1,1]（最大值恰好是 1.0，
                        // 所以不能靠"大于 1"来判断），必须先探测有没有负分量，再统一编码成
                        // 标准法线贴图 n * 0.5 + 0.5；否则负分量会被 8 位图直接钳成黑色。
                        double RawMin = TNumericLimits<double>::Max();
                        double RawMax = -TNumericLimits<double>::Max();
                        for (const FLinearColor& Pixel : Pixels)
                        {
                            RawMin = FMath::Min(RawMin, static_cast<double>(FMath::Min3(Pixel.R, Pixel.G, Pixel.B)));
                            RawMax = FMath::Max(RawMax, static_cast<double>(FMath::Max3(Pixel.R, Pixel.G, Pixel.B)));
                        }

                        const bool bRawNormal = RawMin < -0.001;

                        TArray<FLinearColor> Encoded;
                        Encoded.SetNumUninitialized(Pixels.Num());
                        for (int32 Index = 0; Index < Pixels.Num(); ++Index)
                        {
                            FLinearColor Value = Pixels[Index];
                            if (bRawNormal)
                            {
                                Value = FLinearColor(
                                    Value.R * 0.5f + 0.5f,
                                    Value.G * 0.5f + 0.5f,
                                    Value.B * 0.5f + 0.5f,
                                    1.0f);
                            }

                            Encoded[Index] = FLinearColor(
                                FMath::Clamp(Value.R, 0.0f, 1.0f),
                                FMath::Clamp(Value.G, 0.0f, 1.0f),
                                FMath::Clamp(Value.B, 0.0f, 1.0f),
                                1.0f);

                            MinValue = FMath::Min(MinValue, static_cast<double>(Encoded[Index].R));
                            MaxValue = FMath::Max(MaxValue, static_cast<double>(Encoded[Index].R));
                            Sum += Encoded[Index].R;
                            ++Count;
                        }

                        bWritten = WriteRgbPng(Encoded, SafeWidth, SafeHeight, FilePath);
                        ChannelFile.bHasStats = true;
                        ChannelFile.bRawNormal = bRawNormal;
                    }

                    ChannelFile.MinValue = (Count > 0 && MinValue != TNumericLimits<double>::Max()) ? MinValue : 0.0;
                    ChannelFile.MaxValue = (Count > 0 && MaxValue != -TNumericLimits<double>::Max()) ? MaxValue : 0.0;
                    ChannelFile.MeanValue = Count > 0 ? Sum / Count : 0.0;
                }
            }

            ChannelFile.Bytes = FPaths::FileExists(FilePath) ? IFileManager::Get().FileSize(*FilePath) : 0;
            Result.bAnyExported |= bWritten;
        }

        CaptureComponent->TextureTarget = nullptr;
        CaptureActor->Destroy();

        if (!Result.bAnyExported)
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("所有通道都写入失败。");
            return Result;
        }

        if (Request.bShowPreview)
        {
            TArray<FShineCapturePreviewItem> PreviewItems;

            auto AddPreview = [&PreviewItems](const TCHAR* Label, const FShineCaptureFile& File)
            {
                if (File.Path.IsEmpty() || !FPaths::FileExists(File.Path))
                {
                    return;
                }

                FShineCapturePreviewItem Item;
                Item.Label = Label;
                Item.FilePath = File.Path;
                Item.Detail = File.bHasStats
                    ? FString::Printf(TEXT("[%.2f..%.2f]"), File.MinValue, File.MaxValue)
                    : FString();
                PreviewItems.Add(MoveTemp(Item));
            };

            AddPreview(TEXT("Color"), Result.Color);
            AddPreview(TEXT("Depth"), Result.Depth);
            AddPreview(TEXT("Normal"), Result.Normal);

            ShineCapturePreview::Show(PreviewItems, Request.CameraLabel);
        }

        return Result;
    }
}
