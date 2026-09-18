#include "Graph/Execution/ShineComfyExecutionEvaluator.h"

namespace
{
    const FString UnresolvedValue(TEXT("<unresolved>"));

    void AddOrUpdateNamedValue(TArray<FShineComfyNamedValue>& Values, const FString& Name, const FString& Value)
    {
        for (FShineComfyNamedValue& ExistingValue : Values)
        {
            if (ExistingValue.Name == Name)
            {
                ExistingValue.Value = Value;
                return;
            }
        }

        FShineComfyNamedValue& NewValue = Values.AddDefaulted_GetRef();
        NewValue.Name = Name;
        NewValue.Value = Value;
    }

    FString FindNamedValue(const TArray<FShineComfyNamedValue>& Values, const FString& Name, const FString& DefaultValue = FString())
    {
        for (const FShineComfyNamedValue& Value : Values)
        {
            if (Value.Name == Name)
            {
                return Value.Value;
            }
        }

        return DefaultValue;
    }

    void EvaluatePromptNode(const FShineComfySerializedNode&, FShineComfyExecutionNode& ExecutionNode)
    {
        const FString PositivePrompt = FindNamedValue(ExecutionNode.Parameters, TEXT("PositivePrompt"));
        const FString NegativePrompt = FindNamedValue(ExecutionNode.Parameters, TEXT("NegativePrompt"));

        AddOrUpdateNamedValue(ExecutionNode.Outputs, TEXT("Conditioning"), FString::Printf(TEXT("cond(%s)"), *PositivePrompt));
        AddOrUpdateNamedValue(ExecutionNode.Outputs, TEXT("Negative"), FString::Printf(TEXT("cond(%s)"), *NegativePrompt));
    }

    void EvaluateSamplerNode(const FShineComfySerializedNode&, FShineComfyExecutionNode& ExecutionNode)
    {
        const FString Conditioning = FindNamedValue(ExecutionNode.Inputs, TEXT("Conditioning"), UnresolvedValue);
        const FString Negative = FindNamedValue(ExecutionNode.Inputs, TEXT("Negative"), UnresolvedValue);
        const FString Steps = FindNamedValue(ExecutionNode.Parameters, TEXT("Steps"), TEXT("20"));
        const FString CfgScale = FindNamedValue(ExecutionNode.Parameters, TEXT("CfgScale"), TEXT("7.0"));
        const FString Seed = FindNamedValue(ExecutionNode.Parameters, TEXT("Seed"), TEXT("42"));
        const FString FixedSeed = FindNamedValue(ExecutionNode.Parameters, TEXT("UseFixedSeed"), TEXT("false"));

        AddOrUpdateNamedValue(
            ExecutionNode.Outputs,
            TEXT("Latent"),
            FString::Printf(
                TEXT("latent[steps=%s cfg=%s seed=%s fixed=%s pos=%s neg=%s]"),
                *Steps,
                *CfgScale,
                *Seed,
                *FixedSeed,
                *Conditioning,
                *Negative));
    }

    void EvaluatePreviewNode(const FShineComfySerializedNode&, FShineComfyExecutionNode& ExecutionNode)
    {
        const FString Latent = FindNamedValue(ExecutionNode.Inputs, TEXT("Latent"), UnresolvedValue);
        const FString OutputLabel = FindNamedValue(ExecutionNode.Parameters, TEXT("OutputLabel"), TEXT("Preview"));
        const FString AutoRefresh = FindNamedValue(ExecutionNode.Parameters, TEXT("AutoRefresh"), TEXT("true"));

        AddOrUpdateNamedValue(
            ExecutionNode.Outputs,
            TEXT("Image"),
            FString::Printf(TEXT("%s <= %s (%s)"), *OutputLabel, *Latent, *AutoRefresh));
    }

    void EvaluateMultiImageGalleryNode(const FShineComfySerializedNode&, FShineComfyExecutionNode& ExecutionNode)
    {
        const FString Latent = FindNamedValue(ExecutionNode.Inputs, TEXT("Latent"), UnresolvedValue);
        const FString Caption = FindNamedValue(ExecutionNode.Parameters, TEXT("Caption"), TEXT("Gallery Preview"));
        const FString ImageCount = FindNamedValue(ExecutionNode.Parameters, TEXT("ImageCount"), TEXT("6"));
        const FString Columns = FindNamedValue(ExecutionNode.Parameters, TEXT("Columns"), TEXT("3"));

        AddOrUpdateNamedValue(
            ExecutionNode.Outputs,
            TEXT("Image"),
            FString::Printf(TEXT("%s <= first(%s)"), *Caption, *Latent));
        AddOrUpdateNamedValue(
            ExecutionNode.Outputs,
            TEXT("Images"),
            FString::Printf(TEXT("gallery[count=%s columns=%s source=%s]"), *ImageCount, *Columns, *Latent));
    }

    void EvaluateDirectorNode(const FShineComfySerializedNode& SerializedNode, FShineComfyExecutionNode& ExecutionNode)
    {
        const FString Scene = FindNamedValue(ExecutionNode.Parameters, TEXT("SceneDescription"), TEXT(""));
        const FString Camera = FindNamedValue(ExecutionNode.Parameters, TEXT("CameraAngle"), TEXT(""));
        const FString Lighting = FindNamedValue(ExecutionNode.Parameters, TEXT("Lighting"), TEXT(""));
        const FString ArtStyle = FindNamedValue(ExecutionNode.Parameters, TEXT("ArtStyle"), TEXT(""));
        const FString NegativePrompt = FindNamedValue(ExecutionNode.Parameters, TEXT("NegativePrompt"), TEXT(""));

        // 模拟 CLIPTextEncode 输出
        const FString CombinedPrompt = FString::Printf(TEXT("%s, %s, %s, %s"), *Scene, *Camera, *Lighting, *ArtStyle);
        AddOrUpdateNamedValue(ExecutionNode.Outputs, TEXT("Conditioning"), FString::Printf(TEXT("cond(%s)"), *CombinedPrompt));
        AddOrUpdateNamedValue(ExecutionNode.Outputs, TEXT("Negative"), FString::Printf(TEXT("cond(%s)"), *NegativePrompt));
    }

    void EvaluateFallbackNode(const FShineComfySerializedNode& SerializedNode, FShineComfyExecutionNode& ExecutionNode)
    {
        AddOrUpdateNamedValue(ExecutionNode.Outputs, TEXT("Result"), SerializedNode.Title);
    }
}

void ShineComfyExecutionEvaluator::EvaluateNode(const FShineComfySerializedNode& SerializedNode, FShineComfyExecutionNode& ExecutionNode)
{
    const FString& Preset = SerializedNode.Preset;

    if (Preset == TEXT("Prompt"))
    {
        EvaluatePromptNode(SerializedNode, ExecutionNode);
        return;
    }

    if (Preset == TEXT("Sampler"))
    {
        EvaluateSamplerNode(SerializedNode, ExecutionNode);
        return;
    }

    if (Preset == TEXT("Preview"))
    {
        EvaluatePreviewNode(SerializedNode, ExecutionNode);
        return;
    }

    if (Preset == TEXT("MultiImageGallery"))
    {
        EvaluateMultiImageGalleryNode(SerializedNode, ExecutionNode);
        return;
    }

    if (Preset == TEXT("Director"))
    {
        EvaluateDirectorNode(SerializedNode, ExecutionNode);
        return;
    }

    EvaluateFallbackNode(SerializedNode, ExecutionNode);
}