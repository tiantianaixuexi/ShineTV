#include "Graph/ShineComfyConnectionDrawingPolicy.h"

#include "EdGraph/EdGraphPin.h"

FShineComfyConnectionDrawingPolicy::FShineComfyConnectionDrawingPolicy(
    int32 InBackLayerID,
    int32 InFrontLayerID,
    float InZoomFactor,
    const FSlateRect& InClippingRect,
    FSlateWindowElementList& InDrawElements)
    : FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
{
}

void FShineComfyConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params)
{
    FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);

    const UEdGraphPin* ReferencePin = OutputPin ? OutputPin : InputPin;
    Params.WireColor = ResolvePinColor(ReferencePin);
    Params.WireThickness = (OutputPin && InputPin) ? 4.0f : 2.8f;
    Params.bDrawBubbles = true;
}

FLinearColor FShineComfyConnectionDrawingPolicy::ResolvePinColor(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return FLinearColor::White;
    }

    const FName Category = Pin->PinType.PinCategory;
    if (Category == TEXT("Shine.Conditioning"))
    {
        return FLinearColor(0.26f, 0.82f, 0.48f, 0.95f);
    }

    if (Category == TEXT("Shine.Latent"))
    {
        return FLinearColor(0.98f, 0.63f, 0.21f, 0.95f);
    }

    if (Category == TEXT("Shine.Image"))
    {
        return FLinearColor(0.31f, 0.66f, 0.97f, 0.95f);
    }

    if (Category == TEXT("Shine.Integer"))
    {
        return FLinearColor(0.86f, 0.87f, 0.91f, 0.95f);
    }

    // 视频工作台那套 pin（ShineVideoGraphTypes.h）。连线颜色必须和 pin 上的颜色一致，
    // 否则"这根线是什么"就变成了两种说法。
    if (Category == TEXT("Shine.Video.Image"))
    {
        return FLinearColor(0.31f, 0.66f, 0.97f, 0.95f);
    }

    if (Category == TEXT("Shine.Video.Picture"))
    {
        return FLinearColor(0.24f, 0.78f, 0.86f, 0.95f);
    }

    if (Category == TEXT("Shine.Video.Character"))
    {
        return FLinearColor(0.91f, 0.54f, 0.20f, 0.95f);
    }

    if (Category == TEXT("Shine.Video.Shot"))
    {
        return FLinearColor(0.24f, 0.60f, 0.86f, 0.95f);
    }

    if (Category == TEXT("Shine.Video.Video"))
    {
        return FLinearColor(0.68f, 0.44f, 0.90f, 0.95f);
    }

    if (Category == TEXT("Shine.Video.Script"))
    {
        return FLinearColor(0.50f, 0.46f, 0.76f, 0.95f);
    }

    return FLinearColor(0.74f, 0.76f, 0.80f, 0.95f);
}