#include "Node/Slate/SShineTextureGraphNodeBase.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Node/ShineTextureNodeConstants.h"
#include "Node/Slate/Pin/SShineTextureGraphPin.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SShineTextureGraphNodeBase::Construct(const FArguments& InArgs, UShineTextureGraphNodeBase* InNode)
{
	ConstructBase(InNode);
}

void SShineTextureGraphNodeBase::ConstructBase(UShineTextureGraphNodeBase* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SShineTextureGraphNodeBase::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	LeftNodeBox.Reset();
	RightNodeBox.Reset();
	TopInputNodeBox.Reset();
	TopOutputNodeBox.Reset();

	for (int32 ZoneIndex = 0; ZoneIndex < static_cast<int32>(ENodeZone::Count); ++ZoneIndex)
	{
		RemoveSlot(static_cast<ENodeZone::Type>(ZoneIndex));
	}

	SetupErrorReporting();
	ContentScale.Bind(this, &SGraphNode::GetContentScale);

	GetOrAddSlot(ENodeZone::Center)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(ShineTextureSlateTheme::GetNodeCardBrush())
			.Padding(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredHeight(ShineTextureSlateTheme::HeaderHeight())
					[
						SNew(SBorder)
						.BorderImage(ShineTextureSlateTheme::GetHeaderFillBrush())
						.BorderBackgroundColor(ShineTextureSlateTheme::NodeBackground())
						.Padding(FMargin(0.0f, 0.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SBox)
								.HeightOverride(3.0f)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
									.BorderBackgroundColor(this, &SShineTextureGraphNodeBase::GetHeaderAccentColor)
								]
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(ShineTextureSlateTheme::GetNodeHeaderAccentPadding())
								.VAlign(VAlign_Center)
								[
									SNew(SColorBlock)
									.Color_Lambda([this]() { return GetHeaderAccentColor().GetSpecifiedColor(); })
									.Size(FVector2D(10.0f, 10.0f))
									.CornerRadius(FVector4(5.0f, 5.0f, 5.0f, 5.0f))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(ShineTextureSlateTheme::GetNodeHeaderTitlePadding())
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(GraphNode
										      ? GraphNode->GetNodeTitle(ENodeTitleType::FullTitle)
										      : FText::GetEmpty())
									.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
									.ColorAndOpacity(ShineTextureSlateTheme::TextMain())
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(ShineTextureSlateTheme::GetNodeHeaderActionPadding())
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "SimpleButton")
									.ContentPadding(FMargin(4.0f, 2.0f))
									.OnClicked(this, &SShineTextureGraphNodeBase::HandleInlinePreviewToggle)
									[
										SNew(STextBlock)
										.Text(this, &SShineTextureGraphNodeBase::GetInlinePreviewToggleText)
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(ShineTextureSlateTheme::TextDim())
									]
								]
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(ShineTextureSlateTheme::GetBodyFillBrush())
					.BorderBackgroundColor(ShineTextureSlateTheme::NodeBackground())
					.Padding(ShineTextureSlateTheme::BodyPadding())
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(ShineTextureSlateTheme::GetNodeSubtitleSlotPadding())
						[
							SNew(STextBlock)
							.Text(GetTextureNode() ? GetTextureNode()->GetSubtitle() : FText::GetEmpty())
							.TextStyle(FAppStyle::Get(), "SmallText")
							.ColorAndOpacity(ShineTextureSlateTheme::TextSecondary())
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							ShouldPinsAppearAboveContent()
								? StaticCastSharedRef<SWidget>(
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										.HAlign(HAlign_Left)
										[
											SAssignNew(TopInputNodeBox, SVerticalBox)
										]
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										.Padding(ShineTextureSlateTheme::GetNodeContentPinPaddingMargin())
										.HAlign(HAlign_Right)
										[
											SAssignNew(TopOutputNodeBox, SVerticalBox)
										]
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(ShineTextureSlateTheme::GetTopSectionPadding())
									[
										CreateNodeContent()
									])
								: StaticCastSharedRef<SWidget>(
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Fill)
									[
										SAssignNew(LeftNodeBox, SVerticalBox)
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Fill)
									[
										SAssignNew(RightNodeBox, SVerticalBox)
									])
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(ShineTextureSlateTheme::GetInlinePreviewPanelSlotPadding())
						[
							SNew(SBorder)
							.BorderImage(ShineTextureSlateTheme::GetPanelBrush())
							.Padding(ShineTextureSlateTheme::GetInlinePreviewPanelContentPadding())
							.Visibility(this, &SShineTextureGraphNodeBase::GetInlinePreviewVisibility)
							[
								CreateInlinePreviewPanel()
							]
						]
					]
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(this, &SShineTextureGraphNodeBase::GetNodeOverlayBrush)
			.Visibility(EVisibility::HitTestInvisible)
			[
				SNullWidget::NullWidget
			]
		]
	];

	CreatePinWidgets();
}

