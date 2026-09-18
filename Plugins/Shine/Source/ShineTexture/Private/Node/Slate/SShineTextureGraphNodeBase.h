#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"
#include "Styling/SlateBrush.h"

struct FShineTextureFloatEntryOptions
{
    TOptional<float> MinValue;
    TOptional<float> MaxValue;
    TOptional<float> MinSliderValue;
    TOptional<float> MaxSliderValue;
    TOptional<int32> MinFractionalDigits;
    TOptional<int32> MaxFractionalDigits;
    float MinDesiredValueWidth = 0.0f;
};

struct FShineTextureIntegerEntryOptions
{
    TOptional<int32> MinValue;
    TOptional<int32> MaxValue;
    TOptional<int32> MinSliderValue;
    TOptional<int32> MaxSliderValue;
    float MinDesiredValueWidth = 0.0f;
};

class UShineTextureGraphNodeBase;

class SShineTextureGraphNodeBase : public SGraphNode
{
public:
    SLATE_BEGIN_ARGS(SShineTextureGraphNodeBase) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureGraphNodeBase* InNode);
    void ConstructBase(UShineTextureGraphNodeBase* InNode);

    virtual void UpdateGraphNode() override;
    virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

protected:
    virtual TSharedPtr<SGraphPin> CreatePinWidget(UEdGraphPin* Pin) const override;
    virtual TSharedRef<SWidget> CreateNodeContent() const;
    virtual bool ShouldPinsAppearAboveContent() const;

    TSharedRef<SWidget> WrapNodeContent(const TSharedRef<SWidget>& Content, float MinWidth = -1.0f) const;
    TSharedRef<SWidget> CreateBodyText(const FText& Text) const;
    TSharedRef<SWidget> CreateSectionLabel(const FText& Label) const;
    TSharedRef<SWidget> CreateSingleValueSection(const FText& Label, const TSharedRef<SWidget>& ValueWidget, float TopPadding = -1.0f) const;
    TSharedRef<SWidget> CreateDualValueSection(const FText& Label, const TSharedRef<SWidget>& LeftValueWidget, const TSharedRef<SWidget>& RightValueWidget, float TopPadding = -1.0f) const;
    TSharedRef<SWidget> CreatePreviewFrame(const TSharedRef<SWidget>& Content, float Padding = 2.0f) const;
    TSharedRef<SWidget> CreatePreviewNodeContent(const FText& Description, const TSharedRef<SWidget>& PreviewWidget, const FText& ControlLabel, const TSharedRef<SWidget>& ControlWidget, float MinWidth = -1.0f, float PreviewTopPadding = -1.0f, float ControlTopPadding = -1.0f) const;
    TSharedRef<SWidget> CreateFloatEntryBox(TFunction<TOptional<float>()> ValueGetter, TFunction<void(float)> OnValueChanged, const FShineTextureFloatEntryOptions& Options = FShineTextureFloatEntryOptions(), const TSharedPtr<SWidget>& LabelWidget = nullptr) const;
    TSharedRef<SWidget> CreateIntegerEntryBox(TFunction<TOptional<int32>()> ValueGetter, TFunction<void(int32)> OnValueChanged, const FShineTextureIntegerEntryOptions& Options = FShineTextureIntegerEntryOptions(), const TSharedPtr<SWidget>& LabelWidget = nullptr) const;

    const FSlateBrush* GetNodeOverlayBrush() const;
    FSlateColor GetHeaderAccentColor() const;
    FText GetInlinePreviewToggleText() const;
    EVisibility GetInlinePreviewVisibility() const;
    FReply HandleInlinePreviewToggle();
    const FSlateBrush* GetInlinePreviewBrush() const;
    TSharedRef<SWidget> CreateInlinePreviewPanel() const;
    FText GetPreviewDisplayModeText() const;
    TSharedRef<SWidget> MakePreviewDisplayModeWidget(TSharedPtr<int32> InOption) const;
    void HandlePreviewDisplayModeChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo);

    UShineTextureGraphNodeBase* GetTextureNode() const;

    TSharedPtr<SVerticalBox> LeftNodeBox;
    TSharedPtr<SVerticalBox> RightNodeBox;
    TSharedPtr<SVerticalBox> TopInputNodeBox;
    TSharedPtr<SVerticalBox> TopOutputNodeBox;
    mutable FSlateBrush InlinePreviewBrush;
    mutable TArray<TSharedPtr<int32>> PreviewDisplayModeOptions;
    mutable TSharedPtr<int32> SelectedPreviewDisplayMode;
};
