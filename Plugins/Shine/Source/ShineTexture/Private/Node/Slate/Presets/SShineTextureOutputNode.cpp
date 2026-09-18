#include "Node/Slate/Presets/SShineTextureOutputNode.h"

#include "Node/Presets/ShineTextureOutputNode.h"
#include "Node/Slate/ShineTextureSlateTheme.h"

void SShineTextureOutputNode::Construct(const FArguments& InArgs, UShineTextureOutputNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureOutputNode::CreateNodeContent() const
{
    return WrapNodeContent(SNullWidget::NullWidget, ShineTextureSlateTheme::NodeContentMinWidth());
}

