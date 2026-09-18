#pragma once

#include "CoreMinimal.h"
#include "ShineComfyGraphTypes.generated.h"

UENUM()
enum class EShineComfyParameterType : uint8
{
    Text,
    Float,
    Integer,
    Boolean
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyNodeParameter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    FName Name;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FText Label;

    UPROPERTY(EditAnywhere, Category = "Shine")
    EShineComfyParameterType Type = EShineComfyParameterType::Text;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString StringValue;

    UPROPERTY(EditAnywhere, Category = "Shine")
    double FloatValue = 0.0;

    UPROPERTY(EditAnywhere, Category = "Shine")
    int32 IntValue = 0;

    UPROPERTY(EditAnywhere, Category = "Shine")
    bool bBoolValue = false;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FString> StringOptions;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString OptionsSourceName;

    /**
     * 文本参数是否要在节点上用多行输入框。
     *
     * 只影响节点上的控件形状（提示词、剧本正文这类一段一段的文字必须能多行写），
     * 不改变取值与导出——导出永远是同一个 `StringValue`。
     */
    UPROPERTY(EditAnywhere, Category = "Shine")
    bool bMultiLine = false;

    FString ExportValueAsString() const
    {
        switch (Type)
        {
        case EShineComfyParameterType::Float:
            return FString::SanitizeFloat(FloatValue);
        case EShineComfyParameterType::Integer:
            return FString::FromInt(IntValue);
        case EShineComfyParameterType::Boolean:
            return bBoolValue ? TEXT("true") : TEXT("false");
        default:
            return StringValue;
        }
    }
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyNamedValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Name;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Value;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfySerializedNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    FGuid NodeId;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Title;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Preset;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FVector2D Position = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyNodeParameter> Parameters;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfySerializedLink
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    FGuid SourceNodeId;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString SourcePin;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FGuid TargetNodeId;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString TargetPin;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyGraphDocument
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfySerializedNode> Nodes;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfySerializedLink> Links;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyExecutionEdge
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    FGuid SourceNodeId;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString SourcePin;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FGuid TargetNodeId;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString TargetPin;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyExecutionNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    FGuid NodeId;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Title;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Preset;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyNamedValue> Parameters;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyNamedValue> Inputs;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyNamedValue> Outputs;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyExecutionPlan
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    bool bHasCycle = false;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString StatusMessage;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyExecutionEdge> Edges;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyExecutionNode> OrderedNodes;
};

USTRUCT()
struct SHINEEDITOR_API FShineComfyExecutionResult
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Shine")
    bool bSuccess = false;

    UPROPERTY(EditAnywhere, Category = "Shine")
    FString Summary;

    UPROPERTY(EditAnywhere, Category = "Shine")
    TArray<FShineComfyExecutionNode> ExecutedNodes;
};