#include "Graph/Pin/SShineComfyGraphPinBase.h"

#include "Widgets/SNullWidget.h"

void SShineComfyGraphPinBase::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
    SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

TSharedRef<SWidget> SShineComfyGraphPinBase::GetDefaultValueWidget()
{
    return SNullWidget::NullWidget;
}

FSlateColor SShineComfyGraphPinBase::GetPinColor() const
{
    return ResolvePinColor();
}

FSlateColor SShineComfyGraphPinBase::ResolvePinColor() const
{
    return FLinearColor::Gray;
}