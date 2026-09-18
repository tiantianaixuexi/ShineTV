#include "Node/Slate/Presets/SShineTextureScalarConstantNode.h"

#include "Node/Presets/Math/ShineTextureScalarConstantNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureScalarConstantNode::Construct(const FArguments& InArgs, UShineTextureScalarConstantNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureScalarConstantNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureScalarConstantNode", "ValueLabel", "Value"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetScalarNode() ? GetScalarNode()->GetValue() : 0.0f; },
                    [this](float NewValue)
                    {
                        if (UShineTextureScalarConstantNode* Node = GetScalarNode())
                        {
                            Node->SetValue(NewValue);
                        }
                    }),
                8.0f)
        ],
        176.0f);
}

UShineTextureScalarConstantNode* SShineTextureScalarConstantNode::GetScalarNode() const
{
    return GraphNode ? CastChecked<UShineTextureScalarConstantNode>(GraphNode) : nullptr;
}
