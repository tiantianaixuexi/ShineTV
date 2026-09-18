#include "Graph/Slate/SShineComfyGraphStandardNode.h"

#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Pin/SShineComfyGraphStandardPin.h"
#include "SGraphPin.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TOptional<float> MakeOptionalFloat(float Value)
    {
        return TOptional<float>(Value);
    }

    TOptional<int32> MakeOptionalInt(int32 Value)
    {
        return TOptional<int32>(Value);
    }
}

void SShineComfyGraphStandardNode::Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode)
{
    ConstructBase(InNode);
    SetCursor(EMouseCursor::CardinalCross);
    UpdateGraphNode();
}

void SShineComfyGraphStandardNode::UpdateGraphNode()
{
    ResetNodeContainers();

    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FLinearColor AccentColor = ShineNode ? ShineNode->GetNodeTitleColor() : FLinearColor::Gray;

    SetupErrorReporting();
    ContentScale.Bind(this, &SGraphNode::GetContentScale);

    GetOrAddSlot(ENodeZone::Center)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.08f, 1.0f))
        .Padding(1.0f)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.10f, 0.11f, 0.14f, 1.0f))
            .Padding(0.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(AccentColor)
                    .Padding(FMargin(14.0f, 10.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(ShineNode ? ShineNode->GetNodeTitle(ENodeTitleType::ListView) : FText::GetEmpty())
                            .ColorAndOpacity(FLinearColor::White)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(ShineNode ? ShineNode->GetNodeSubtitle() : FText::GetEmpty())
                            .ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.85f))
                            .WrapTextAt(240.0f)
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    BuildExtraBodyWidget()
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.09f, 0.10f, 0.13f, 1.0f))
                    .Padding(FMargin(10.0f, 8.0f))
                    [
                        SAssignNew(ParameterBox, SVerticalBox)
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.08f, 0.09f, 0.12f, 1.0f))
                    .Padding(FMargin(10.0f, 8.0f))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        [
                            SAssignNew(LeftNodeBox, SVerticalBox)
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(8.0f, 0.0f)
                        [
                            SNew(SBox)
                            .WidthOverride(18.0f)
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        [
                            SAssignNew(RightNodeBox, SVerticalBox)
                        ]
                    ]
                ]
            ]
        ]
    ];

    PopulateParameterWidgets();
    CreatePinWidgets();
}

void SShineComfyGraphStandardNode::PopulateParameterWidgets()
{
    if (!ParameterBox.IsValid())
    {
        return;
    }

    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    if (!ShineNode)
    {
        return;
    }

    ParameterOptionItems.Reset();

    for (const FShineComfyNodeParameter& Parameter : ShineNode->GetParameters())
    {
        ParameterBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [
            CreateParameterWidget(Parameter)
        ];
    }
}

