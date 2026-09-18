#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FShineTools_SixFaceTool;
class STextBlock;

/**
 * Slate panel for "拍摄六面" — the entire expandable area within the Modes panel.
 * Talks directly to FShineTools_SixFaceTool — no EdMode/Toolkit passthrough.
 */
class SSixFaceCameraPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSixFaceCameraPanel) {}
		SLATE_ARGUMENT(FShineTools_SixFaceTool*, SixFaceTool)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSixFaceCameraPanel() override;

private:
	// -- Slate bindings --
	float     GetCaptureDistance() const;
	void      OnCaptureDistanceChanged(float NewValue);
	float     GetCaptureHeight() const;
	void      OnCaptureHeightChanged(float NewValue);
	ECheckBoxState GetIsCapturing() const;
	void      OnIsCapturingChanged(ECheckBoxState NewState);
	bool      GetIsCapturingBool() const;
	FReply    OnGenerateClicked();

	void RefreshStatusText();

	FShineTools_SixFaceTool* SixFaceTool = nullptr;
	TSharedPtr<STextBlock>   StatusTextWidget;
	FDelegateHandle          DataChangedHandle;
};
