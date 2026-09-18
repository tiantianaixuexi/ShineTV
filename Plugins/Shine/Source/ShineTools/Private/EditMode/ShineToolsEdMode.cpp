#include "EditMode/ShineToolsEdMode.h"

#include "Tools/ShineBaseTool.h"
#include "Tools/SixFaceCamera/ShineTools_SixFaceTool.h"
#include "EditMode/ShineToolsToolkit.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Toolkits/ToolkitManager.h"

#define LOCTEXT_NAMESPACE "FShineToolsEdMode"

const FEditorModeID FShineToolsEdMode::EM_ShineTools(TEXT("EM_FShineTools"));

FShineToolsEdMode::FShineToolsEdMode()
{
	// Register tool(s) — add new tools here
	Tools.Add(MakeUnique<FShineTools_SixFaceTool>());
}

FShineToolsEdMode::~FShineToolsEdMode() = default;

FShineToolsEdMode* FShineToolsEdMode::Get()
{
	return static_cast<FShineToolsEdMode*>(GLevelEditorModeTools().GetActiveMode(EM_ShineTools));
}

void FShineToolsEdMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FShineToolsToolkit);
		if (Owner)
		{
			Toolkit->Init(Owner->GetToolkitHost());
		}
	}

	static_cast<FShineToolsToolkit*>(Toolkit.Get())->AddToolPanels(Tools);

	for (auto& Tool : Tools)
	{
		Tool->Enter();
	}

	if (USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr)
	{
		SelectionChangedHandle = Selection->SelectionChangedEvent.AddRaw(
			this,
			&FShineToolsEdMode::OnSelectionChanged);
	}

	OnSelectionChanged(nullptr);
}

void FShineToolsEdMode::Exit()
{
	if (USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr)
	{
		if (SelectionChangedHandle.IsValid())
		{
			Selection->SelectionChangedEvent.Remove(SelectionChangedHandle);
			SelectionChangedHandle.Reset();
		}
	}

	for (auto& Tool : Tools)
	{
		Tool->Exit();
	}

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

void FShineToolsEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	for (auto& Tool : Tools)
	{
		if (Tool->IsActive())
		{
			Tool->Tick();
			Tool->OnSelectionChanged();
		}
	}
}

void FShineToolsEdMode::OnSelectionChanged(UObject* /*NewSelection*/)
{
	for (auto& Tool : Tools)
	{
		if (Tool->IsActive())
		{
			Tool->OnSelectionChanged();
		}
	}
}

#undef LOCTEXT_NAMESPACE