TSharedRef<SWidget> SShineComfyGraphStandardNode::BuildExtraBodyWidget()
{
    return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SShineComfyGraphStandardNode::CreateParameterWidget(const FShineComfyNodeParameter& Parameter)
{
    // 多行文本（提示词、剧本正文…）用另一种排版：标签在上、框在下、宽度占满。
    // 挤在右边那个 140px 的窄格里写一整段提示词是没法用的。
    if (Parameter.Type == EShineComfyParameterType::Text && Parameter.bMultiLine)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 3.0f)
            [
                SNew(STextBlock)
                .Text(Parameter.Label)
                .ColorAndOpacity(FLinearColor(0.84f, 0.86f, 0.90f, 1.0f))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBox)
                .WidthOverride(320.0f)
                .MinDesiredHeight(72.0f)
                [
                    SNew(SMultiLineEditableTextBox)
                    .Text(this, &SShineComfyGraphStandardNode::GetTextParameterValue, Parameter.Name)
                    .OnTextCommitted(this, &SShineComfyGraphStandardNode::HandleTextParameterCommitted, Parameter.Name)
                    .AutoWrapText(true)
                    .AlwaysShowScrollbars(false)
                ]
            ];
    }

    TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;

    switch (Parameter.Type)
    {
    case EShineComfyParameterType::Float:
        ValueWidget = SNew(SSpinBox<float>)
            .Delta(this, &SShineComfyGraphStandardNode::GetFloatDelta, Parameter.Name)
            .MinValue(this, &SShineComfyGraphStandardNode::GetFloatMinValue, Parameter.Name)
            .MaxValue(this, &SShineComfyGraphStandardNode::GetFloatMaxValue, Parameter.Name)
            .MinSliderValue(this, &SShineComfyGraphStandardNode::GetFloatSliderMinValue, Parameter.Name)
            .MaxSliderValue(this, &SShineComfyGraphStandardNode::GetFloatSliderMaxValue, Parameter.Name)
            .MinDesiredWidth(92.0f)
            .MaxFractionalDigits(4)
            .Value(this, &SShineComfyGraphStandardNode::GetFloatParameterValue, Parameter.Name)
            .OnValueChanged(this, &SShineComfyGraphStandardNode::HandleFloatParameterChanged, Parameter.Name)
            .OnValueCommitted(this, &SShineComfyGraphStandardNode::HandleFloatParameterCommitted, Parameter.Name);
        break;
    case EShineComfyParameterType::Integer:
        ValueWidget = SNew(SSpinBox<int32>)
            .Delta(this, &SShineComfyGraphStandardNode::GetIntegerDelta, Parameter.Name)
            .MinValue(this, &SShineComfyGraphStandardNode::GetIntegerMinValue, Parameter.Name)
            .MaxValue(this, &SShineComfyGraphStandardNode::GetIntegerMaxValue, Parameter.Name)
            .MinSliderValue(this, &SShineComfyGraphStandardNode::GetIntegerSliderMinValue, Parameter.Name)
            .MaxSliderValue(this, &SShineComfyGraphStandardNode::GetIntegerSliderMaxValue, Parameter.Name)
            .MinDesiredWidth(92.0f)
            .Value(this, &SShineComfyGraphStandardNode::GetIntegerParameterValue, Parameter.Name)
            .OnValueChanged(this, &SShineComfyGraphStandardNode::HandleIntegerParameterChanged, Parameter.Name)
            .OnValueCommitted(this, &SShineComfyGraphStandardNode::HandleIntegerParameterCommitted, Parameter.Name);
        break;
    case EShineComfyParameterType::Boolean:
        ValueWidget = SNew(SCheckBox)
            .IsChecked(this, &SShineComfyGraphStandardNode::GetBoolParameterValue, Parameter.Name)
            .OnCheckStateChanged(this, &SShineComfyGraphStandardNode::HandleBoolParameterChanged, Parameter.Name);
        break;
    default:
        if (Parameter.StringOptions.Num() > 0)
        {
            TArray<TSharedPtr<FString>>& OptionItems = GetOrBuildTextOptions(Parameter.Name);
            ValueWidget = SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&OptionItems)
                .InitiallySelectedItem(GetSelectedTextOption(Parameter.Name))
                .OnGenerateWidget(this, &SShineComfyGraphStandardNode::GenerateTextOptionWidget)
                .OnSelectionChanged(this, &SShineComfyGraphStandardNode::HandleTextParameterSelectionChanged, Parameter.Name)
                [
                    SNew(STextBlock)
                    .Text(this, &SShineComfyGraphStandardNode::GetTextParameterValue, Parameter.Name)
                ];
        }
        else
        {
            ValueWidget = SNew(SEditableTextBox)
                .Text(this, &SShineComfyGraphStandardNode::GetTextParameterValue, Parameter.Name)
                .OnTextCommitted(this, &SShineComfyGraphStandardNode::HandleTextParameterCommitted, Parameter.Name);
        }
        break;
    }

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        .Padding(0.0f, 2.0f, 8.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(Parameter.Label)
            .ColorAndOpacity(FLinearColor(0.84f, 0.86f, 0.90f, 1.0f))
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Center)
        [
            SNew(SBox)
            .WidthOverride(Parameter.Type == EShineComfyParameterType::Boolean ? 20.0f : 140.0f)
            .HAlign(HAlign_Right)
            [
                ValueWidget
            ]
        ];
}

