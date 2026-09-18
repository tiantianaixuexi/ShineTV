#include "Capture/ShineCapturePreview.h"

#include "Capture/ShineSceneCapture.h"
#include "EditorViewportClient.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/Paths.h"
#include "SEditorViewport.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    /** 缩略图宽度（像素）；高度按图片比例算。 */
    constexpr float PreviewWidth = 208.0f;

    /** 预览面板最多占视口高度的比例，超出就滚动。 */
    constexpr float MaxPreviewHeightRatio = 0.62f;

    TArray<TStrongObjectPtr<UTexture2D>> GPreviewTextures;
    TArray<TSharedPtr<FSlateBrush>> GPreviewBrushes;
    TSharedPtr<SWidget> GPreviewWidget;
    TWeakPtr<SEditorViewport> GPreviewViewport;

    const FLinearColor PreviewTextColor(0.92f, 0.95f, 0.99f, 1.0f);
    const FLinearColor PreviewMutedColor(0.66f, 0.72f, 0.80f, 1.0f);
    const FLinearColor PreviewCardColor(0.07f, 0.08f, 0.10f, 0.94f);
    const FLinearColor PreviewImageBackColor(0.02f, 0.02f, 0.03f, 0.92f);
}

namespace ShineCapturePreview
{
    void Clear()
    {
        if (TSharedPtr<SEditorViewport> Viewport = GPreviewViewport.Pin())
        {
            if (GPreviewWidget.IsValid())
            {
                Viewport->RemoveOverlayWidget(GPreviewWidget.ToSharedRef());
            }
        }

        GPreviewWidget.Reset();
        GPreviewViewport.Reset();
        GPreviewBrushes.Reset();
        GPreviewTextures.Reset();
    }

    bool IsVisible()
    {
        return GPreviewWidget.IsValid();
    }

    void Show(const TArray<FShineCapturePreviewItem>& Items, const FString& Header)
    {
        Clear();

        FLevelEditorViewportClient* ViewportClient = ShineSceneCapture::GetActiveViewportClient();
        if (!ViewportClient)
        {
            return;
        }

        TSharedPtr<SEditorViewport> ViewportWidget = ViewportClient->GetEditorViewportWidget();
        if (!ViewportWidget.IsValid())
        {
            return;
        }

        TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

        if (!Header.IsEmpty())
        {
            Column->AddSlot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                [
                    SNew(STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FSlateColor(PreviewMutedColor))
                    .AutoWrapText(true)
                    .Text(FText::FromString(Header))
                ];
        }

        for (const FShineCapturePreviewItem& Item : Items)
        {
            if (Item.FilePath.IsEmpty() || !FPaths::FileExists(Item.FilePath))
            {
                continue;
            }

            UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Item.FilePath);
            if (!Texture)
            {
                continue;
            }

            GPreviewTextures.Emplace(Texture);

            const float Aspect = (Texture->GetSizeX() > 0)
                ? static_cast<float>(Texture->GetSizeY()) / static_cast<float>(Texture->GetSizeX())
                : 1.0f;

            TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
            Brush->SetResourceObject(Texture);
            Brush->ImageSize = FVector2D(PreviewWidth, PreviewWidth * Aspect);
            Brush->DrawAs = ESlateBrushDrawType::Image;
            GPreviewBrushes.Add(Brush);

            Column->AddSlot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                        .ColorAndOpacity(FSlateColor(PreviewTextColor))
                        .Text(FText::FromString(Item.Label))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                    [
                        SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                        .BorderBackgroundColor(PreviewImageBackColor)
                        .Padding(FMargin(2.0f))
                        [
                            SNew(SImage)
                            .Image(Brush.Get())
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                    [
                        SNew(STextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .ColorAndOpacity(FSlateColor(PreviewMutedColor))
                        .Text(FText::FromString(Item.Detail))
                    ]
                ];
        }

        if (Column->NumSlots() == 0)
        {
            return;
        }

        // 视口高度不够时靠滚动看第三张（以前超出部分会被直接裁掉）。
        const FIntPoint ViewportSize = ViewportClient->Viewport ? ViewportClient->Viewport->GetSizeXY() : FIntPoint(1080, 720);
        const float MaxPanelHeight = FMath::Max(240.0f, ViewportSize.Y * MaxPreviewHeightRatio);

        const TSharedRef<SWidget> Panel =
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(PreviewCardColor)
            .Padding(FMargin(8.0f))
            [
                SNew(SBox)
                .MaxDesiredHeight(MaxPanelHeight)
                [
                    SNew(SScrollBox)
                    .Orientation(Orient_Vertical)
                    + SScrollBox::Slot()
                    [
                        Column
                    ]
                ]
            ];

        // 只有右侧那块面板本身接收鼠标（这样滚动条能用），左侧空白区域点击照旧落到视口。
        const TSharedRef<SWidget> Root =
            SNew(SHorizontalBox)
            .Visibility(EVisibility::SelfHitTestInvisible)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SSpacer)
                .Visibility(EVisibility::HitTestInvisible)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .HAlign(HAlign_Right)
            .VAlign(VAlign_Top)
            .Padding(FMargin(12.0f))
            [
                SNew(SVerticalBox)
                .Visibility(EVisibility::SelfHitTestInvisible)
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Right)
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("关闭预览")))
                    .OnClicked_Lambda([]()
                    {
                        ShineCapturePreview::Clear();
                        return FReply::Handled();
                    })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    Panel
                ]
            ];

        ViewportWidget->AddOverlayWidget(Root, 1000);
        GPreviewWidget = Root;
        GPreviewViewport = ViewportWidget;
    }
}