void SShineTextureGraphNodeBase::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));

	const bool bUseTopPinLayout = ShouldPinsAppearAboveContent() && TopInputNodeBox.IsValid() && TopOutputNodeBox.
		IsValid();

	if (PinToAdd->GetDirection() == EGPD_Input)
	{
		(bUseTopPinLayout ? TopInputNodeBox : LeftNodeBox)->AddSlot()
		                                                  .AutoHeight()
		                                                  .Padding(ShineTextureSlateTheme::GetPinSlotPadding())
		[
			PinToAdd
		];
		InputPins.Add(PinToAdd);
		return;
	}

	(bUseTopPinLayout ? TopOutputNodeBox : RightNodeBox)->AddSlot()
	                                                    .AutoHeight()
	                                                    .Padding(ShineTextureSlateTheme::GetPinSlotPadding())
	[
		PinToAdd
	];
	OutputPins.Add(PinToAdd);
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateNodeContent() const
{
	return WrapNodeContent(CreateBodyText(FText::GetEmpty()));
}

bool SShineTextureGraphNodeBase::ShouldPinsAppearAboveContent() const
{
	return true;
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::WrapNodeContent(const TSharedRef<SWidget>& Content,
                                                                float MinWidth) const
{
	const float ResolvedMinWidth = MinWidth >= 0.0f ? MinWidth : ShineTextureSlateTheme::NodeContentMinWidth();
	return SNew(SBox)
		.MinDesiredWidth(ResolvedMinWidth)
		[
			Content
		];
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateBodyText(const FText& Text) const
{
		return SNew(STextBlock)
		.TextStyle(FAppStyle::Get(), "SmallText")
		.ColorAndOpacity(ShineTextureSlateTheme::TextSecondary())
		.AutoWrapText(true)
		.Text(Text);
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateSectionLabel(const FText& Label) const
{
	return SNew(STextBlock)
		.Text(Label)
		.TextStyle(FAppStyle::Get(), "SmallText")
		.ColorAndOpacity(ShineTextureSlateTheme::TextSecondary());
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateSingleValueSection(
	const FText& Label, const TSharedRef<SWidget>& ValueWidget, float TopPadding) const
{
	const float ResolvedTopPadding = TopPadding >= 0.0f ? TopPadding : ShineTextureSlateTheme::FormSectionTopPadding();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ResolvedTopPadding >= ShineTextureSlateTheme::FormSectionTopPadding()
			         ? ShineTextureSlateTheme::GetTopSectionPadding()
			         : FMargin(0.0f, ResolvedTopPadding, 0.0f, 0.0f))
		[
			CreateSectionLabel(Label)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ShineTextureSlateTheme::GetFormLabelSlotPadding())
		[
			SNew(SBox)
			.MinDesiredHeight(ShineTextureSlateTheme::FormSectionMinHeight())
			[
				ValueWidget
			]
		];
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateDualValueSection(const FText& Label,
                                                                       const TSharedRef<SWidget>& LeftValueWidget,
                                                                       const TSharedRef<SWidget>& RightValueWidget,
                                                                       float TopPadding) const
{
	const float ResolvedTopPadding = TopPadding >= 0.0f ? TopPadding : ShineTextureSlateTheme::FormSectionTopPadding();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ResolvedTopPadding >= ShineTextureSlateTheme::FormSectionTopPadding()
			         ? ShineTextureSlateTheme::GetTopSectionPadding()
			         : FMargin(0.0f, ResolvedTopPadding, 0.0f, 0.0f))
		[
			CreateSectionLabel(Label)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ShineTextureSlateTheme::GetFormLabelSlotPadding())
		[
			SNew(SBox)
			.MinDesiredHeight(ShineTextureSlateTheme::FormSectionMinHeight())
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					LeftValueWidget
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(ShineTextureSlateTheme::GetFormDualValueSlotPadding())
				[
					RightValueWidget
				]
			]
		];
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreatePreviewFrame(const TSharedRef<SWidget>& Content,
                                                                   float Padding) const
{
	return SNew(SBorder)
		.BorderImage(ShineTextureSlateTheme::GetPreviewBrush())
		.Padding(Padding)
		[
			Content
		];
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreatePreviewNodeContent(
	const FText& Description, const TSharedRef<SWidget>& PreviewWidget, const FText& ControlLabel,
	const TSharedRef<SWidget>& ControlWidget, float MinWidth, float PreviewTopPadding, float ControlTopPadding) const
{
	const float ResolvedMinWidth = MinWidth >= 0.0f ? MinWidth : ShineTextureSlateTheme::PreviewNodeContentMinWidth();
	const float ResolvedPreviewTopPadding = PreviewTopPadding >= 0.0f
		                                        ? PreviewTopPadding
		                                        : ShineTextureSlateTheme::BodyPadding();
	const float ResolvedControlTopPadding = ControlTopPadding >= 0.0f
		                                        ? ControlTopPadding
		                                        : ShineTextureSlateTheme::BodyPadding();
	return WrapNodeContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CreateBodyText(Description)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(ResolvedPreviewTopPadding >= ShineTextureSlateTheme::BodyPadding()
			                                            ? ShineTextureSlateTheme::GetBodySpacingPadding()
			                                            : FMargin(0.0f, ResolvedPreviewTopPadding, 0.0f, 0.0f))
		[
			CreatePreviewFrame(PreviewWidget)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			CreateSingleValueSection(ControlLabel, ControlWidget, ResolvedControlTopPadding)
		],
		ResolvedMinWidth);
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateFloatEntryBox(TFunction<TOptional<float>()> ValueGetter,
                                                                    TFunction<void(float)> OnValueChanged,
                                                                    const FShineTextureFloatEntryOptions& Options,
                                                                    const TSharedPtr<SWidget>& LabelWidget) const
{
	const float MinDesiredValueWidth = Options.MinDesiredValueWidth > 0.0f
		                                   ? Options.MinDesiredValueWidth
		                                   : ShineTextureSlateTheme::NumericEntryMinWidth();
	if (LabelWidget.IsValid())
	{
		return SNew(SNumericEntryBox<float>)
			.EditableTextBoxStyle(&ShineTextureSlateTheme::GetNumericEntryTextBoxStyle())
			.SpinBoxStyle(&ShineTextureSlateTheme::GetNumericEntrySpinBoxStyle())
			.AllowSpin(true)
			.BorderBackgroundColor(ShineTextureSlateTheme::InputBackground())
			.BorderForegroundColor(ShineTextureSlateTheme::TextMain())
			.MinDesiredValueWidth(MinDesiredValueWidth)
			.MinValue(Options.MinValue)
			.MaxValue(Options.MaxValue)
			.MinSliderValue(Options.MinSliderValue)
			.MaxSliderValue(Options.MaxSliderValue)
			.MinFractionalDigits(Options.MinFractionalDigits)
			.MaxFractionalDigits(Options.MaxFractionalDigits)
			.OnBeginSliderMovement_Lambda([this]()
			{
				if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
				{
					TextureNode->BeginInteractivePreviewChange();
				}
			})
			.OnEndSliderMovement_Lambda([this](float)
			{
				if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
				{
					TextureNode->EndInteractivePreviewChange();
				}
			})
			.LabelVAlign(VAlign_Center)
			.Label()
			[
				LabelWidget.ToSharedRef()
			]
			.Value_Lambda([Getter = MoveTemp(ValueGetter)]() mutable -> TOptional<float> { return Getter(); })
			.OnValueChanged_Lambda([Changed = OnValueChanged](float NewValue) mutable { Changed(NewValue); })
			.OnValueCommitted_Lambda([Changed = MoveTemp(OnValueChanged)](float NewValue, ETextCommit::Type) mutable
			{
				Changed(NewValue);
			});
	}

	return SNew(SNumericEntryBox<float>)
		.EditableTextBoxStyle(&ShineTextureSlateTheme::GetNumericEntryTextBoxStyle())
		.SpinBoxStyle(&ShineTextureSlateTheme::GetNumericEntrySpinBoxStyle())
		.AllowSpin(true)
		.BorderBackgroundColor(ShineTextureSlateTheme::InputBackground())
		.BorderForegroundColor(ShineTextureSlateTheme::TextMain())
		.MinDesiredValueWidth(MinDesiredValueWidth)
		.MinValue(Options.MinValue)
		.MaxValue(Options.MaxValue)
		.MinSliderValue(Options.MinSliderValue)
		.MaxSliderValue(Options.MaxSliderValue)
		.MinFractionalDigits(Options.MinFractionalDigits)
		.MaxFractionalDigits(Options.MaxFractionalDigits)
		.OnBeginSliderMovement_Lambda([this]()
		{
			if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
			{
				TextureNode->BeginInteractivePreviewChange();
			}
		})
		.OnEndSliderMovement_Lambda([this](float)
		{
			if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
			{
				TextureNode->EndInteractivePreviewChange();
			}
		})
		.Value_Lambda([Getter = MoveTemp(ValueGetter)]() mutable -> TOptional<float> { return Getter(); })
		.OnValueChanged_Lambda([Changed = OnValueChanged](float NewValue) mutable { Changed(NewValue); })
		.OnValueCommitted_Lambda([Changed = MoveTemp(OnValueChanged)](float NewValue, ETextCommit::Type) mutable
		{
			Changed(NewValue);
		});
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateIntegerEntryBox(TFunction<TOptional<int32>()> ValueGetter,
                                                                      TFunction<void(int32)> OnValueChanged,
                                                                      const FShineTextureIntegerEntryOptions& Options,
                                                                      const TSharedPtr<SWidget>& LabelWidget) const
{
	const float MinDesiredValueWidth = Options.MinDesiredValueWidth > 0.0f
		                                   ? Options.MinDesiredValueWidth
		                                   : ShineTextureSlateTheme::NumericEntryMinWidth();
	if (LabelWidget.IsValid())
	{
		return SNew(SNumericEntryBox<int32>)
			.EditableTextBoxStyle(&ShineTextureSlateTheme::GetNumericEntryTextBoxStyle())
			.SpinBoxStyle(&ShineTextureSlateTheme::GetNumericEntrySpinBoxStyle())
			.AllowSpin(true)
			.BorderBackgroundColor(ShineTextureSlateTheme::InputBackground())
			.BorderForegroundColor(ShineTextureSlateTheme::TextMain())
			.MinDesiredValueWidth(MinDesiredValueWidth)
			.MinValue(Options.MinValue)
			.MaxValue(Options.MaxValue)
			.MinSliderValue(Options.MinSliderValue)
			.MaxSliderValue(Options.MaxSliderValue)
			.OnBeginSliderMovement_Lambda([this]()
			{
				if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
				{
					TextureNode->BeginInteractivePreviewChange();
				}
			})
			.OnEndSliderMovement_Lambda([this](int32)
			{
				if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
				{
					TextureNode->EndInteractivePreviewChange();
				}
			})
			.LabelVAlign(VAlign_Center)
			.Label()
			[
				LabelWidget.ToSharedRef()
			]
			.Value_Lambda([Getter = MoveTemp(ValueGetter)]() mutable -> TOptional<int32> { return Getter(); })
			.OnValueChanged_Lambda([Changed = OnValueChanged](int32 NewValue) mutable { Changed(NewValue); })
			.OnValueCommitted_Lambda([Changed = MoveTemp(OnValueChanged)](int32 NewValue, ETextCommit::Type) mutable
			{
				Changed(NewValue);
			});
	}

	return SNew(SNumericEntryBox<int32>)
		.EditableTextBoxStyle(&ShineTextureSlateTheme::GetNumericEntryTextBoxStyle())
		.SpinBoxStyle(&ShineTextureSlateTheme::GetNumericEntrySpinBoxStyle())
		.AllowSpin(true)
		.BorderBackgroundColor(ShineTextureSlateTheme::InputBackground())
		.BorderForegroundColor(ShineTextureSlateTheme::TextMain())
		.MinDesiredValueWidth(MinDesiredValueWidth)
		.MinValue(Options.MinValue)
		.MaxValue(Options.MaxValue)
		.MinSliderValue(Options.MinSliderValue)
		.MaxSliderValue(Options.MaxSliderValue)
		.Value_Lambda([Getter = MoveTemp(ValueGetter)]() mutable -> TOptional<int32> { return Getter(); })
		.OnBeginSliderMovement_Lambda([this]()
		{
			if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
			{
				TextureNode->BeginInteractivePreviewChange();
			}
		})
		.OnEndSliderMovement_Lambda([this](int32)
		{
			if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
			{
				TextureNode->EndInteractivePreviewChange();
			}
		})
		.OnValueChanged_Lambda([Changed = OnValueChanged](int32 NewValue) mutable { Changed(NewValue); })
		.OnValueCommitted_Lambda([Changed = MoveTemp(OnValueChanged)](int32 NewValue, ETextCommit::Type) mutable
		{
			Changed(NewValue);
		});
}

TSharedPtr<SGraphPin> SShineTextureGraphNodeBase::CreatePinWidget(UEdGraphPin* Pin) const
{
	return SNew(SShineTextureGraphPin, Pin);
}

const FSlateBrush* SShineTextureGraphNodeBase::GetNodeOverlayBrush() const
{
	return IsSelectedExclusively()
		       ? ShineTextureSlateTheme::GetNodeSelectionBrush()
		       : ShineTextureSlateTheme::GetNodeHoverBrush();
}

FSlateColor SShineTextureGraphNodeBase::GetHeaderAccentColor() const
{
	return GraphNode ? GraphNode->GetNodeTitleColor() : ShineTextureSlateTheme::TextDim();
}

FText SShineTextureGraphNodeBase::GetInlinePreviewToggleText() const
{
	const UShineTextureGraphNodeBase* TextureNode = GetTextureNode();
	return TextureNode && TextureNode->IsInlinePreviewExpanded()
		       ? NSLOCTEXT("SShineTextureGraphNodeBase", "HidePreview", "隐藏 (Hide)")
		       : NSLOCTEXT("SShineTextureGraphNodeBase", "ShowPreview", "预览 (Preview)");
}

EVisibility SShineTextureGraphNodeBase::GetInlinePreviewVisibility() const
{
	const UShineTextureGraphNodeBase* TextureNode = GetTextureNode();
	return TextureNode && TextureNode->IsInlinePreviewExpanded()
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

FReply SShineTextureGraphNodeBase::HandleInlinePreviewToggle()
{
	if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
	{
		TextureNode->SetInlinePreviewExpanded(!TextureNode->IsInlinePreviewExpanded());
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

const FSlateBrush* SShineTextureGraphNodeBase::GetInlinePreviewBrush() const
{
	InlinePreviewBrush = FSlateBrush();
	const FIntPoint PreviewSize = GetTextureNode()
		                              ? GetTextureNode()->GetInlinePreviewSize()
		                              : ShineTextureNodeConstants::DefaultInlinePreviewSize();
	InlinePreviewBrush.ImageSize = FVector2D(PreviewSize.X, PreviewSize.Y);

	UTexture* PreviewTexture = nullptr;
	if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
	{
		PreviewTexture = TextureNode->GetInlinePreviewDisplayTexture();
		if (!PreviewTexture)
		{
			UTextureRenderTarget2D* PreviewRenderTarget = TextureNode->GetOrCreateInlinePreviewRenderTarget(
				TextureNode->GetInlinePreviewSize());
			PreviewTexture = PreviewRenderTarget;
		}
	}

	InlinePreviewBrush.DrawAs = PreviewTexture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	InlinePreviewBrush.SetResourceObject(PreviewTexture);
	return &InlinePreviewBrush;
}

FText SShineTextureGraphNodeBase::GetPreviewDisplayModeText() const
{
	static const FText ModeLabels[] =
	{
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeRGBA", "RGBA"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeGrayscale", "黑白 (Grayscale)"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeRed", "Red"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeGreen", "Green"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeBlue", "Blue"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeAlpha", "Alpha"),
	};

	const UShineTextureGraphNodeBase* TextureNode = GetTextureNode();
	const int32 ModeIndex = TextureNode ? static_cast<int32>(TextureNode->GetPreviewDisplayMode()) : 0;
	return ModeLabels[FMath::Clamp(ModeIndex, 0, UE_ARRAY_COUNT(ModeLabels) - 1)];
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::MakePreviewDisplayModeWidget(TSharedPtr<int32> InOption) const
{
	const int32 ModeIndex = InOption.IsValid() ? *InOption : 0;
	static const FText ModeLabels[] =
	{
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeRGBAOption", "RGBA"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeGrayscaleOption", "黑白 (Grayscale)"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeRedOption", "Red"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeGreenOption", "Green"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeBlueOption", "Blue"),
		NSLOCTEXT("SShineTextureGraphNodeBase", "DisplayModeAlphaOption", "Alpha"),
	};

	return SNew(STextBlock)
		.Text(ModeLabels[FMath::Clamp(ModeIndex, 0, UE_ARRAY_COUNT(ModeLabels) - 1)])
		.Font(ShineTextureSlateTheme::BodyFont());
}

void SShineTextureGraphNodeBase::HandlePreviewDisplayModeChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct || !NewSelection.IsValid())
	{
		return;
	}

	SelectedPreviewDisplayMode = NewSelection;
	if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
	{
		TextureNode->SetPreviewDisplayMode(static_cast<EShineTexturePreviewDisplayMode>(*NewSelection));
	}
}

TSharedRef<SWidget> SShineTextureGraphNodeBase::CreateInlinePreviewPanel() const
{
	const FIntPoint PreviewSize = GetTextureNode()
		                              ? GetTextureNode()->GetInlinePreviewSize()
		                              : ShineTextureNodeConstants::DefaultInlinePreviewSize();
	constexpr  float MaxPreviewDimension = 112.0f;
	const float Width = static_cast<float>(FMath::Max(1, PreviewSize.X));
	const float Height = static_cast<float>(FMath::Max(1, PreviewSize.Y));
	const float ScaleFactor = MaxPreviewDimension / FMath::Max(Width, Height);
	const FVector2D DisplaySize(Width * ScaleFactor, Height * ScaleFactor);
	constexpr float PreviewFramePadding = 1.0f;
	const FVector2D FrameSize(DisplaySize.X + (PreviewFramePadding * 2.0f),
	                          DisplaySize.Y + (PreviewFramePadding * 2.0f));

	if (PreviewDisplayModeOptions.IsEmpty())
	{
		for (int32 ModeIndex = 0; ModeIndex < 6; ++ModeIndex)
		{
			PreviewDisplayModeOptions.Add(MakeShared<int32>(ModeIndex));
		}
	}

	const int32 CurrentModeIndex = GetTextureNode()
		                               ? static_cast<int32>(GetTextureNode()->GetPreviewDisplayMode())
		                               : 0;
	SelectedPreviewDisplayMode = PreviewDisplayModeOptions[FMath::Clamp(CurrentModeIndex, 0, PreviewDisplayModeOptions.Num() - 1)];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(FrameSize.X)
			.HeightOverride(FrameSize.Y)
			[
				CreatePreviewFrame(
					SNew(SBox)
					.WidthOverride(DisplaySize.X)
					.HeightOverride(DisplaySize.Y)
					[
						SNew(SImage)
						.Image(this, &SShineTextureGraphNodeBase::GetInlinePreviewBrush)
					],
					PreviewFramePadding)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			CreateDualValueSection(
				NSLOCTEXT("SShineTextureGraphNodeBase", "PreviewSize", "预览尺寸 (Preview Size)"),
				CreateIntegerEntryBox(
					[this]() -> TOptional<int32>
					{
						return GetTextureNode() ? GetTextureNode()->GetInlinePreviewSize().X : 0;
					},
					[this](int32 NewValue)
					{
						if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
						{
							FIntPoint NewSize = TextureNode->GetInlinePreviewSize();
							NewSize.X = NewValue;
							TextureNode->SetInlinePreviewSize(NewSize);
						}
					},
					FShineTextureIntegerEntryOptions(),
					CreateSectionLabel(NSLOCTEXT("SShineTextureGraphNodeBase", "PreviewWidth", "宽(W)"))),
				CreateIntegerEntryBox(
					[this]() -> TOptional<int32>
					{
						return GetTextureNode() ? GetTextureNode()->GetInlinePreviewSize().Y : 0;
					},
					[this](int32 NewValue)
					{
						if (UShineTextureGraphNodeBase* TextureNode = GetTextureNode())
						{
							FIntPoint NewSize = TextureNode->GetInlinePreviewSize();
							NewSize.Y = NewValue;
							TextureNode->SetInlinePreviewSize(NewSize);
						}
					},
					FShineTextureIntegerEntryOptions(),
					CreateSectionLabel(NSLOCTEXT("SShineTextureGraphNodeBase", "PreviewHeight", "高(H)"))),
				0.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			CreateSingleValueSection(
				NSLOCTEXT("SShineTextureGraphNodeBase", "PreviewDisplayMode", "显示模式 (Display Mode)"),
				SNew(SComboBox<TSharedPtr<int32>>)
				.OptionsSource(&PreviewDisplayModeOptions)
				.InitiallySelectedItem(SelectedPreviewDisplayMode)
				.OnGenerateWidget_Lambda([this](TSharedPtr<int32> InOption)
			{
				return const_cast<SShineTextureGraphNodeBase*>(this)->MakePreviewDisplayModeWidget(InOption);
			})
				.OnSelectionChanged_Lambda([this](TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo)
			{
				const_cast<SShineTextureGraphNodeBase*>(this)->HandlePreviewDisplayModeChanged(NewSelection, SelectInfo);
			})
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
				{
					return const_cast<SShineTextureGraphNodeBase*>(this)->GetPreviewDisplayModeText();
				})
					.Font(ShineTextureSlateTheme::BodyFont())
				])
		];
}

UShineTextureGraphNodeBase* SShineTextureGraphNodeBase::GetTextureNode() const
{
	return GraphNode ? CastChecked<UShineTextureGraphNodeBase>(GraphNode) : nullptr;
}
