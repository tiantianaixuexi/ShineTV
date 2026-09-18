#include "Graph/Node/ShineComfyGraphPresetNode.h"

namespace
{
    FShineComfyNodeParameter MakeTextParameter(const FName& Name, const TCHAR* Label, const TCHAR* DefaultValue)
    {
        FShineComfyNodeParameter Parameter;
        Parameter.Name = Name;
        Parameter.Label = FText::FromString(Label);
        Parameter.Type = EShineComfyParameterType::Text;
        Parameter.StringValue = DefaultValue;
        return Parameter;
    }

    FShineComfyNodeParameter MakeFloatParameter(const FName& Name, const TCHAR* Label, double DefaultValue)
    {
        FShineComfyNodeParameter Parameter;
        Parameter.Name = Name;
        Parameter.Label = FText::FromString(Label);
        Parameter.Type = EShineComfyParameterType::Float;
        Parameter.FloatValue = DefaultValue;
        return Parameter;
    }

    FShineComfyNodeParameter MakeIntegerParameter(const FName& Name, const TCHAR* Label, int32 DefaultValue)
    {
        FShineComfyNodeParameter Parameter;
        Parameter.Name = Name;
        Parameter.Label = FText::FromString(Label);
        Parameter.Type = EShineComfyParameterType::Integer;
        Parameter.IntValue = DefaultValue;
        return Parameter;
    }

    FShineComfyNodeParameter MakeBoolParameter(const FName& Name, const TCHAR* Label, bool bDefaultValue)
    {
        FShineComfyNodeParameter Parameter;
        Parameter.Name = Name;
        Parameter.Label = FText::FromString(Label);
        Parameter.Type = EShineComfyParameterType::Boolean;
        Parameter.bBoolValue = bDefaultValue;
        return Parameter;
    }
}

UShineComfyGraphPresetNode::UShineComfyGraphPresetNode()
    : NodePreset(NAME_None)
{
}

FName UShineComfyGraphPresetNode::GetNodePreset() const
{
    return NodePreset;
}

void UShineComfyGraphPresetNode::SetPresetName(FName InNodePreset)
{
    NodePreset = InNodePreset;
}

void UShineComfyGraphPresetNode::ResetPresetParameters()
{
    Parameters.Reset();
}

void UShineComfyGraphPresetNode::AddTextParameter(const FName& Name, const TCHAR* Label, const TCHAR* DefaultValue)
{
    Parameters.Add(MakeTextParameter(Name, Label, DefaultValue));
}

void UShineComfyGraphPresetNode::AddFloatParameter(const FName& Name, const TCHAR* Label, double DefaultValue)
{
    Parameters.Add(MakeFloatParameter(Name, Label, DefaultValue));
}

void UShineComfyGraphPresetNode::AddIntegerParameter(const FName& Name, const TCHAR* Label, int32 DefaultValue)
{
    Parameters.Add(MakeIntegerParameter(Name, Label, DefaultValue));
}

void UShineComfyGraphPresetNode::AddBoolParameter(const FName& Name, const TCHAR* Label, bool bDefaultValue)
{
    Parameters.Add(MakeBoolParameter(Name, Label, bDefaultValue));
}