#pragma once

#include "CoreMinimal.h"
#include "Tools/ShineBaseTool.h"

class FShineTools_SixFaceVisualizer;
class IAssetViewport;
class SWidget;
class AActor;

DECLARE_MULTICAST_DELEGATE(FOnSixFaceToolDataChanged);

/**
 * Six-face orthographic capture tool.
 * Owns the Visualizer, viewport overlay, and Slate panel.
 */
class FShineTools_SixFaceTool : public FShineBaseTool
{
public:
	FShineTools_SixFaceTool();
	virtual ~FShineTools_SixFaceTool() override;

	// -- FShineBaseTool overrides --
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool IsActive() const override { return IsCapturing(); }
	virtual FName GetSectionName() const override;
	virtual void OnSelectionChanged() override;
	virtual TSharedRef<SWidget> CreatePanelWidget() override;

	// -- Parameters --
	float GetCaptureDistance() const;
	void SetCaptureDistance(float NewDistance);
	float GetCaptureHeight() const;
	void SetCaptureHeight(float NewHeight);
	bool IsCapturing() const;
	void SetIsCapturing(bool bNewIsCapturing);
	const FString& GetLastOutputDirectory() const;

	// -- Actions --
	void GenerateForSelection();

	/** Selection utility. */
	static AActor* GetSelectedActor();

	/** Multicast — Slate panels subscribe to refresh themselves. */
	FOnSixFaceToolDataChanged OnDataChanged;

private:
	void AddViewportOverlay();
	void RemoveViewportOverlay();

	TUniquePtr<FShineTools_SixFaceVisualizer> Visualizer;
	TSharedPtr<IAssetViewport> OverlayViewport;
	TSharedPtr<SWidget> ViewportOverlayWidget;
};
