#pragma once

#include "CoreMinimal.h"

class AActor;
class ASceneCapture2D;
class UPrimitiveComponent;
class UTextureRenderTarget2D;


class FShineTools_SixFaceVisualizer
{
public:
	void Clear();
	
	
	bool UpdatePreviewForActor(AActor* TargetActor);
	bool GenerateForActor(AActor* TargetActor);
	const FString& GetLastOutputDirectory() const { return LastOutputDirectory; }
	static int32 GetPreviewCount();
	const TCHAR* GetPreviewLabel(int32 PreviewIndex) const;
	UTextureRenderTarget2D* GetPreviewRenderTarget(int32 PreviewIndex) const;

	static FBox ComputeHierarchyBounds(const AActor* RootActor);
	
	float  CaptureDistance = 300.f;
	const float DefaultCaptureDistance = 300.0f;
	float  CaptureHeight = 0.f;
	bool   bIsCapturing = true;

	void SetCaptureDistance(float NewDistance) noexcept;
	void SetCaptureHeight(float NewHeight) noexcept;
	
	
private:
	static void GatherRenderableComponents(AActor* RootActor, TArray<UPrimitiveComponent*>& OutComponents);
	ASceneCapture2D* CreateCaptureActor(AActor* TargetActor, int32 CaptureIndex) const;

	FString LastOutputDirectory;
	TArray<TWeakObjectPtr<ASceneCapture2D>> CaptureActors;
};
