#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"

#include "EdGraph/EdGraphPin.h"

UShineVideoGraphNodeBase::UShineVideoGraphNodeBase()
{
    AccentColor = FLinearColor(0.13f, 0.61f, 0.58f, 1.0f);
}

void UShineVideoGraphNodeBase::SetRuntimeState(EShineVideoNodeRuntimeState InState, const FString& InDetail, int32 InStep, int32 InStepMax)
{
    RuntimeState = InState;
    RuntimeDetail = InDetail;
    RuntimeStep = InStep;
    RuntimeStepMax = InStepMax;
}

FLinearColor UShineVideoGraphNodeBase::GetStateColor(EShineVideoNodeRuntimeState State)
{
    switch (State)
    {
    case EShineVideoNodeRuntimeState::Pending:
        return FLinearColor(0.55f, 0.58f, 0.62f, 1.0f);
    case EShineVideoNodeRuntimeState::Running:
        return FLinearColor(0.98f, 0.75f, 0.22f, 1.0f);
    case EShineVideoNodeRuntimeState::Finished:
        return FLinearColor(0.26f, 0.82f, 0.48f, 1.0f);
    case EShineVideoNodeRuntimeState::Failed:
        return FLinearColor(0.90f, 0.32f, 0.30f, 1.0f);
    default:
        return FLinearColor(0.28f, 0.30f, 0.34f, 1.0f);
    }
}

void UShineVideoGraphNodeBase::AddTextParameter(FName ParameterName, const FString& Label, const FString& Value, bool bMultiLine)
{
    FShineComfyNodeParameter& Parameter = Parameters.AddDefaulted_GetRef();
    Parameter.Name = ParameterName;
    Parameter.Label = FText::FromString(Label);
    Parameter.Type = EShineComfyParameterType::Text;
    Parameter.StringValue = Value;
    Parameter.bMultiLine = bMultiLine;
}

void UShineVideoGraphNodeBase::AddOptionParameter(FName ParameterName, const FString& Label, const FString& Value, const TArray<FString>& Options)
{
    FShineComfyNodeParameter& Parameter = Parameters.AddDefaulted_GetRef();
    Parameter.Name = ParameterName;
    Parameter.Label = FText::FromString(Label);
    Parameter.Type = EShineComfyParameterType::Text;
    Parameter.StringValue = Value;
    Parameter.StringOptions = Options;
}

void UShineVideoGraphNodeBase::AddFloatParameter(FName ParameterName, const FString& Label, double Value)
{
    FShineComfyNodeParameter& Parameter = Parameters.AddDefaulted_GetRef();
    Parameter.Name = ParameterName;
    Parameter.Label = FText::FromString(Label);
    Parameter.Type = EShineComfyParameterType::Float;
    Parameter.FloatValue = Value;
}

void UShineVideoGraphNodeBase::AddIntegerParameter(FName ParameterName, const FString& Label, int32 Value)
{
    FShineComfyNodeParameter& Parameter = Parameters.AddDefaulted_GetRef();
    Parameter.Name = ParameterName;
    Parameter.Label = FText::FromString(Label);
    Parameter.Type = EShineComfyParameterType::Integer;
    Parameter.IntValue = Value;
}

void UShineVideoGraphNodeBase::AddBoolParameter(FName ParameterName, const FString& Label, bool bValue)
{
    FShineComfyNodeParameter& Parameter = Parameters.AddDefaulted_GetRef();
    Parameter.Name = ParameterName;
    Parameter.Label = FText::FromString(Label);
    Parameter.Type = EShineComfyParameterType::Boolean;
    Parameter.bBoolValue = bValue;
}


