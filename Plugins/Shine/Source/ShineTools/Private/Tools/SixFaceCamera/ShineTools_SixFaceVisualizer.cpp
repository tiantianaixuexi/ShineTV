#include "Tools/SixFaceCamera/ShineTools_SixFaceVisualizer.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageWriteBlueprintLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Math/RotationMatrix.h"

namespace ShineToolsBoundsFace
{
	constexpr int32 CaptureResolution = 1024;
	constexpr FLinearColor BackgroundKeyColor(0.0f, 1.0f, 0.0f, 1.0f);
	constexpr float OrthoWidthScale = 2.0f;

	struct FFaceDefinition
	{
		const TCHAR* Label;
		FVector ViewDirection;
		FVector UpDirection;
	};

	const TArray<FFaceDefinition>& GetFaceDefinitions()
	{
		static const TArray<FFaceDefinition> Faces = {
			{TEXT("Front"), FVector::ForwardVector, FVector::UpVector},
			{TEXT("Back"), -FVector::ForwardVector, FVector::UpVector},
			{TEXT("Right"), FVector::RightVector, FVector::UpVector},
			{TEXT("Left"), -FVector::RightVector, FVector::UpVector},
			{TEXT("Top"), FVector::UpVector, FVector::ForwardVector},
			{TEXT("Bottom"), -FVector::UpVector, FVector::ForwardVector},
		};

		return Faces;
	}

	float SanitizeCaptureDistance(float CaptureDistance)
	{
		return FMath::Max(CaptureDistance, 1.0);
	}

