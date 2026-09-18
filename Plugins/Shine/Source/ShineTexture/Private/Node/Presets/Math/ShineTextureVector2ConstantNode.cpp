#include "Node/Presets/Math/ShineTextureVector2ConstantNode.h"

#include "Node/Slate/Presets/SShineTextureVector2ConstantNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureVector2ConstantNode::CreateVisualWidget()
{
    return SNew(SShineTextureVector2ConstantNode, this);
}

UShineTextureVector2ConstantNode::UShineTextureVector2ConstantNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureVector2ConstantNode", "Vector2ConstantTitle", "Vector2"),
        NSLOCTEXT("UShineTextureVector2ConstantNode", "Vector2ConstantSubtitle", ""),
        FLinearColor(0.220f, 0.720f, 0.900f, 1.0f));
}

FVector2D UShineTextureVector2ConstantNode::GetValue() const
{
    return Value;
}

void UShineTextureVector2ConstantNode::SetValue(FVector2D InValue)
{
    if (Value.Equals(InValue, KINDA_SMALL_NUMBER))
    {
        return;
    }

    Modify();
    Value = InValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureVector2ConstantNode::SetValueX(float InValueX)
{
    SetValue(FVector2D(InValueX, Value.Y));
}

void UShineTextureVector2ConstantNode::SetValueY(float InValueY)
{
    SetValue(FVector2D(Value.X, InValueY));
}

void UShineTextureVector2ConstantNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, Vector2PinCategory, TEXT("Value"));
}