TArray<TSharedPtr<FString>>& SShineComfyGraphStandardNode::GetOrBuildTextOptions(FName ParameterName)
{
    if (TArray<TSharedPtr<FString>>* ExistingOptions = ParameterOptionItems.Find(ParameterName))
    {
        return *ExistingOptions;
    }

    TArray<TSharedPtr<FString>>& OptionItems = ParameterOptionItems.Add(ParameterName);
    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FShineComfyNodeParameter* Parameter = ShineNode ? ShineNode->FindParameter(ParameterName) : nullptr;
    if (Parameter)
    {
        for (const FString& OptionValue : Parameter->StringOptions)
        {
            OptionItems.Add(MakeShared<FString>(OptionValue));
        }
    }

    return OptionItems;
}

TSharedRef<SWidget> SShineComfyGraphStandardNode::GenerateTextOptionWidget(TSharedPtr<FString> Option) const
{
    return SNew(STextBlock)
        .Text(FText::FromString(Option.IsValid() ? *Option : FString()));
}

void SShineComfyGraphStandardNode::CreatePinWidgets()
{
    for (UEdGraphPin* CurrentPin : GraphNode->Pins)
    {
        TSharedPtr<SGraphPin> NewPin = SNew(SShineComfyGraphStandardPin, CurrentPin);
        AddPin(NewPin.ToSharedRef());
    }
}

void SShineComfyGraphStandardNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
    PinToAdd->SetOwner(SharedThis(this));

    if (PinToAdd->GetDirection() == EGPD_Input)
    {
        LeftNodeBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f)
        [
            PinToAdd
        ];

        InputPins.Add(PinToAdd);
        return;
    }

    RightNodeBox->AddSlot()
    .AutoHeight()
    .HAlign(HAlign_Right)
    .Padding(0.0f, 2.0f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNullWidget::NullWidget
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            PinToAdd
        ]
    ];

    OutputPins.Add(PinToAdd);
}

FText SShineComfyGraphStandardNode::GetTextParameterValue(FName ParameterName) const
{
    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FShineComfyNodeParameter* Parameter = ShineNode ? ShineNode->FindParameter(ParameterName) : nullptr;
    return Parameter ? FText::FromString(Parameter->StringValue) : FText::GetEmpty();
}

TSharedPtr<FString> SShineComfyGraphStandardNode::GetSelectedTextOption(FName ParameterName) const
{
    const TArray<TSharedPtr<FString>>* OptionItems = ParameterOptionItems.Find(ParameterName);
    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FShineComfyNodeParameter* Parameter = ShineNode ? ShineNode->FindParameter(ParameterName) : nullptr;
    if (!OptionItems || !Parameter)
    {
        return nullptr;
    }

    for (const TSharedPtr<FString>& OptionItem : *OptionItems)
    {
        if (OptionItem.IsValid() && *OptionItem == Parameter->StringValue)
        {
            return OptionItem;
        }
    }

    return OptionItems->Num() > 0 ? (*OptionItems)[0] : nullptr;
}

float SShineComfyGraphStandardNode::GetFloatParameterValue(FName ParameterName) const
{
    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FShineComfyNodeParameter* Parameter = ShineNode ? ShineNode->FindParameter(ParameterName) : nullptr;
    return Parameter ? static_cast<float>(Parameter->FloatValue) : 0.0f;
}

int32 SShineComfyGraphStandardNode::GetIntegerParameterValue(FName ParameterName) const
{
    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FShineComfyNodeParameter* Parameter = ShineNode ? ShineNode->FindParameter(ParameterName) : nullptr;
    return Parameter ? Parameter->IntValue : 0;
}

