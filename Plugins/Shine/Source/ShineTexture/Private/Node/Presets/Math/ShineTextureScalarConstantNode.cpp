#include "Node/Presets/Math/ShineTextureScalarConstantNode.h"

#include "Node/Slate/Presets/SShineTextureScalarConstantNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureScalarConstantNode::CreateVisualWidget()
{
    return SNew(SShineTextureScalarConstantNode, this);
}

UShineTextureScalarConstantNode::UShineTextureScalarConstantNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureScalarConstantNode", "ScalarConstantTitle", "Scalar"),
        NSLOCTEXT("UShineTextureScalarConstantNode", "ScalarConstantSubtitle", "Outputs a single scalar constant value."),
        FLinearColor(0.720f, 0.720f, 0.720f, 1.0f));
}

float UShineTextureScalarConstantNode::GetValue() const
{
    return Value;
}

void UShineTextureScalarConstantNode::SetValue(float InValue)
{
    if (FMath::IsNearlyEqual(Value, InValue))
    {
        return;
    }

    Modify();
    Value = InValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureScalarConstantNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ScalarPinCategory, TEXT("Value"));
}

