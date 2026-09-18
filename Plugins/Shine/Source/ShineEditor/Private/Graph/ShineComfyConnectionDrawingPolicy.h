#pragma once

#include "ConnectionDrawingPolicy.h"

class FShineComfyConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
    FShineComfyConnectionDrawingPolicy(
        int32 InBackLayerID,
        int32 InFrontLayerID,
        float InZoomFactor,
        const FSlateRect& InClippingRect,
        FSlateWindowElementList& InDrawElements);

    virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params) override;

private:
    static FLinearColor ResolvePinColor(const UEdGraphPin* Pin);
};