#include "Node/Slate/Presets/SShineTextureVector2ConstantNode.h"

#include "Node/Presets/Math/ShineTextureVector2ConstantNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureVector2ConstantNode::Construct(const FArguments& InArgs, UShineTextureVector2ConstantNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureVector2ConstantNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureVector2ConstantNode", "ComponentsLabel", "Components"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetVector2Node() ? GetVector2Node()->GetValue().X : 0.0f; },
                    [this](float NewValue)
                    {
                        if (UShineTextureVector2ConstantNode* Node = GetVector2Node())
                        {
                            Node->SetValueX(NewValue);
                        }
                    },
                    FShineTextureFloatEntryOptions(),
                    CreateSectionLabel(NSLOCTEXT("SShineTextureVector2ConstantNode", "ComponentXLabel", "X"))),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetVector2Node() ? GetVector2Node()->GetValue().Y : 0.0f; },
                    [this](float NewValue)
                    {
                        if (UShineTextureVector2ConstantNode* Node = GetVector2Node())
                        {
                            Node->SetValueY(NewValue);
                        }
                    },
                    FShineTextureFloatEntryOptions(),
                    CreateSectionLabel(NSLOCTEXT("SShineTextureVector2ConstantNode", "ComponentYLabel", "Y"))),
                8.0f)
        ],
        196.0f);
}

UShineTextureVector2ConstantNode* SShineTextureVector2ConstantNode::GetVector2Node() const
{
    return GraphNode ? CastChecked<UShineTextureVector2ConstantNode>(GraphNode) : nullptr;
}
