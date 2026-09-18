#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;
class FShineTools_SixFaceVisualizer;

class SSixFaceCameraViewportPreview final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSixFaceCameraViewportPreview) {}
		SLATE_ARGUMENT(FShineTools_SixFaceVisualizer*, Visualizer)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	const FSlateBrush* GetPreviewBrush(int32 PreviewIndex) const;
	EVisibility GetPreviewVisibility(int32 PreviewIndex) const;
	FText GetPreviewLabelText(int32 PreviewIndex) const;

	FShineTools_SixFaceVisualizer* Visualizer = nullptr;
	mutable TArray<FSlateBrush> PreviewBrushes;
};