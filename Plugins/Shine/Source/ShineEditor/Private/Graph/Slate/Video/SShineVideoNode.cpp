#include "Graph/Slate/Video/SShineVideoNode.h"

#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

void SShineVideoNode::Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode)
{
    SShineComfyGraphStandardNode::Construct(SShineComfyGraphStandardNode::FArguments(), InNode);
}

UShineVideoGraphNodeBase* SShineVideoNode::GetVideoNode() const
{
    // `GraphNode` 在 SGraphNode 里是 `UEdGraphNode*`（非 const），`GetShineNode()` 返回 const；
    // 这里要的是"能读也能改运行期状态"的那个指针。
    return Cast<UShineVideoGraphNodeBase>(GraphNode);
}

TSharedRef<SWidget> SShineVideoNode::BuildExtraBodyWidget()
{
    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.07f, 0.08f, 0.10f, 1.0f))
        .Padding(FMargin(10.0f, 8.0f))
        .Visibility_Lambda([this]() -> EVisibility
        {
            return HasExtraBody() ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                BuildPreviewWidget()
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    // 一个小圆点当状态灯：不用图，Slate 的 WhiteBrush 画方块也一样醒目。
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(this, &SShineVideoNode::GetRuntimeStateColor)
                    .Padding(FMargin(4.0f, 4.0f))
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .ColorAndOpacity(this, &SShineVideoNode::GetRuntimeStateColor)
                    .Text(this, &SShineVideoNode::GetRuntimeStateText)
                ]
            ]
        ];
}

TSharedRef<SWidget> SShineVideoNode::BuildPreviewWidget()
{
    return SNullWidget::NullWidget;
}

FText SShineVideoNode::GetExtraStatusText() const
{
    return FText::GetEmpty();
}

bool SShineVideoNode::HasExtraBody() const
{
    const UShineVideoGraphNodeBase* VideoNode = GetVideoNode();
    return (VideoNode && VideoNode->GetRuntimeState() != EShineVideoNodeRuntimeState::Idle)
        || !GetExtraStatusText().IsEmpty();
}

FText SShineVideoNode::GetRuntimeStateText() const
{
    const UShineVideoGraphNodeBase* VideoNode = GetVideoNode();
    if (!VideoNode)
    {
        return FText::GetEmpty();
    }

    FString StateText;
    switch (VideoNode->GetRuntimeState())
    {
    case EShineVideoNodeRuntimeState::Pending:
        StateText = TEXT("排队中");
        break;
    case EShineVideoNodeRuntimeState::Running:
        StateText = VideoNode->GetRuntimeStepMax() > 0
            ? FString::Printf(TEXT("运行中  %d/%d"), VideoNode->GetRuntimeStep(), VideoNode->GetRuntimeStepMax())
            : TEXT("运行中");
        break;
    case EShineVideoNodeRuntimeState::Finished:
        StateText = TEXT("完成");
        break;
    case EShineVideoNodeRuntimeState::Failed:
        StateText = VideoNode->GetRuntimeDetail().IsEmpty()
            ? TEXT("失败")
            : FString::Printf(TEXT("失败：%s"), *VideoNode->GetRuntimeDetail());
        break;
    default:
        StateText = TEXT("空闲");
        break;
    }

    const FText ExtraText = GetExtraStatusText();
    if (!ExtraText.IsEmpty())
    {
        StateText += FString::Printf(TEXT("    %s"), *ExtraText.ToString());
    }

    return FText::FromString(StateText);
}

FSlateColor SShineVideoNode::GetRuntimeStateColor() const
{
    const UShineVideoGraphNodeBase* VideoNode = GetVideoNode();
    return UShineVideoGraphNodeBase::GetStateColor(
        VideoNode ? VideoNode->GetRuntimeState() : EShineVideoNodeRuntimeState::Idle);
}
