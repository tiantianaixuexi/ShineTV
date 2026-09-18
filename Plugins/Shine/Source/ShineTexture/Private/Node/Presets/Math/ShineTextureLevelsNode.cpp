#include "Node/Presets/Math/ShineTextureLevelsNode.h"

#include "Node/Slate/Presets/SShineTextureLevelsNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureLevelsNode::CreateVisualWidget()
{
    return SNew(SShineTextureLevelsNode, this);
}

UShineTextureLevelsNode::UShineTextureLevelsNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureLevelsNode", "LevelsTitle", "色阶 (Levels)"),
        NSLOCTEXT("UShineTextureLevelsNode", "LevelsSubtitle", "Remaps input range, gamma, and output range."),
        FLinearColor(0.910f, 0.840f, 0.330f, 1.0f));
}

float UShineTextureLevelsNode::GetInputLow() const
{
    return InputLow;
}

float UShineTextureLevelsNode::GetInputHigh() const
{
    return InputHigh;
}

float UShineTextureLevelsNode::GetGamma() const
{
    return Gamma;
}

float UShineTextureLevelsNode::GetOutputLow() const
{
    return OutputLow;
}

float UShineTextureLevelsNode::GetOutputHigh() const
{
    return OutputHigh;
}

void UShineTextureLevelsNode::SetInputLow(float InValue)
{
    const float NewValue = FMath::Clamp(InValue, 0.0f, InputHigh);
    if (FMath::IsNearlyEqual(InputLow, NewValue))
    {
        return;
    }

    Modify();
    InputLow = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureLevelsNode::SetInputHigh(float InValue)
{
    const float NewValue = FMath::Clamp(InValue, InputLow, 1.0f);
    if (FMath::IsNearlyEqual(InputHigh, NewValue))
    {
        return;
    }

    Modify();
    InputHigh = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureLevelsNode::SetGamma(float InValue)
{
    const float NewValue = FMath::Clamp(InValue, 0.1f, 4.0f);
    if (FMath::IsNearlyEqual(Gamma, NewValue))
    {
        return;
    }

    Modify();
    Gamma = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureLevelsNode::SetOutputLow(float InValue)
{
    const float NewValue = FMath::Clamp(InValue, 0.0f, OutputHigh);
    if (FMath::IsNearlyEqual(OutputLow, NewValue))
    {
        return;
    }

    Modify();
    OutputLow = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureLevelsNode::SetOutputHigh(float InValue)
{
    const float NewValue = FMath::Clamp(InValue, OutputLow, 1.0f);
    if (FMath::IsNearlyEqual(OutputHigh, NewValue))
    {
        return;
    }

    Modify();
    OutputHigh = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureLevelsNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

