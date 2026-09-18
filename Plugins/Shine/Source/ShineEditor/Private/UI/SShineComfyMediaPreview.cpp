#include "UI/SShineComfyMediaPreview.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    /** Slate 的动态图资源按名字注册，图片的完整路径就是最自然的键。 */
    FName MakeBrushResourceName(const FString& FullPath)
    {
        return FName(*FullPath);
    }
}

void SShineComfyMediaPreview::Construct(const FArguments& InArgs)
{
    PreviewSize = (InArgs._PreviewSize.X > 0.0 && InArgs._PreviewSize.Y > 0.0)
        ? InArgs._PreviewSize
        : FVector2D(240.0f, 135.0f);
    DisplaySize = PreviewSize;

    bShowPlaybackControls = InArgs._bShowPlaybackControls;
    OnPlayClicked = InArgs._OnPlayClicked;
    PlayButtonText = InArgs._PlayButtonText;

    TSharedRef<SOverlay> MediaStack = SNew(SOverlay)

        + SOverlay::Slot()
        [
            SNew(SImage)
            .Image(this, &SShineComfyMediaPreview::GetImageBrush)
            .Visibility(this, &SShineComfyMediaPreview::GetImageVisibility)
        ]

        + SOverlay::Slot()
        [
            SNew(SImage)
            .Image(this, &SShineComfyMediaPreview::GetVideoBrush)
            .Visibility(this, &SShineComfyMediaPreview::GetVideoVisibility)
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.13f, 0.13f, 0.15f, 1.0f))
            .Padding(FMargin(8.0f, 4.0f))
            .Visibility(this, &SShineComfyMediaPreview::GetPlaceholderVisibility)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Justification(ETextJustify::Center)
                .Text(this, &SShineComfyMediaPreview::GetPlaceholderText)
            ]
        ];

    if (bShowPlaybackControls)
    {
        MediaStack->AddSlot()
            .HAlign(HAlign_Right)
            .VAlign(VAlign_Bottom)
            .Padding(FMargin(0.0f, 0.0f, 4.0f, 4.0f))
            [
                SNew(SButton)
                .Text(PlayButtonText)
                .OnClicked(OnPlayClicked)
                .Visibility(this, &SShineComfyMediaPreview::GetPlayButtonVisibility)
            ];
    }

    ChildSlot
    [
        SNew(SBox)
        .WidthOverride_Lambda([this]() -> float { return static_cast<float>(DisplaySize.X); })
        .HeightOverride_Lambda([this]() -> float { return static_cast<float>(DisplaySize.Y); })
        [
            MediaStack
        ]
    ];

    if (!InArgs._MediaPath.IsEmpty())
    {
        SetMediaPath(InArgs._MediaPath);
    }
}

bool SShineComfyMediaPreview::HasVideoExtension(const FString& Path)
{
    static const TCHAR* const VideoExtensions[] =
    {
        TEXT("mp4"), TEXT("webm"), TEXT("mov"), TEXT("mkv"), TEXT("avi"), TEXT("m4v"), TEXT("wmv")
    };

    const FString Extension = FPaths::GetExtension(Path).ToLower();
    for (const TCHAR* const Candidate : VideoExtensions)
    {
        if (Extension == Candidate)
        {
            return true;
        }
    }

    return false;
}

void SShineComfyMediaPreview::SetMediaPath(const FString& InPath)
{
    const FString Trimmed = InPath.TrimStartAndEnd();
    if (Trimmed == MediaPath)
    {
        return;
    }

    MediaPath = Trimmed;

    // 换了媒体就把上一次的图刷放掉：不然换了图之后旧图还在，看起来像"没刷新"。
    ImageBrush.Reset();
    bImageReady = false;
    LoadError.Reset();

    Reload();
}

