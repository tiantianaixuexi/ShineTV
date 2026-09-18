#include "Node/Slate/Presets/SShineTexturePreviewNode.h"

#include "Node/Presets/ShineTexturePreviewNode.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Widgets/SBoxPanel.h"

void SShineTexturePreviewNode::Construct(const FArguments& InArgs, UShineTexturePreviewNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTexturePreviewNode::CreateNodeContent() const
{
    return WrapNodeContent(SNullWidget::NullWidget, ShineTextureSlateTheme::PreviewNodeContentMinWidth());
}

UShineTexturePreviewNode* SShineTexturePreviewNode::GetPreviewNode() const
{
    return GraphNode ? Cast<UShineTexturePreviewNode>(GraphNode) : nullptr;
}