ECheckBoxState SShineComfyGraphStandardNode::GetBoolParameterValue(FName ParameterName) const
{
    const UShineComfyGraphNodeBase* ShineNode = GetShineNode();
    const FShineComfyNodeParameter* Parameter = ShineNode ? ShineNode->FindParameter(ParameterName) : nullptr;
    return (Parameter && Parameter->bBoolValue) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

TOptional<float> SShineComfyGraphStandardNode::GetFloatMinValue(FName ParameterName) const
{
    if (ParameterName == TEXT("CfgScale"))
    {
        return MakeOptionalFloat(0.0f);
    }

    return TOptional<float>();
}

TOptional<float> SShineComfyGraphStandardNode::GetFloatMaxValue(FName ParameterName) const
{
    if (ParameterName == TEXT("CfgScale"))
    {
        return MakeOptionalFloat(30.0f);
    }

    return TOptional<float>();
}

TOptional<float> SShineComfyGraphStandardNode::GetFloatSliderMinValue(FName ParameterName) const
{
    if (ParameterName == TEXT("CfgScale"))
    {
        return MakeOptionalFloat(0.0f);
    }

    return TOptional<float>();
}

TOptional<float> SShineComfyGraphStandardNode::GetFloatSliderMaxValue(FName ParameterName) const
{
    if (ParameterName == TEXT("CfgScale"))
    {
        return MakeOptionalFloat(15.0f);
    }

    return TOptional<float>();
}

TOptional<int32> SShineComfyGraphStandardNode::GetIntegerMinValue(FName ParameterName) const
{
    if (ParameterName == TEXT("Steps"))
    {
        return MakeOptionalInt(1);
    }

    if (ParameterName == TEXT("Seed"))
    {
        return MakeOptionalInt(0);
    }

    return TOptional<int32>();
}

TOptional<int32> SShineComfyGraphStandardNode::GetIntegerMaxValue(FName ParameterName) const
{
    if (ParameterName == TEXT("Steps"))
    {
        return MakeOptionalInt(150);
    }

    if (ParameterName == TEXT("Seed"))
    {
        return MakeOptionalInt(2147483647);
    }

    return TOptional<int32>();
}

TOptional<int32> SShineComfyGraphStandardNode::GetIntegerSliderMinValue(FName ParameterName) const
{
    if (ParameterName == TEXT("Steps"))
    {
        return MakeOptionalInt(1);
    }

    if (ParameterName == TEXT("Seed"))
    {
        return MakeOptionalInt(0);
    }

    return TOptional<int32>();
}

TOptional<int32> SShineComfyGraphStandardNode::GetIntegerSliderMaxValue(FName ParameterName) const
{
    if (ParameterName == TEXT("Steps"))
    {
        return MakeOptionalInt(60);
    }

    if (ParameterName == TEXT("Seed"))
    {
        return MakeOptionalInt(100000);
    }

    return TOptional<int32>();
}

float SShineComfyGraphStandardNode::GetFloatDelta(FName ParameterName) const
{
    if (ParameterName == TEXT("CfgScale"))
    {
        return 0.05f;
    }

    return 0.1f;
}

int32 SShineComfyGraphStandardNode::GetIntegerDelta(FName ParameterName) const
{
    return 1;
}

void SShineComfyGraphStandardNode::HandleTextParameterCommitted(const FText& NewText, ETextCommit::Type CommitType, FName ParameterName)
{
    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetTextParameter(ParameterName, NewText.ToString());
    }
}

void SShineComfyGraphStandardNode::HandleTextParameterSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, FName ParameterName)
{
    if (!NewSelection.IsValid())
    {
        return;
    }

    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetTextParameter(ParameterName, *NewSelection);
    }
}

void SShineComfyGraphStandardNode::HandleFloatParameterChanged(float NewValue, FName ParameterName)
{
    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetFloatParameter(ParameterName, static_cast<double>(NewValue));
    }
}

void SShineComfyGraphStandardNode::HandleFloatParameterCommitted(float NewValue, ETextCommit::Type CommitType, FName ParameterName)
{
    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetFloatParameter(ParameterName, static_cast<double>(NewValue));
    }
}

void SShineComfyGraphStandardNode::HandleIntegerParameterChanged(int32 NewValue, FName ParameterName)
{
    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetIntegerParameter(ParameterName, NewValue);
    }
}

void SShineComfyGraphStandardNode::HandleIntegerParameterCommitted(int32 NewValue, ETextCommit::Type CommitType, FName ParameterName)
{
    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetIntegerParameter(ParameterName, NewValue);
    }
}

void SShineComfyGraphStandardNode::HandleBoolParameterChanged(ECheckBoxState NewState, FName ParameterName)
{
    if (UShineComfyGraphNodeBase* ShineNode = const_cast<UShineComfyGraphNodeBase*>(GetShineNode()))
    {
        ShineNode->SetBoolParameter(ParameterName, NewState == ECheckBoxState::Checked);
    }
}