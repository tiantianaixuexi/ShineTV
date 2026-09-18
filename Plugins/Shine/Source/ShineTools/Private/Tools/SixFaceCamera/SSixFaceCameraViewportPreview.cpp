#include "Tools/SixFaceCamera/SSixFaceCameraViewportPreview.h"
#include "Tools/SixFaceCamera/ShineTools_SixFaceVisualizer.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float PreviewWidth = 120.0f;
	constexpr float PreviewHeight = 120.0f;
	constexpr float PreviewCardPadding = 6.0f;
	constexpr float PreviewPanelPadding = 8.0f;
}

void SSixFaceCameraViewportPreview::Construct(const FArguments& InArgs)
{
	Visualizer = InArgs._Visualizer;
	PreviewBrushes.SetNum(Visualizer ? Visualizer->GetPreviewCount() : 0);

	const TSharedRef<SVerticalBox> PreviewList = SNew(SVerticalBox);
	for (int32 PreviewIndex = 0; PreviewIndex < PreviewBrushes.Num(); ++PreviewIndex)
	{
		TSharedRef<SWidget> PreviewCard =
			SNew(SBorder)
			.Visibility(this, &SSixFaceCameraViewportPreview::GetPreviewVisibility, PreviewIndex)
			.Padding(PreviewCardPadding)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SSixFaceCameraViewportPreview::GetPreviewLabelText, PreviewIndex)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(PreviewWidth)
					.HeightOverride(PreviewHeight)
					[
						SNew(SImage)
						.Image(this, &SSixFaceCameraViewportPreview::GetPreviewBrush, PreviewIndex)
					]
				]
			];

		PreviewList->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			PreviewCard
		];
	}

	ChildSlot
	[
		SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Anchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f))
		.AutoSize(true)
		.Alignment(FVector2D(1.0f, 0.0f))
		.Offset(FMargin(-12.0f, 12.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.Padding(PreviewPanelPadding)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SNew(SBox)
					.MinDesiredWidth(PreviewWidth + (PreviewCardPadding * 2.0f))
					[
						PreviewList
					]
				]
			]
		]
	];
}

const FSlateBrush* SSixFaceCameraViewportPreview::GetPreviewBrush(int32 PreviewIndex) const
{
	if (!Visualizer || !PreviewBrushes.IsValidIndex(PreviewIndex))
	{
		return nullptr;
	}

	PreviewBrushes[PreviewIndex].SetResourceObject(Visualizer->GetPreviewRenderTarget(PreviewIndex));
	PreviewBrushes[PreviewIndex].ImageSize = FVector2D(PreviewWidth, PreviewHeight);
	return &PreviewBrushes[PreviewIndex];
}

EVisibility SSixFaceCameraViewportPreview::GetPreviewVisibility(int32 PreviewIndex) const
{
	return (Visualizer && Visualizer->GetPreviewRenderTarget(PreviewIndex)) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SSixFaceCameraViewportPreview::GetPreviewLabelText(int32 PreviewIndex) const
{
	return Visualizer ? FText::FromString(Visualizer->GetPreviewLabel(PreviewIndex)) : FText::GetEmpty();
}