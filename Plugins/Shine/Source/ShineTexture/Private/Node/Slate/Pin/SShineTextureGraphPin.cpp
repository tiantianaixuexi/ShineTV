#include "Node/Slate/Pin/SShineTextureGraphPin.h"

#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Widgets/SNullWidget.h"

void SShineTextureGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
    SGraphPin::Construct(
        SGraphPin::FArguments()
        .PinLabelStyle(NAME_DefaultPinLabelStyle)
        .SideToSideMargin(8.0f),
        InPin);
}

TSharedRef<SWidget> SShineTextureGraphPin::GetDefaultValueWidget()
{
    return SNullWidget::NullWidget;
}

const FSlateBrush* SShineTextureGraphPin::GetPinIcon() const
{
    return (IsConnected() || IsHovered())
        ? ShineTextureSlateTheme::GetPinConnectedBrush()
        : ShineTextureSlateTheme::GetPinDisconnectedBrush();
}

FSlateColor SShineTextureGraphPin::GetPinColor() const
{
    return ShineTextureSlateTheme::ResolvePinColor(GetPinObj());
}

FSlateColor SShineTextureGraphPin::GetPinTextColor() const
{
    return IsConnected() ? ShineTextureSlateTheme::TextMain() : ShineTextureSlateTheme::TextSecondary();
}

FSlateColor SShineTextureGraphPin::GetHighlightColor() const
{
    return IsHovered()
        ? ShineTextureSlateTheme::ResolvePinColor(GetPinObj()).CopyWithNewOpacity(0.16f)
        : FLinearColor::Transparent;
}
