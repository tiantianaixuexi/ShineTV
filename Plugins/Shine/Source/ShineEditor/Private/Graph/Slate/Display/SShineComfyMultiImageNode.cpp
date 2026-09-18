#include "Graph/Slate/Display/SShineComfyMultiImageNode.h"

#include "Graph/Node/Display/ShineComfyMultiImageGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Slate/ShineImageZoom.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SShineComfyMultiImageNode::Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode)
{
    SShineComfyGraphStandardNode::Construct(SShineComfyGraphStandardNode::FArguments(), InNode);
}

void SShineComfyMultiImageNode::UpdateGraphNode()
{
    ResetNodeContainers();

    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FLinearColor AccentColor = ShineNode ? ShineNode->GetNodeTitleColor() : FLinearColor::Gray;

    SetupErrorReporting();
    ContentScale.Bind(this, &SGraphNode::GetContentScale);

    GetOrAddSlot(ENodeZone::Center)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.05f, 0.07f, 1.0f))
        .Padding(1.0f)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.11f, 0.09f, 0.12f, 1.0f))
            .Padding(0.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(AccentColor)
                    .Padding(FMargin(14.0f, 10.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(ShineNode ? ShineNode->GetNodeTitle(ENodeTitleType::ListView) : FText::GetEmpty())
                            .ColorAndOpacity(FLinearColor::White)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(ShineNode ? ShineNode->GetNodeSubtitle() : FText::GetEmpty())
                            .ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.85f))
                            .WrapTextAt(320.0f)
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 1.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.15f, 0.10f, 0.11f, 1.0f))
                    .Padding(FMargin(12.0f, 10.0f))
                    [
                        SAssignNew(GalleryGrid, SUniformGridPanel)
                        .SlotPadding(FMargin(6.0f))
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.10f, 0.08f, 0.10f, 1.0f))
                    .Padding(FMargin(10.0f, 8.0f))
                    [
                        SAssignNew(ParameterBox, SVerticalBox)
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.08f, 0.07f, 0.09f, 1.0f))
                    .Padding(FMargin(10.0f, 8.0f))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        [
                            SAssignNew(LeftNodeBox, SVerticalBox)
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(8.0f, 0.0f)
                        [
                            SNew(SBox)
                            .WidthOverride(18.0f)
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        [
                            SAssignNew(RightNodeBox, SVerticalBox)
                        ]
                    ]
                ]
            ]
        ]
    ];

    RebuildGalleryTiles();
    PopulateParameterWidgets();
    CreatePinWidgets();
}

void SShineComfyMultiImageNode::RebuildGalleryTiles()
{
    if (!GalleryGrid.IsValid())
    {
        return;
    }

    GalleryGrid->ClearChildren();
    TileBrushes.Reset();
    TileTextures.Reset();

    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const int32 ResultImageCount = ShineNode ? ShineNode->GetResultImageCount() : 0;
    const bool bHasResultImages = ResultImageCount > 0;

    // 有真实结果图时按结果数量铺；没有就退回"占位格子"形态（保持原来的样子）。
    const int32 ImageCount = bHasResultImages
        ? FMath::Min(ResultImageCount, 12)
        : GetGalleryImageCount();

    const int32 ColumnCount = bHasResultImages ? GetGalleryColumns() : GetGalleryColumns();
    const bool bShowFrameNumbers = GetShowFrameNumbers();

    // 单列（也就是 Preview 节点）时给一张大图，多列时用小格子。
    const float TileWidth = ColumnCount >= 2 ? 84.0f : 280.0f;
    const float TileHeight = ColumnCount >= 2 ? 84.0f : 168.0f;

    for (int32 ImageIndex = 0; ImageIndex < ImageCount; ++ImageIndex)
    {
        const int32 ColumnIndex = ImageIndex % ColumnCount;
        const int32 RowIndex = ImageIndex / ColumnCount;

        const FString ImagePath = bHasResultImages ? ShineNode->GetResultImagePath(ImageIndex) : FString();

        TSharedPtr<FSlateBrush> TileBrush;
        if (!ImagePath.IsEmpty() && FPaths::FileExists(ImagePath))
        {
            if (UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(ImagePath))
            {
                TileTextures.Emplace(Texture);

                const float TextureAspect = (Texture->GetSizeX() > 0)
                    ? static_cast<float>(Texture->GetSizeY()) / static_cast<float>(Texture->GetSizeX())
                    : 1.0f;
                const float BrushWidth = TileWidth - 8.0f;
                const float BrushHeight = FMath::Min(BrushWidth * TextureAspect, TileHeight - 18.0f);

                TileBrush = MakeShared<FSlateBrush>();
                TileBrush->SetResourceObject(Texture);
                TileBrush->ImageSize = FVector2D(BrushWidth, BrushHeight);
                TileBrush->DrawAs = ESlateBrushDrawType::Image;
                TileBrushes.Add(TileBrush);
            }
        }

        const FString TileLabel = !ImagePath.IsEmpty()
            ? FPaths::GetBaseFilename(ImagePath)
            : (bShowFrameNumbers
                ? FString::Printf(TEXT("Frame %02d"), ImageIndex + 1)
                : FString::Printf(TEXT("Image %02d"), ImageIndex + 1));

        TSharedRef<SWidget> TileWidget = TileBrush.IsValid()
            ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(TileBrush.Get()))
            : StaticCastSharedRef<SWidget>(
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.30f, 0.18f, 0.20f, 1.0f))
                .Padding(0.0f));

        // 有图时给一个"放大"按钮：弹独立窗口按原图尺寸看。
        const FString ZoomPath = ImagePath;
        TSharedRef<SHorizontalBox> LabelRow =
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TileLabel))
                .Justification(ETextJustify::Left)
                .ColorAndOpacity(FLinearColor(0.96f, 0.92f, 0.92f, 1.0f))
            ];

        if (TileBrush.IsValid())
        {
            LabelRow->AddSlot()
                .AutoWidth()
                .Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("放大")))
                    .ToolTipText(FText::FromString(TEXT("按原图尺寸打开，可拖大窗口看细节")))
                    .OnClicked_Lambda([ZoomPath]()
                    {
                        ShineImageZoom::Show(ZoomPath);
                        return FReply::Handled();
                    })
                ];
        }

        GalleryGrid->AddSlot(ColumnIndex, RowIndex)
        [
            SNew(SBox)
            .WidthOverride(TileWidth)
            .HeightOverride(TileHeight)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.20f, 0.14f, 0.16f, 1.0f))
                .Padding(FMargin(4.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    [
                        TileWidget
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                    [
                        LabelRow
                    ]
                ]
            ]
        ];
    }
}

int32 SShineComfyMultiImageNode::GetGalleryImageCount() const
{
    return FMath::Max(1, GetIntegerParameterValue(TEXT("ImageCount")));
}

int32 SShineComfyMultiImageNode::GetGalleryColumns() const
{
    return FMath::Max(1, GetIntegerParameterValue(TEXT("Columns")));
}

bool SShineComfyMultiImageNode::GetShowFrameNumbers() const
{
    return GetBoolParameterValue(TEXT("ShowFrameNumbers")) == ECheckBoxState::Checked;
}