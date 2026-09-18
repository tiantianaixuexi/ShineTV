#include "Graph/Slate/Video/SShineVideoGroupNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Video/ShineVideoGroupGraphNode.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UI/SShineComfyMediaPreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SShineVideoGroupNode::Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode)
{
    SShineVideoNode::Construct(SShineVideoNode::FArguments(), InNode);
}

UShineVideoGroupGraphNode* SShineVideoGroupNode::GetGroupNode() const
{
    return Cast<UShineVideoGroupGraphNode>(GraphNode);
}

TSharedRef<SWidget> SShineVideoGroupNode::BuildPreviewWidget()
{
    // 每次重建都从这里开始：控件被销毁过，旧的弱引用不能再用。
    SegmentPreviews.Reset();

    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

    const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    const int32 SlotCount = GroupNode ? GroupNode->GetSegmentSlotCount() : 0;

    for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
    {
        Rows->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                BuildSegmentRow(SlotIndex)
            ];
    }

    Rows->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 0.0f)
        [
            BuildSlotButtons()
        ];

    return Rows;
}

TSharedRef<SWidget> SShineVideoGroupNode::BuildSegmentRow(int32 SlotIndex)
{
    UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    const FString OutputFile = GroupNode ? GroupNode->GetSegmentOutputFile(SlotIndex) : FString();
    const FString Caption = GroupNode ? GroupNode->GetSegmentCaption(SlotIndex) : FString();

    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(76.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("#%d"), SlotIndex + 1)))
            ]
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(FText::FromString(Caption.IsEmpty()
                    ? TEXT("( 还没编译：这段的标题在编译后出现 )")
                    : Caption))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .ColorAndOpacity(this, &SShineVideoGroupNode::GetSegmentStatusColor, SlotIndex)
                .Text(this, &SShineVideoGroupNode::GetSegmentStatusText, SlotIndex)
            ]
        ];

    if (!OutputFile.IsEmpty())
    {
        TSharedPtr<SShineComfyMediaPreview> Preview;

        Row->AddSlot()
            .AutoWidth()
            .Padding(4.0f, 0.0f, 4.0f, 0.0f)
            [
                SAssignNew(Preview, SShineComfyMediaPreview)
                .PreviewSize(FVector2D(176.0f, 99.0f))
                .MediaPath(OutputFile)
                .bShowPlaybackControls(true)
                .OnPlayClicked(FOnClicked::CreateSP(this, &SShineVideoGroupNode::HandleToggleSegmentPlayback, SlotIndex))
                .PlayButtonText_Lambda([this, SlotIndex]() -> FText
                {
                    return GetSegmentPlayButtonText(SlotIndex);
                })
            ];

        SegmentPreviews.SetNum(FMath::Max(SegmentPreviews.Num(), SlotIndex + 1));
        SegmentPreviews[SlotIndex] = Preview;

        // 这次重建之前可能已经在播了（播放器归节点持有，重建不会打断它），
        // 所以把节点的贴图直接接进新控件，画面立刻就有。
        if (Preview.IsValid() && GroupNode)
        {
            Preview->SetVideoTexture(GroupNode->GetSegmentTexture(SlotIndex));
        }
    }
    else
    {
        Row->AddSlot()
            .AutoWidth()
            .Padding(4.0f, 0.0f)
            [
                SNew(STextBlock)
                .ColorAndOpacity(FLinearColor(0.55f, 0.58f, 0.62f, 1.0f))
                .Text(FText::FromString(TEXT("( 还没有产物 )")))
            ];
    }

    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.09f, 0.10f, 0.12f, 1.0f))
        .Padding(FMargin(6.0f, 4.0f))
        [
            Row
        ];
}

TSharedRef<SWidget> SShineVideoGroupNode::BuildSlotButtons()
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("＋ 一段")))
            .ToolTipText(FText::FromString(FString::Printf(TEXT("加一个段槽位（最多 %d 段）"),
                UShineVideoGroupGraphNode::MaxSegments)))
            .IsEnabled_Lambda([this]() -> bool
            {
                const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
                return GroupNode && GroupNode->CanAddSegmentSlot();
            })
            .OnClicked(FOnClicked::CreateSP(this, &SShineVideoGroupNode::HandleAddSegmentSlot))
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("－ 末段")))
            .ToolTipText(FText::FromString(TEXT("删掉最后一个段槽位（只在它没接线时可用）")))
            .IsEnabled_Lambda([this]() -> bool
            {
                const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
                return GroupNode && GroupNode->CanRemoveLastSegmentSlot();
            })
            .OnClicked(FOnClicked::CreateSP(this, &SShineVideoGroupNode::HandleRemoveSegmentSlot))
        ];
}

