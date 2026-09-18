#include "Tools/SixFaceCamera/SSixFaceCameraPanel.h"

#include "Tools/SixFaceCamera/ShineTools_SixFaceTool.h"
#include "GameFramework/Actor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSixFaceCameraPanel"

void SSixFaceCameraPanel::Construct(const FArguments& InArgs)
{
	SixFaceTool = InArgs._SixFaceTool;

	if (SixFaceTool)
	{
		DataChangedHandle = SixFaceTool->OnDataChanged.AddSP(this, &SSixFaceCameraPanel::RefreshStatusText);
	}

	ChildSlot
	[
		SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.AreaTitle(FText::FromString(TEXT("拍摄六面")))
		.BodyContent()
		[
			SNew(SBorder)
			.Padding(4.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("开始拍摄")))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked(this, &SSixFaceCameraPanel::GetIsCapturing)
						.OnCheckStateChanged(this, &SSixFaceCameraPanel::OnIsCapturingChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("拍摄距离")))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(1.0f)
						.MaxValue(100000.0f)
						.MinSliderValue(50.0f)
						.MaxSliderValue(5000.0f)
						.Value(this, &SSixFaceCameraPanel::GetCaptureDistance)
						.OnValueChanged(this, &SSixFaceCameraPanel::OnCaptureDistanceChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("拍摄高度")))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(-100000.0f)
						.MaxValue(100000.0f)
						.MinSliderValue(-5000.0f)
						.MaxSliderValue(5000.0f)
						.Value(this, &SSixFaceCameraPanel::GetCaptureHeight)
						.OnValueChanged(this, &SSixFaceCameraPanel::OnCaptureHeightChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(FText::FromString(TEXT("拍六面")))
					.ToolTipText(FText::FromString(TEXT("按当前拍摄距离更新六面相机并导出六张 PNG 图像")))
					.IsEnabled(this, &SSixFaceCameraPanel::GetIsCapturingBool)
					.OnClicked(this, &SSixFaceCameraPanel::OnGenerateClicked)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SAssignNew(StatusTextWidget, STextBlock)
					.AutoWrapText(true)
				]
			]
		]
	];

	RefreshStatusText();
}

SSixFaceCameraPanel::~SSixFaceCameraPanel()
{
	if (SixFaceTool && DataChangedHandle.IsValid())
	{
		SixFaceTool->OnDataChanged.Remove(DataChangedHandle);
	}
}

// ---------------------------------------------------------------------------
// Slate bindings
// ---------------------------------------------------------------------------

float SSixFaceCameraPanel::GetCaptureDistance() const
{
	return SixFaceTool ? SixFaceTool->GetCaptureDistance() : 300.0f;
}

void SSixFaceCameraPanel::OnCaptureDistanceChanged(float NewValue)
{
	if (SixFaceTool)
	{
		SixFaceTool->SetCaptureDistance(NewValue);
	}
}

float SSixFaceCameraPanel::GetCaptureHeight() const
{
	return SixFaceTool ? SixFaceTool->GetCaptureHeight() : 0.f;
}

void SSixFaceCameraPanel::OnCaptureHeightChanged(float NewValue)
{
	if (SixFaceTool)
	{
		SixFaceTool->SetCaptureHeight(NewValue);
	}
}

ECheckBoxState SSixFaceCameraPanel::GetIsCapturing() const
{
	return (SixFaceTool && SixFaceTool->IsCapturing()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SSixFaceCameraPanel::OnIsCapturingChanged(ECheckBoxState NewState)
{
	if (SixFaceTool)
	{
		SixFaceTool->SetIsCapturing(NewState == ECheckBoxState::Checked);
	}
}

bool SSixFaceCameraPanel::GetIsCapturingBool() const
{
	return SixFaceTool ? SixFaceTool->IsCapturing() : false;
}

FReply SSixFaceCameraPanel::OnGenerateClicked()
{
	if (SixFaceTool)
	{
		SixFaceTool->GenerateForSelection();
	}
	return FReply::Handled();
}

void SSixFaceCameraPanel::RefreshStatusText()
{
	if (!StatusTextWidget.IsValid())
	{
		return;
	}

	AActor* SelectedActor = FShineTools_SixFaceTool::GetSelectedActor();
	if (!SelectedActor)
	{
		StatusTextWidget->SetText(LOCTEXT("NoSelection", "No actor selected."));
		return;
	}

	const FString OutputDirectory = SixFaceTool ? SixFaceTool->GetLastOutputDirectory() : FString();
	const float CaptureDistance = SixFaceTool ? SixFaceTool->GetCaptureDistance() : 0.0f;
	const float CaptureHeight = SixFaceTool ? SixFaceTool->GetCaptureHeight() : 0.0f;
	if (!OutputDirectory.IsEmpty())
	{
		StatusTextWidget->SetText(FText::Format(
			LOCTEXT("SelectionAndOutputStatus", "Target: {0}\nDistance: {1}\nHeight: {2}\nOutput: {3}"),
			FText::FromString(SelectedActor->GetActorLabel()),
			FText::AsNumber(CaptureDistance),
			FText::AsNumber(CaptureHeight),
			FText::FromString(OutputDirectory)));
		return;
	}

	StatusTextWidget->SetText(FText::Format(
		LOCTEXT("SelectionStatus", "Target: {0}\nDistance: {1}\nHeight: {2}"),
		FText::FromString(SelectedActor->GetActorLabel()),
		FText::AsNumber(CaptureDistance),
		FText::AsNumber(CaptureHeight)));
}

#undef LOCTEXT_NAMESPACE
