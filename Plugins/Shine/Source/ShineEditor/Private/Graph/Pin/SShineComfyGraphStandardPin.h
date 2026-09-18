#pragma once

#include "CoreMinimal.h"
#include "Graph/Pin/SShineComfyGraphPinBase.h"

class SShineComfyGraphStandardPin : public SShineComfyGraphPinBase
{
public:
    SLATE_BEGIN_ARGS(SShineComfyGraphStandardPin) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
    virtual FSlateColor ResolvePinColor() const override;
};