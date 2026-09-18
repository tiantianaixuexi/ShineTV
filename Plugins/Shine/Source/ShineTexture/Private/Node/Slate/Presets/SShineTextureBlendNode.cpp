#include "Node/Slate/Presets/SShineTextureBlendNode.h"

#include "Node/Presets/ShineTextureBlendNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureBlendNode::Construct(const FArguments& InArgs, UShineTextureBlendNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureBlendNode::CreateNodeContent() const
{
    const UShineTextureBlendNode* BlendNode = GetBlendNode();
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureBlendNode", "BlendFactorLabel", "混合系数 (Blend Factor)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetBlendNode() ? GetBlendNode()->GetBlendFactor() : 0.0f; },
                    [this](float NewValue) { if (GetBlendNode()) { GetBlendNode()->SetBlendFactor(NewValue); } }))
        ]);
}

UShineTextureBlendNode* SShineTextureBlendNode::GetBlendNode() const
{
    return GraphNode ? CastChecked<UShineTextureBlendNode>(GraphNode) : nullptr;
}

