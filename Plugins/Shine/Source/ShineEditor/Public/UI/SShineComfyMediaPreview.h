#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"

class UMediaTexture;

/**
 * 节点里的媒体预览：图片走 Slate 的动态图刷，视频走调用方给的 `UMediaTexture`。
 *
 * ## 为什么图片不自己解码
 *
 * `FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(路径)` 就是 Slate
 * 加载磁盘图片的官方入口，配 `FSlateDynamicImageBrush` 使用；自己写一遍 PNG/JPG 解码
 * 只会在"某些位深的图"上踩坑。ComfyUI 写出来的就是普通 PNG，这条路径完全够用。
 *
 * ## 为什么视频的播放器不归这个控件管
 *
 * 节点控件在一次任务里会被反复重建——`SGraphPanel::PurgeVisualRepresentation` 是
 * `NotifyGraphChanged()` 的固定动作，而逐段进度更新会触发它。把 `UMediaPlayer` 放在控件里，
 * 每次重建都会把正在播的片子掐掉。所以播放器归**节点**（`UPROPERTY(Transient)`）持有，
 * 这个控件只负责"把那张贴图画出来 + 提供一个播放按钮"。
 */
class SHINEEDITOR_API SShineComfyMediaPreview : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineComfyMediaPreview)
        : _PreviewSize(FVector2D(240.0f, 135.0f))
        , _bShowPlaybackControls(false)
    {}
        /** 预览框的最大尺寸（图片按原图比例缩进去，小图不放大）。 */
        SLATE_ARGUMENT(FVector2D, PreviewSize)

        /** 媒体路径（图片或视频；按扩展名自动判断）。 */
        SLATE_ARGUMENT(FString, MediaPath)

        /** 视频是否显示播放/暂停按钮（由调用方提供 `OnPlayClicked`）。 */
        SLATE_ARGUMENT(bool, bShowPlaybackControls)

        /** 播放按钮的点击事件。 */
        SLATE_EVENT(FOnClicked, OnPlayClicked)

        /** 播放按钮上显示的文案（"播放" / "暂停"）。 */
        SLATE_ATTRIBUTE(FText, PlayButtonText)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** 换一个媒体（路径没变就不重复读盘）。 */
    void SetMediaPath(const FString& InPath);

    const FString& GetMediaPath() const { return MediaPath; }

    /** 视频：把这路播放器的贴图交进来（归调用方所有，这里只画）。 */
    void SetVideoTexture(UMediaTexture* InTexture);

    /** 强制重读图片（同名文件可能已被新结果覆盖）。 */
    void Reload();

    bool HasMedia() const { return !MediaPath.IsEmpty(); }
    bool IsVideo() const { return bIsVideo; }
    bool IsImageReady() const { return bImageReady; }
    const FString& GetLoadError() const { return LoadError; }

    /** 这个路径看起来是不是视频（按扩展名）。 */
    static bool HasVideoExtension(const FString& Path);

private:
    EVisibility GetImageVisibility() const;
    EVisibility GetVideoVisibility() const;
    EVisibility GetPlayButtonVisibility() const;

    /** 三张画面都没有东西可画时，显示占位/错误文字。 */
    EVisibility GetPlaceholderVisibility() const;
    const FSlateBrush* GetImageBrush() const;
    const FSlateBrush* GetVideoBrush() const;
    FText GetPlaceholderText() const;
    FVector2D ComputeDisplaySize(int32 SourceWidth, int32 SourceHeight) const;

    FVector2D PreviewSize = FVector2D(240.0f, 135.0f);
    FVector2D DisplaySize = FVector2D(240.0f, 135.0f);

    FString MediaPath;
    bool bIsVideo = false;
    bool bImageReady = false;
    bool bShowPlaybackControls = false;
    FString LoadError;

    /** 图片：Slate 的动态图刷（自己持有；Slate 只拿裸指针）。 */
    TSharedPtr<struct FSlateDynamicImageBrush> ImageBrush;

    /** 视频：调用方给的贴图 + 一个包它的画刷。 */
    TWeakObjectPtr<UMediaTexture> VideoTexture;
    TSharedPtr<FSlateBrush> VideoBrush;

    FOnClicked OnPlayClicked;
    TAttribute<FText> PlayButtonText;
};