FReply SShineVideoGroupNode::HandleAddSegmentSlot()
{
    if (UShineVideoGroupGraphNode* GroupNode = GetGroupNode())
    {
        GroupNode->AddSegmentSlot();
    }

    return FReply::Handled();
}

FReply SShineVideoGroupNode::HandleRemoveSegmentSlot()
{
    if (UShineVideoGroupGraphNode* GroupNode = GetGroupNode())
    {
        GroupNode->RemoveLastSegmentSlot();
    }

    return FReply::Handled();
}

FReply SShineVideoGroupNode::HandleToggleSegmentPlayback(int32 SlotIndex)
{
    UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    if (!GroupNode)
    {
        return FReply::Handled();
    }

    GroupNode->ToggleSegmentPlayback(SlotIndex);

    // 第一次播放时才真正建解码器，所以这里要把新贴图接进控件；
    // 否则只有按钮文案在变，画面一直是占位文字。
    if (SegmentPreviews.IsValidIndex(SlotIndex) && SegmentPreviews[SlotIndex].IsValid())
    {
        SegmentPreviews[SlotIndex]->SetVideoTexture(GroupNode->GetSegmentTexture(SlotIndex));
    }

    return FReply::Handled();
}

FText SShineVideoGroupNode::GetSegmentPlayButtonText(int32 SlotIndex) const
{
    const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    const bool bPlaying = GroupNode && GroupNode->IsSegmentPlaying(SlotIndex);
    return FText::FromString(bPlaying ? TEXT("暂停") : TEXT("播放"));
}

FText SShineVideoGroupNode::GetSegmentStatusText(int32 SlotIndex) const
{
    const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    if (!GroupNode)
    {
        return FText::GetEmpty();
    }

    int32 Step = 0;
    int32 StepMax = 0;
    GroupNode->GetSegmentStep(SlotIndex, Step, StepMax);

    switch (GroupNode->GetSegmentState(SlotIndex))
    {
    case EShineVideoNodeRuntimeState::Pending:
        return FText::FromString(TEXT("排队中"));
    case EShineVideoNodeRuntimeState::Running:
        return FText::FromString(StepMax > 0
            ? FString::Printf(TEXT("运行中  %d/%d"), Step, StepMax)
            : TEXT("运行中"));
    case EShineVideoNodeRuntimeState::Finished:
        return FText::FromString(TEXT("完成"));
    case EShineVideoNodeRuntimeState::Failed:
        return FText::FromString(FString::Printf(TEXT("失败：%s"), *GroupNode->GetRuntimeDetail()));
    default:
        return FText::FromString(TEXT("未运行"));
    }
}

FSlateColor SShineVideoGroupNode::GetSegmentStatusColor(int32 SlotIndex) const
{
    const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    return UShineVideoGraphNodeBase::GetStateColor(
        GroupNode ? GroupNode->GetSegmentState(SlotIndex) : EShineVideoNodeRuntimeState::Idle);
}

FText SShineVideoGroupNode::GetExtraStatusText() const
{
    const UShineVideoGroupGraphNode* GroupNode = GetGroupNode();
    if (!GroupNode)
    {
        return FText::GetEmpty();
    }

    int32 LinkedCount = 0;
    TArray<UEdGraphPin*> SegmentPins;
    GroupNode->GetSegmentInputPins(SegmentPins);
    for (const UEdGraphPin* Pin : SegmentPins)
    {
        if (Pin && Pin->LinkedTo.Num() > 0)
        {
            ++LinkedCount;
        }
    }

    return FText::FromString(FString::Printf(TEXT("%d 段（已接 %d）"), SegmentPins.Num(), LinkedCount));
}

bool SShineVideoGroupNode::HasExtraBody() const
{
    return true;
}
