#include "Tools/SixFaceCamera/ShineTools_SixFaceTool.h"

#include "Tools/SixFaceCamera/ShineTools_SixFaceVisualizer.h"
#include "Tools/SixFaceCamera/SSixFaceCameraViewportPreview.h"
#include "Tools/SixFaceCamera/SSixFaceCameraPanel.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"

FShineTools_SixFaceTool::FShineTools_SixFaceTool()
	: Visualizer(MakeUnique<FShineTools_SixFaceVisualizer>())
{
}

FShineTools_SixFaceTool::~FShineTools_SixFaceTool() = default;

// ---------------------------------------------------------------------------
// FShineBaseTool overrides
// ---------------------------------------------------------------------------

void FShineTools_SixFaceTool::Enter()
{
	if (Visualizer.IsValid() && Visualizer->bIsCapturing)
	{
		AddViewportOverlay();
	}
}

void FShineTools_SixFaceTool::Exit()
{
	if (Visualizer.IsValid())
	{
		Visualizer->Clear();
	}

	RemoveViewportOverlay();

	OnDataChanged.Clear();
}

FName FShineTools_SixFaceTool::GetSectionName() const
{
	return FName(TEXT("ComfyUI"));
}

void FShineTools_SixFaceTool::OnSelectionChanged()
{
	if (!Visualizer.IsValid())
	{
		return;
	}

	if (AActor* SelectedActor = GetSelectedActor())
	{
		Visualizer->UpdatePreviewForActor(SelectedActor);
	}
	else
	{
		Visualizer->Clear();
	}

	OnDataChanged.Broadcast();
}

TSharedRef<SWidget> FShineTools_SixFaceTool::CreatePanelWidget()
{
	return SNew(SSixFaceCameraPanel)
		.SixFaceTool(this);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

float FShineTools_SixFaceTool::GetCaptureDistance() const
{
	return Visualizer.IsValid() ? Visualizer->CaptureDistance : 0.f;
}

void FShineTools_SixFaceTool::SetCaptureDistance(float NewDistance)
{
	if (!Visualizer.IsValid())
	{
		return;
	}

	AActor* SelectedActor = GetSelectedActor();
	if (!SelectedActor)
	{
		Visualizer->Clear();
		OnDataChanged.Broadcast();
		return;
	}

	Visualizer->SetCaptureDistance(NewDistance);
	Visualizer->UpdatePreviewForActor(SelectedActor);
	OnDataChanged.Broadcast();
}

float FShineTools_SixFaceTool::GetCaptureHeight() const
{
	return Visualizer.IsValid() ? Visualizer->CaptureHeight : 0.f;
}

void FShineTools_SixFaceTool::SetCaptureHeight(float NewHeight)
{
	if (!Visualizer.IsValid())
	{
		return;
	}

	Visualizer->SetCaptureHeight(NewHeight);

	if (AActor* SelectedActor = GetSelectedActor())
	{
		Visualizer->UpdatePreviewForActor(SelectedActor);
	}
	OnDataChanged.Broadcast();
}

bool FShineTools_SixFaceTool::IsCapturing() const
{
	return Visualizer.IsValid() ? Visualizer->bIsCapturing : false;
}

void FShineTools_SixFaceTool::SetIsCapturing(bool bNewIsCapturing)
{
	if (!Visualizer.IsValid())
	{
		return;
	}

	Visualizer->bIsCapturing = bNewIsCapturing;

	if (!bNewIsCapturing)
	{
		Visualizer->Clear();
		RemoveViewportOverlay();
	}
	else
	{
		AddViewportOverlay();
		if (AActor* SelectedActor = GetSelectedActor())
		{
			Visualizer->UpdatePreviewForActor(SelectedActor);
		}
	}
	OnDataChanged.Broadcast();
}

const FString& FShineTools_SixFaceTool::GetLastOutputDirectory() const
{
	static const FString Empty;
	return Visualizer.IsValid() ? Visualizer->GetLastOutputDirectory() : Empty;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void FShineTools_SixFaceTool::GenerateForSelection()
{
	if (!Visualizer.IsValid())
	{
		return;
	}

	if (AActor* SelectedActor = GetSelectedActor())
	{
		Visualizer->GenerateForActor(SelectedActor);
	}
	else
	{
		Visualizer->Clear();
	}

	OnDataChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

AActor* FShineTools_SixFaceTool::GetSelectedActor()
{
	if (!GEditor)
	{
		return nullptr;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection || Selection->Num() == 0)
	{
		return nullptr;
	}

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			return Actor;
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// Viewport overlay
// ---------------------------------------------------------------------------

void FShineTools_SixFaceTool::AddViewportOverlay()
{
	if (ViewportOverlayWidget.IsValid())
	{
		return;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	OverlayViewport = LevelEditorModule.GetFirstActiveViewport();
	if (!OverlayViewport.IsValid() || !Visualizer.IsValid())
	{
		return;
	}

	ViewportOverlayWidget =
		SNew(SSixFaceCameraViewportPreview)
		.Visualizer(Visualizer.Get());
	OverlayViewport->AddOverlayWidget(ViewportOverlayWidget.ToSharedRef(), 20);
}

void FShineTools_SixFaceTool::RemoveViewportOverlay()
{
	if (OverlayViewport.IsValid() && ViewportOverlayWidget.IsValid())
	{
		OverlayViewport->RemoveOverlayWidget(ViewportOverlayWidget.ToSharedRef());
	}

	OverlayViewport.Reset();
	ViewportOverlayWidget.Reset();
}
