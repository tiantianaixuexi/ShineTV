#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

class SShineComfyGraphPinBase : public SGraphPin
{
public:
    SLATE_BEGIN_ARGS(SShineComfyGraphPinBase) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
    virtual TSharedRef<SWidget> GetDefaultValueWidget() override;
    virtual FSlateColor GetPinColor() const override;
    virtual FSlateColor ResolvePinColor() const;
};