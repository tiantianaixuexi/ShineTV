#include "Node/Presets/Math/ShineTextureStepNode.h"

UShineTextureStepNode::UShineTextureStepNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureStepNode", "StepTitle", "Step"),
        NSLOCTEXT("UShineTextureStepNode", "StepSubtitle", "Outputs true when Value is greater than or equal to Edge."),
        FLinearColor(0.680f, 0.760f, 0.280f, 1.0f));
}

void UShineTextureStepNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Edge"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Value"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

