#include "Node/Slate/Presets/SShineTextureBlurNode.h"

#include "Node/Presets/ShineTextureBlurNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureBlurNode::Construct(const FArguments& InArgs, UShineTextureBlurNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureBlurNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureBlurNode", "RadiusLabel", "半径"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetBlurNode() ? GetBlurNode()->GetRadius() : 0.0f; },
                    [this](float NewValue) { if (GetBlurNode()) { GetBlurNode()->SetRadius(NewValue); } }),
                8.0f)
        ],
        180.0f);
}

UShineTextureBlurNode* SShineTextureBlurNode::GetBlurNode() const
{
    return GraphNode ? CastChecked<UShineTextureBlurNode>(GraphNode) : nullptr;
}
