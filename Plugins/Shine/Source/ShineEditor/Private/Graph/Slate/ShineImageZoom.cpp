#include "Graph/Slate/ShineImageZoom.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    struct FShineZoomWindowState
    {
        TWeakPtr<SWindow> Window;
        FString ImagePath;
        TStrongObjectPtr<UTexture2D> Texture;
        TSharedPtr<FSlateBrush> Brush;
    };

    /** 只存弱引用：窗口关掉后 Slate 释放它，下次清理状态时贴图也跟着释放。 */
    TArray<TSharedPtr<FShineZoomWindowState>> GZoomWindows;

    void PruneClosedWindows()
    {
        for (int32 Index = GZoomWindows.Num() - 1; Index >= 0; --Index)
        {
            if (!GZoomWindows[Index].IsValid() || !GZoomWindows[Index]->Window.IsValid())
            {
                GZoomWindows.RemoveAt(Index);
            }
        }
    }

    bool PathsEqual(const FString& A, const FString& B)
    {
        return A.Equals(B, ESearchCase::IgnoreCase);
    }

    /** 重新读盘一次并刷新窗口内容（文件可能同名覆盖，所以必须重读）。 */
    bool ReloadWindowContent(const TSharedPtr<FShineZoomWindowState>& State)
    {
        if (!State.IsValid() || !State->Brush.IsValid() || !FPaths::FileExists(State->ImagePath))
        {
            return false;
        }

        UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(State->ImagePath);
        if (!Texture)
        {
            return false;
        }

        State->Texture = TStrongObjectPtr<UTexture2D>(Texture);
        State->Brush->SetResourceObject(Texture);
        State->Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());

        if (TSharedPtr<SWindow> Window = State->Window.Pin())
        {
            Window->SetTitle(FText::FromString(FPaths::GetCleanFilename(State->ImagePath)));
            // 贴图换了要主动请求重画，否则可能停在旧画面。
            Window->Invalidate(EInvalidateWidgetReason::Paint);
        }

        return true;
    }
}

namespace ShineImageZoom
{
    void CloseAll()
    {
        const TArray<TSharedPtr<FShineZoomWindowState>> Windows = GZoomWindows;
        GZoomWindows.Reset();

        for (const TSharedPtr<FShineZoomWindowState>& State : Windows)
        {
            if (State.IsValid())
            {
                if (TSharedPtr<SWindow> Window = State->Window.Pin())
                {
                    Window->RequestDestroyWindow();
                }
            }
        }
    }

    void Show(const FString& ImagePath)
    {
        const FString FullPath = FPaths::ConvertRelativePathToFull(ImagePath);
        if (!FPaths::FileExists(FullPath))
        {
            return;
        }

        PruneClosedWindows();

        // 同一张图已经开着：重读一次（同名文件可能已经被新结果覆盖）+ 置顶。
        for (const TSharedPtr<FShineZoomWindowState>& State : GZoomWindows)
        {
            if (State.IsValid() && PathsEqual(State->ImagePath, FullPath))
            {
                ReloadWindowContent(State);
                if (TSharedPtr<SWindow> Window = State->Window.Pin())
                {
                    Window->BringToFront(true);
                }
                return;
            }
        }

        UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(FullPath);
        if (!Texture)
        {
            return;
        }

        TSharedPtr<FShineZoomWindowState> State = MakeShared<FShineZoomWindowState>();
        State->ImagePath = FullPath;
        State->Texture = TStrongObjectPtr<UTexture2D>(Texture);
        State->Brush = MakeShared<FSlateBrush>();
        State->Brush->SetResourceObject(Texture);
        State->Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        State->Brush->DrawAs = ESlateBrushDrawType::Image;

        const float ImageWidth = static_cast<float>(Texture->GetSizeX());
        const float ImageHeight = static_cast<float>(Texture->GetSizeY());
        const float WindowWidth = FMath::Clamp(ImageWidth + 40.0f, 560.0f, 1680.0f);
        const float WindowHeight = FMath::Clamp(ImageHeight + 96.0f, 420.0f, 980.0f);

        const TWeakPtr<FShineZoomWindowState> WeakState = State;

        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(FText::FromString(FPaths::GetCleanFilename(FullPath)))
            .ClientSize(FVector2D(WindowWidth, WindowHeight))
            .SizingRule(ESizingRule::UserSized)
            .SupportsMaximize(true)
            .SupportsMinimize(false)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f, 1.0f))
                .Padding(FMargin(6.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.92f, 1.0f))
                            // Lambda 取状态里的路径与尺寸：窗口切换/重载后文字自动跟上。
                            .Text_Lambda([WeakState]()
                            {
                                const TSharedPtr<FShineZoomWindowState> Pinned = WeakState.Pin();
                                if (!Pinned.IsValid() || !Pinned->Texture.IsValid())
                                {
                                    return FText::GetEmpty();
                                }

                                return FText::FromString(FString::Printf(
                                    TEXT("%s    %dx%d"),
                                    *Pinned->ImagePath,
                                    Pinned->Texture->GetSizeX(),
                                    Pinned->Texture->GetSizeY()));
                            })
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("关闭")))
                            .OnClicked_Lambda([WeakState]()
                            {
                                if (const TSharedPtr<FShineZoomWindowState> Pinned = WeakState.Pin())
                                {
                                    if (TSharedPtr<SWindow> PinnedWindow = Pinned->Window.Pin())
                                    {
                                        PinnedWindow->RequestDestroyWindow();
                                    }
                                }
                                return FReply::Handled();
                            })
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SNew(SImage)
                            .Image_Lambda([WeakState]() -> const FSlateBrush*
                            {
                                const TSharedPtr<FShineZoomWindowState> Pinned = WeakState.Pin();
                                return Pinned.IsValid() ? Pinned->Brush.Get() : nullptr;
                            })
                        ]
                    ]
                ]
            ];

        State->Window = Window;
        GZoomWindows.Add(State);
        FSlateApplication::Get().AddWindow(Window);
    }

    void RefreshOpenWindows(const TArray<FString>& CurrentPaths)
    {
        PruneClosedWindows();
        if (GZoomWindows.Num() == 0)
        {
            return;
        }

        TArray<FString> FullPaths;
        FullPaths.Reserve(CurrentPaths.Num());
        for (const FString& Path : CurrentPaths)
        {
            FullPaths.Add(FPaths::ConvertRelativePathToFull(Path));
        }

        const FString NewestPath = FullPaths.Num() > 0 ? FullPaths.Last() : FString();

        for (const TSharedPtr<FShineZoomWindowState>& State : GZoomWindows)
        {
            if (!State.IsValid())
            {
                continue;
            }

            // 当前结果里还有这个文件 → 重读一遍（同名文件可能被覆盖）。
            const bool bStillCurrent = FullPaths.ContainsByPredicate(
                [&State](const FString& Path)
                {
                    return PathsEqual(Path, State->ImagePath);
                });

            if (bStillCurrent)
            {
                ReloadWindowContent(State);
                continue;
            }

            // 这张已经不在当前结果里了 → 跟着最新那张走，别停在上一次的结果上。
            if (!NewestPath.IsEmpty() && !PathsEqual(NewestPath, State->ImagePath))
            {
                State->ImagePath = NewestPath;
                ReloadWindowContent(State);
            }
        }
    }
}
