#include "Graph/Slate/Video/SShineVideoShotNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Video/ShineVideoShotGraphNode.h"
#include "SGraphPin.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SShineVideoShotNode::Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode)
{
    SShineVideoNode::Construct(SShineVideoNode::FArguments(), InNode);
}

void SShineVideoShotNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
    const UEdGraphPin* Pin = PinToAdd->GetPinObj();
    const UShineVideoShotGraphNode* ShotNode = Cast<UShineVideoShotGraphNode>(GraphNode);

    const bool bIsPictureSlot = Pin
        && Pin->Direction == EGPD_Input
        && ShotNode
        && UShineVideoShotGraphNode::ParsePicturePinIndex(Pin->PinName) != INDEX_NONE;

    if (!bIsPictureSlot)
    {
        SShineVideoNode::AddPin(PinToAdd);
        return;
    }

    // pin 必须仍然贴在最左边（那是连线落点），所以徽标放在 pin 的**右边**。
    PinToAdd->SetOwner(SharedThis(this));

    LeftNodeBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                PinToAdd
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            .Padding(6.0f, 0.0f, 0.0f, 0.0f)
            [
                MakePictureBadgeWidget(Pin)
            ]
        ];

    InputPins.Add(PinToAdd);
}

TSharedRef<SWidget> SShineVideoShotNode::MakePictureBadgeWidget(const UEdGraphPin* Pin) const
{
    const UShineVideoShotGraphNode* ShotNode = Cast<UShineVideoShotGraphNode>(GraphNode);
    const int32 SlotIndex = (Pin && ShotNode) ? UShineVideoShotGraphNode::ParsePicturePinIndex(Pin->PinName) : INDEX_NONE;
    const int32 Ordinal = (ShotNode && SlotIndex != INDEX_NONE) ? ShotNode->GetPictureOrdinal(SlotIndex) : INDEX_NONE;

    const bool bLinked = Ordinal != INDEX_NONE;
    const FString Label = bLinked
        ? FString::Printf(TEXT("<Picture %d>"), Ordinal)
        : FString::Printf(TEXT("第 %d 槽（空）"), SlotIndex + 1);

    const FLinearColor BadgeColor = bLinked
        ? FLinearColor(0.20f, 0.42f, 0.66f, 1.0f)
        : FLinearColor(0.20f, 0.21f, 0.24f, 1.0f);

    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(BadgeColor)
        .Padding(FMargin(5.0f, 1.0f))
        .ToolTipText(FText::FromString(bLinked
            ? FString::Printf(TEXT("这个槽位会排到第 %d 张参考图（编号顺序 = 槽位顺序，空槽位跳过）"), Ordinal)
            : TEXT("空槽位不参与编号：接了线的槽位会往前挤")))
        [
            SNew(STextBlock)
            .Text(FText::FromString(Label))
            .ColorAndOpacity(FLinearColor(0.93f, 0.95f, 0.98f, 1.0f))
        ];
}

FText SShineVideoShotNode::GetExtraStatusText() const
{
    const UShineVideoShotGraphNode* ShotNode = Cast<UShineVideoShotGraphNode>(GraphNode);
    if (!ShotNode)
    {
        return FText::GetEmpty();
    }

    const int32 LinkedCount = ShotNode->GetLinkedPictureCount();
    return FText::FromString(LinkedCount > 0
        ? FString::Printf(TEXT("参考图 %d 张"), LinkedCount)
        : TEXT("还没接参考图"));
}

bool SShineVideoShotNode::HasExtraBody() const
{
    // 分镜节点始终显示这条："接了几张参考图"是提交前最该确认的一件事。
    return true;
}