void SShineComfyMediaPreview::Reload()
{
    ImageBrush.Reset();
    bImageReady = false;
    LoadError.Reset();

    if (MediaPath.IsEmpty())
    {
        return;
    }

    const FString FullPath = FPaths::ConvertRelativePathToFull(MediaPath);
    if (!FPaths::FileExists(FullPath))
    {
        LoadError = FString::Printf(TEXT("找不到文件：%s"), *FullPath);
        return;
    }

    bIsVideo = HasVideoExtension(FullPath);
    if (bIsVideo)
    {
        // 视频的贴图由调用方（节点）在开始播放时交进来，这里只等。
        DisplaySize = PreviewSize;
        return;
    }

    if (!FSlateApplication::IsInitialized())
    {
        LoadError = TEXT("Slate 还没起来，暂时画不了图");
        return;
    }

    FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
    if (!Renderer)
    {
        LoadError = TEXT("拿不到 Slate 渲染器");
        return;
    }

    // 这句就是 Slate 的"从磁盘载图"入口；返回图的实际尺寸（0 表示它认不出这个格式）。
    const FName ResourceName = MakeBrushResourceName(FullPath);
    const FIntPoint SourceSize = Renderer->GenerateDynamicImageResource(ResourceName);
    if (SourceSize.X <= 0 || SourceSize.Y <= 0)
    {
        LoadError = FString::Printf(TEXT("Slate 读不了这张图（只支持 png/jpg/bmp 这类常见格式）：%s"),
            *FPaths::GetCleanFilename(FullPath));
        return;
    }

    DisplaySize = ComputeDisplaySize(SourceSize.X, SourceSize.Y);
    ImageBrush = MakeShared<FSlateDynamicImageBrush>(ResourceName, DisplaySize);
    bImageReady = true;
}

void SShineComfyMediaPreview::SetVideoTexture(UMediaTexture* InTexture)
{
    if (VideoTexture.Get() == InTexture)
    {
        return;
    }

    VideoTexture = InTexture;
    VideoBrush.Reset();

    if (!InTexture)
    {
        return;
    }

    VideoBrush = MakeShared<FSlateBrush>();
    VideoBrush->SetResourceObject(InTexture);
    VideoBrush->ImageSize = DisplaySize;
    VideoBrush->DrawAs = ESlateBrushDrawType::Image;
    VideoBrush->Tiling = ESlateBrushTileType::NoTile;
}

FVector2D SShineComfyMediaPreview::ComputeDisplaySize(int32 SourceWidth, int32 SourceHeight) const
{
    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        return PreviewSize;
    }

    const double Scale = FMath::Min(
        PreviewSize.X / static_cast<double>(SourceWidth),
        PreviewSize.Y / static_cast<double>(SourceHeight));

    // 小图不放大（放大只会糊），大图按比例缩进预览框。
    const double AppliedScale = FMath::Min(Scale, 1.0);
    return FVector2D(SourceWidth * AppliedScale, SourceHeight * AppliedScale);
}

EVisibility SShineComfyMediaPreview::GetImageVisibility() const
{
    return bImageReady ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SShineComfyMediaPreview::GetVideoVisibility() const
{
    return (bIsVideo && VideoBrush.IsValid()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SShineComfyMediaPreview::GetPlayButtonVisibility() const
{
    return (bIsVideo && VideoTexture.IsValid()) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SShineComfyMediaPreview::GetPlaceholderVisibility() const
{
    // 图和视频都没画出来时才显示占位/错误文字；有一条能画就不该盖住画面。
    const bool bHasImage = GetImageVisibility() != EVisibility::Collapsed;
    const bool bHasVideo = GetVideoVisibility() != EVisibility::Collapsed;
    return (bHasImage || bHasVideo) ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

const FSlateBrush* SShineComfyMediaPreview::GetImageBrush() const
{
    return ImageBrush.IsValid() ? ImageBrush.Get() : nullptr;
}

const FSlateBrush* SShineComfyMediaPreview::GetVideoBrush() const
{
    return VideoBrush.IsValid() ? VideoBrush.Get() : nullptr;
}

FText SShineComfyMediaPreview::GetPlaceholderText() const
{
    if (!LoadError.IsEmpty())
    {
        return FText::FromString(LoadError);
    }

    if (MediaPath.IsEmpty())
    {
        return FText::FromString(TEXT("( 还没有图 )"));
    }

    if (bIsVideo)
    {
        return FText::FromString(FString::Printf(TEXT("视频：%s"), *FPaths::GetCleanFilename(MediaPath)));
    }

    return FText::FromString(FPaths::GetCleanFilename(MediaPath));
}