	FString SanitizeFileName(const FString& InName)
	{
		FString Sanitized = InName;
		Sanitized.ReplaceInline(TEXT("\\"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT("/"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT(":"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT("*"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT("?"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT("\""), TEXT("_"));
		Sanitized.ReplaceInline(TEXT("<"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT(">"), TEXT("_"));
		Sanitized.ReplaceInline(TEXT("|"), TEXT("_"));
		return Sanitized;
	}
}

int32 FShineTools_SixFaceVisualizer::GetPreviewCount()
{
	return ShineToolsBoundsFace::GetFaceDefinitions().Num();
}

const TCHAR* FShineTools_SixFaceVisualizer::GetPreviewLabel(int32 PreviewIndex) const
{
	const TArray<ShineToolsBoundsFace::FFaceDefinition>& Faces = ShineToolsBoundsFace::GetFaceDefinitions();
	return Faces.IsValidIndex(PreviewIndex) ? Faces[PreviewIndex].Label : TEXT("");
}

UTextureRenderTarget2D* FShineTools_SixFaceVisualizer::GetPreviewRenderTarget(int32 PreviewIndex) const
{
	ASceneCapture2D* CaptureActor = CaptureActors.IsValidIndex(PreviewIndex) ? CaptureActors[PreviewIndex].Get() : nullptr;
	if (!CaptureActor)
	{
		return nullptr;
	}

	if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
	{
		return CaptureComponent->TextureTarget;
	}

	return nullptr;
}

void FShineTools_SixFaceVisualizer::Clear()
{
	for (const TWeakObjectPtr<ASceneCapture2D>& CaptureActor : CaptureActors)
	{
		if (CaptureActor.IsValid())
		{
			CaptureActor->Destroy();
		}
	}

	CaptureActors.Reset();
	LastOutputDirectory.Reset();
}

FBox FShineTools_SixFaceVisualizer::ComputeHierarchyBounds(const AActor* RootActor)
{
	FBox Bounds(ForceInit);
	if (!RootActor)
	{
		return Bounds;
	}

	TArray<AActor*> HierarchyActors;
	HierarchyActors.Add(const_cast<AActor*>(RootActor));

	TArray<AActor*> AttachedActors;
	RootActor->GetAttachedActors(AttachedActors, false, true);
	HierarchyActors.Append(AttachedActors);

	for (AActor* Actor : HierarchyActors)
	{
		if (!Actor)
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);

		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!Primitive || !Primitive->IsRegistered() || !Primitive->IsVisible())
			{
				continue;
			}

			if (!Primitive->IsA<UStaticMeshComponent>() && !Primitive->IsA<USkeletalMeshComponent>())
			{
				continue;
			}

			const FBoxSphereBounds& PrimitiveBounds = Primitive->Bounds;
			if (PrimitiveBounds.BoxExtent.IsNearlyZero())
			{
				continue;
			}

			Bounds += PrimitiveBounds.GetBox();
		}
	}

	if (!Bounds.IsValid)
	{
		Bounds = RootActor->GetComponentsBoundingBox(true, false);
	}

	return Bounds;
}

void FShineTools_SixFaceVisualizer::SetCaptureDistance(float NewDistance) noexcept
{
	const float ClampedDistance = FMath::Max(NewDistance, 1.0);
	if (FMath::IsNearlyEqual(CaptureDistance, ClampedDistance))
	{
		return;
	}

	CaptureDistance = ClampedDistance;
}

void FShineTools_SixFaceVisualizer::SetCaptureHeight(float NewHeight) noexcept
{
	if (FMath::IsNearlyEqual(CaptureHeight, NewHeight))
	{
		return;
	}

	CaptureHeight = NewHeight;
}

void FShineTools_SixFaceVisualizer::GatherRenderableComponents(AActor* RootActor, TArray<UPrimitiveComponent*>& OutComponents)
{
	OutComponents.Reset();
	if (!RootActor)
	{
		return;
	}

	TArray<AActor*> HierarchyActors;
	HierarchyActors.Add(RootActor);

	TArray<AActor*> AttachedActors;
	RootActor->GetAttachedActors(AttachedActors, false, true);
	HierarchyActors.Append(AttachedActors);

	for (AActor* Actor : HierarchyActors)
	{
		if (!Actor)
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!Primitive || !Primitive->IsRegistered() || !Primitive->IsVisible())
			{
				continue;
			}

			if (!Primitive->IsA<UStaticMeshComponent>() && !Primitive->IsA<USkeletalMeshComponent>())
			{
				continue;
			}

			OutComponents.Add(Primitive);
		}
	}
}

ASceneCapture2D* FShineTools_SixFaceVisualizer::CreateCaptureActor(
	AActor* TargetActor,
	int32 CaptureIndex) const
{
	if (!TargetActor || !TargetActor->GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.bHideFromSceneOutliner = true;

	ASceneCapture2D* CaptureActor = TargetActor->GetWorld()->SpawnActor<ASceneCapture2D>(
		ASceneCapture2D::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!CaptureActor)
	{
		return nullptr;
	}

	CaptureActor->SetFlags(RF_Transient);
	CaptureActor->SetActorHiddenInGame(true);

	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	if (!CaptureComponent)
	{
		CaptureActor->Destroy();
		return nullptr;
	}

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(CaptureActor, NAME_None, RF_Transient);
	RenderTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
	RenderTarget->ClearColor = ShineToolsBoundsFace::BackgroundKeyColor;
	RenderTarget->InitAutoFormat(ShineToolsBoundsFace::CaptureResolution, ShineToolsBoundsFace::CaptureResolution);
	RenderTarget->UpdateResourceImmediate(true);

	CaptureActor->SetActorLabel(FString::Printf(TEXT("ShineToolsPreview_%d"), CaptureIndex));
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	CaptureComponent->ShowFlags.SetAtmosphere(false);
	CaptureComponent->ShowFlags.SetFog(false);
	CaptureComponent->ShowFlags.SetVolumetricFog(false);
	CaptureComponent->ShowFlags.SetPostProcessing(true);

	if (UMaterial* PPMMaterial = LoadObject<UMaterial>(nullptr, TEXT("Material'/Shine/Tools/Materials/M_SceneCapCutOff.M_SceneCapCutOff'")))
	{
		FWeightedBlendable Blendable(1.0f, PPMMaterial);
		CaptureComponent->PostProcessSettings.WeightedBlendables.Array.Add(Blendable);
	}else
	{
		UE_LOG(LogTemp, Warning, TEXT("ShineToolsPreview_%d"), CaptureIndex);
	}

	return CaptureActor;
}

bool FShineTools_SixFaceVisualizer::UpdatePreviewForActor(AActor* TargetActor)
{
	if (!TargetActor || !TargetActor->GetWorld())
	{
		return false;
	}

	if (!bIsCapturing)
	{
		return false;
	}

	TArray<UPrimitiveComponent*> ShowOnlyComponents;
	GatherRenderableComponents(TargetActor, ShowOnlyComponents);
	if (ShowOnlyComponents.IsEmpty())
	{
		return false;
	}

	LastOutputDirectory.Reset();

	const FVector Center = TargetActor->GetActorLocation();
	const float SanitizedCaptureDistance = ShineToolsBoundsFace::SanitizeCaptureDistance(CaptureDistance);
	const float OrthoWidth = SanitizedCaptureDistance * ShineToolsBoundsFace::OrthoWidthScale;
	const TArray<ShineToolsBoundsFace::FFaceDefinition>& Faces = ShineToolsBoundsFace::GetFaceDefinitions();

	if (CaptureActors.Num() > Faces.Num())
	{
		for (int32 CaptureIndex = Faces.Num(); CaptureIndex < CaptureActors.Num(); ++CaptureIndex)
		{
			if (CaptureActors[CaptureIndex].IsValid())
			{
				CaptureActors[CaptureIndex]->Destroy();
			}
		}
		CaptureActors.SetNum(Faces.Num());
	}
	else if (CaptureActors.Num() < Faces.Num())
	{
		CaptureActors.SetNum(Faces.Num());
	}

	bool bUpdatedAny = false;
	for (int32 CaptureIndex = 0; CaptureIndex < Faces.Num(); ++CaptureIndex)
	{
		const ShineToolsBoundsFace::FFaceDefinition& Face = Faces[CaptureIndex];
		FVector CameraLocation = Center + Face.ViewDirection * SanitizedCaptureDistance;
		CameraLocation.Z += CaptureHeight;
		const FRotator CameraRotation = FRotationMatrix::MakeFromXZ(-Face.ViewDirection, Face.UpDirection).Rotator();

		ASceneCapture2D* CaptureActor = CaptureActors[CaptureIndex].Get();
		if (!CaptureActor)
		{
			CaptureActor = CreateCaptureActor(TargetActor, CaptureIndex);
			CaptureActors[CaptureIndex] = CaptureActor;
		}

		if (!CaptureActor)
		{
			continue;
		}

		CaptureActor->SetActorLocationAndRotation(CameraLocation, CameraRotation);
		if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
		{
			CaptureComponent->OrthoWidth = OrthoWidth;
			CaptureComponent->ShowOnlyComponents.Reset();
			for (UPrimitiveComponent* Component : ShowOnlyComponents)
			{
				CaptureComponent->ShowOnlyComponents.Add(Component);
			}
			CaptureComponent->CaptureScene();

			bUpdatedAny = true;
		}
	}

	return bUpdatedAny;
}

bool FShineTools_SixFaceVisualizer::GenerateForActor(AActor* TargetActor)
{
	if (!UpdatePreviewForActor(TargetActor))
	{
		return false;
	}

	const FString ActorName = ShineToolsBoundsFace::SanitizeFileName(
		TargetActor->GetActorLabel().IsEmpty() ? TargetActor->GetName() : TargetActor->GetActorLabel());
	LastOutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShineTools"), TEXT("BoundsCaptures"), ActorName);
	IFileManager::Get().MakeDirectory(*LastOutputDirectory, true);

	const TArray<ShineToolsBoundsFace::FFaceDefinition>& Faces = ShineToolsBoundsFace::GetFaceDefinitions();
	bool bExported = false;
	for (int32 CaptureIndex = 0; CaptureIndex < Faces.Num(); ++CaptureIndex)
	{
		ASceneCapture2D* CaptureActor = CaptureActors.IsValidIndex(CaptureIndex) ? CaptureActors[CaptureIndex].Get() : nullptr;
		if (!CaptureActor)
		{
			continue;
		}

		if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
		{
			CaptureComponent->CaptureScene();

			const FString FilePath = FPaths::Combine(
				LastOutputDirectory,
				FString::Printf(TEXT("%s_%s.png"), *ActorName, Faces[CaptureIndex].Label));

			FImageWriteOptions Options;
			Options.Format = EDesiredImageFormat::PNG;
			Options.bOverwriteFile = true;
			UImageWriteBlueprintLibrary::ExportToDisk(CaptureComponent->TextureTarget, FilePath, Options);
			bExported = true;
		}
	}

	if (!bExported)
	{
		LastOutputDirectory.Reset();
	}

	return bExported;
}
