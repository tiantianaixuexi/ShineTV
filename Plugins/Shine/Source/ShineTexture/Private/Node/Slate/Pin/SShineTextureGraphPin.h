#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

class SShineTextureGraphPin : public SGraphPin
{
public:
    SLATE_BEGIN_ARGS(SShineTextureGraphPin) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
    virtual TSharedRef<SWidget> GetDefaultValueWidget() override;
    virtual const FSlateBrush* GetPinIcon() const override;
    virtual FSlateColor GetPinColor() const override;
    virtual FSlateColor GetPinTextColor() const override;
    virtual FSlateColor GetHighlightColor() const override;
};