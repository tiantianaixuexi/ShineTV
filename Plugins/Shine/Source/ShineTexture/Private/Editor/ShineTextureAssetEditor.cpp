#include "ShineTextureAssetEditor.h"

#include "Asset/ShineTextureAsset.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Commands/GenericCommands.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Graph/ShineTextureGraph.h"
#include "GraphEditAction.h"
#include "GraphEditor.h"
#include "Render/ShineTexturePreviewRenderer.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"

namespace
{
    FString GetJsonDialogDefaultDirectory()
    {
        const FString DefaultDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShineTexture"), TEXT("Json"));
        IFileManager::Get().MakeDirectory(*DefaultDirectory, true);
        return DefaultDirectory;
    }
}

const FName FShineTextureAssetEditor::GraphTabId(TEXT("ShineTextureEditor.Graph"));

FShineTextureAssetEditor::~FShineTextureAssetEditor()
{
    UnbindGraphEvents();
}

void FShineTextureAssetEditor::InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UShineTextureAsset* InAsset)
{
    EditingAsset = InAsset;
    EditingAsset->GetOrCreateGraph();
    BindGraphEditorCommands();
    BindGraphEvents();
    ShineTexturePreviewRenderer::RefreshAssetPreview(EditingAsset);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("ShineTextureAssetEditorLayout_v1"))
        ->AddArea(
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Vertical)
            ->Split(
                FTabManager::NewStack()
                ->SetHideTabWell(true)
                ->AddTab(GraphTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("ShineTextureAssetEditorApp"), Layout, true, true, InAsset);
    ExtendToolbar();
    RegenerateMenusAndToolbars();
}

void FShineTextureAssetEditor::BindGraphEditorCommands()
{
    if (!GraphEditorCommands.IsValid())
    {
        GraphEditorCommands = MakeShared<FUICommandList>();
    }

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Delete,
        FExecuteAction::CreateSP(this, &FShineTextureAssetEditor::DeleteSelectedNodes),
        FCanExecuteAction::CreateSP(this, &FShineTextureAssetEditor::CanDeleteSelectedNodes));

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Copy,
        FExecuteAction::CreateSP(this, &FShineTextureAssetEditor::ExportGraphJsonToClipboard),
        FCanExecuteAction::CreateSP(this, &FShineTextureAssetEditor::CanExportGraphJson));

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Paste,
        FExecuteAction::CreateSP(this, &FShineTextureAssetEditor::ImportGraphJsonFromClipboard),
        FCanExecuteAction::CreateSP(this, &FShineTextureAssetEditor::CanImportGraphJsonFromClipboard));
}

void FShineTextureAssetEditor::ExtendToolbar()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(GetToolMenuToolbarName()))
    {
        FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("ShineTextureJson"));
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineTextureExportGraphJson"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineTextureAssetEditor::ExportGraphJsonToFile),
                FCanExecuteAction::CreateSP(this, &FShineTextureAssetEditor::CanExportGraphJson)),
            NSLOCTEXT("FShineTextureAssetEditor", "ExportGraphJsonLabel", "Export JSON"),
            NSLOCTEXT("FShineTextureAssetEditor", "ExportGraphJsonTooltip", "Exports the current ShineTexture graph to a JSON file."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save")));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineTextureImportGraphJson"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineTextureAssetEditor::ImportGraphJsonFromFile),
                FCanExecuteAction::CreateSP(this, &FShineTextureAssetEditor::CanImportGraphJsonFromFile)),
            NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJsonLabel", "Import JSON"),
            NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJsonTooltip", "Opens a JSON file and rebuilds the ShineTexture graph from it."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen")));
    }
}

bool FShineTextureAssetEditor::CanExportGraphJson() const
{
    return EditingAsset && EditingAsset->GetOrCreateGraph();
}

void FShineTextureAssetEditor::ExportGraphJsonToFile()
{
    if (!EditingAsset)
    {
        return;
    }

    FString Json;
    UShineTextureGraph* Graph = EditingAsset->GetOrCreateGraph();
    if (!Graph || !Graph->ExportGraphToJson(Json))
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "ExportGraphJsonFailed", "Failed to export graph JSON."));
        return;
    }

    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "DesktopPlatformUnavailableForSave", "Desktop file dialogs are unavailable."));
        return;
    }

    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    const FString DefaultDirectory = GetJsonDialogDefaultDirectory();
    const FString DefaultFileName = FString::Printf(TEXT("%s.json"), *EditingAsset->GetName());

    TArray<FString> OutFilenames;
    const bool bPickedFile = DesktopPlatform->SaveFileDialog(
        ParentWindowHandle,
        TEXT("Export ShineTexture Graph JSON"),
        DefaultDirectory,
        DefaultFileName,
        TEXT("JSON Files (*.json)|*.json"),
        EFileDialogFlags::None,
        OutFilenames);

    if (!bPickedFile || OutFilenames.Num() == 0)
    {
        return;
    }

    if (!FFileHelper::SaveStringToFile(Json, *OutFilenames[0]))
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "ExportGraphJsonWriteFailed", "Failed to write the JSON file."));
    }
}

void FShineTextureAssetEditor::ExportGraphJsonToClipboard()
{
    if (!EditingAsset)
    {
        return;
    }

    FString Json;
    if (UShineTextureGraph* Graph = EditingAsset->GetOrCreateGraph())
    {
        if (Graph->ExportGraphToJson(Json))
        {
            FPlatformApplicationMisc::ClipboardCopy(*Json);
            return;
        }
    }

    FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "ExportGraphJsonFailed", "Failed to export graph JSON."));
}

bool FShineTextureAssetEditor::CanImportGraphJsonFromFile() const
{
    return EditingAsset && EditingAsset->GetOrCreateGraph();
}

bool FShineTextureAssetEditor::CanImportGraphJsonFromClipboard() const
{
    if (!EditingAsset)
    {
        return false;
    }

    FString ClipboardText;
    FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
    return ClipboardText.Contains(TEXT("\"format\":\"ShineTextureGraph\"")) || ClipboardText.Contains(TEXT("\"format\": \"ShineTextureGraph\""));
}

void FShineTextureAssetEditor::ImportGraphJsonFromFile()
{
    if (!EditingAsset)
    {
        return;
    }

    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "DesktopPlatformUnavailableForOpen", "Desktop file dialogs are unavailable."));
        return;
    }

    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    TArray<FString> SelectedFiles;
    const bool bPickedFile = DesktopPlatform->OpenFileDialog(
        ParentWindowHandle,
        TEXT("Import ShineTexture Graph JSON"),
        GetJsonDialogDefaultDirectory(),
        TEXT(""),
        TEXT("JSON Files (*.json)|*.json"),
        EFileDialogFlags::None,
        SelectedFiles);

    if (!bPickedFile || SelectedFiles.Num() == 0)
    {
        return;
    }

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *SelectedFiles[0]))
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJsonReadFailed", "Failed to read the selected JSON file."));
        return;
    }

    if (JsonText.IsEmpty())
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJsonFileEmpty", "The selected JSON file is empty."));
        return;
    }

    FString ErrorMessage;
    if (UShineTextureGraph* Graph = EditingAsset->GetOrCreateGraph())
    {
        const FScopedTransaction Transaction(NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJsonFromFile", "Import Shine Texture Graph JSON From File"));
        Graph->Modify();

        if (Graph->ImportGraphFromJson(JsonText, &ErrorMessage))
        {
            ShineTexturePreviewRenderer::RefreshAssetPreview(EditingAsset);
            return;
        }
    }

    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage.IsEmpty() ? TEXT("Failed to import graph JSON.") : ErrorMessage));
}

void FShineTextureAssetEditor::ImportGraphJsonFromClipboard()
{
    if (!EditingAsset)
    {
        return;
    }

    FString ClipboardText;
    FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
    if (ClipboardText.IsEmpty())
    {
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJsonEmpty", "Clipboard does not contain graph JSON."));
        return;
    }

    FString ErrorMessage;
    if (UShineTextureGraph* Graph = EditingAsset->GetOrCreateGraph())
    {
        const FScopedTransaction Transaction(NSLOCTEXT("FShineTextureAssetEditor", "ImportGraphJson", "Import Shine Texture Graph JSON"));
        Graph->Modify();

        if (Graph->ImportGraphFromJson(ClipboardText, &ErrorMessage))
        {
            ShineTexturePreviewRenderer::RefreshAssetPreview(EditingAsset);
            return;
        }
    }

    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage.IsEmpty() ? TEXT("Failed to import graph JSON.") : ErrorMessage));
}

bool FShineTextureAssetEditor::CanDeleteSelectedNodes() const
{
    if (!GraphEditor.IsValid())
    {
        return false;
    }

    const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
    for (UObject* SelectedObject : SelectedNodes)
    {
        if (const UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(SelectedObject))
        {
            if (SelectedNode->CanUserDeleteNode())
            {
                return true;
            }
        }
    }

    return false;
}

void FShineTextureAssetEditor::DeleteSelectedNodes()
{
    if (!GraphEditor.IsValid() || !EditingAsset)
    {
        return;
    }

    const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
    if (SelectedNodes.IsEmpty())
    {
        return;
    }

    const FScopedTransaction Transaction(NSLOCTEXT("FShineTextureAssetEditor", "DeleteSelectedNodes", "Delete Shine Texture Nodes"));

    if (UShineTextureGraph* Graph = EditingAsset->GetOrCreateGraph())
    {
        Graph->Modify();
    }

    GraphEditor->ClearSelectionSet();

    for (UObject* SelectedObject : SelectedNodes)
    {
        if (UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(SelectedObject))
        {
            if (!SelectedNode->CanUserDeleteNode())
            {
                continue;
            }

            SelectedNode->Modify();
            SelectedNode->DestroyNode();
        }
    }
}

FName FShineTextureAssetEditor::GetToolkitFName() const
{
    return TEXT("ShineTextureAssetEditor");
}

FText FShineTextureAssetEditor::GetBaseToolkitName() const
{
    return NSLOCTEXT("FShineTextureAssetEditor", "ToolkitName", "Shine Texture Graph");
}

FString FShineTextureAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("ShineTexture");
}

FLinearColor FShineTextureAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.150f, 0.430f, 0.810f, 1.0f);
}

void FShineTextureAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FShineTextureAssetEditor::SpawnGraphTab))
        .SetDisplayName(NSLOCTEXT("FShineTextureAssetEditor", "GraphTabLabel", "Graph"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FShineTextureAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(GraphTabId);
}

void FShineTextureAssetEditor::HandleGraphChanged(const FEdGraphEditAction& InAction)
{
    if (!EditingAsset)
    {
        return;
    }

    const bool bOnlySelectionChanged =
        (InAction.Action & GRAPHACTION_SelectNode) != 0 &&
        (InAction.Action & (GRAPHACTION_AddNode | GRAPHACTION_RemoveNode | GRAPHACTION_EditNode)) == 0;
    if (bOnlySelectionChanged)
    {
        return;
    }

    ShineTexturePreviewRenderer::RefreshAssetPreview(EditingAsset, &InAction);
}

void FShineTextureAssetEditor::BindGraphEvents()
{
    UnbindGraphEvents();

    if (UShineTextureGraph* Graph = EditingAsset ? EditingAsset->GetOrCreateGraph() : nullptr)
    {
        GraphChangedHandle = Graph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &FShineTextureAssetEditor::HandleGraphChanged));
    }
}

void FShineTextureAssetEditor::UnbindGraphEvents()
{
    if (GraphChangedHandle.IsValid())
    {
        if (UShineTextureGraph* Graph = EditingAsset ? EditingAsset->Graph : nullptr)
        {
            Graph->RemoveOnGraphChangedHandler(GraphChangedHandle);
        }

        GraphChangedHandle.Reset();
    }
}

void FShineTextureAssetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(EditingAsset);
}

FString FShineTextureAssetEditor::GetReferencerName() const
{
    return TEXT("FShineTextureAssetEditor");
}

TSharedRef<SDockTab> FShineTextureAssetEditor::SpawnGraphTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(NSLOCTEXT("FShineTextureAssetEditor", "GraphTabTitle", "Graph"))
        [
            CreateGraphEditorWidget()
        ];
}

TSharedRef<SWidget> FShineTextureAssetEditor::CreateGraphEditorWidget()
{
    if (!GraphEditor.IsValid())
    {
        FGraphAppearanceInfo AppearanceInfo;
        AppearanceInfo.CornerText = NSLOCTEXT("FShineTextureAssetEditor", "GraphCornerText", "Shine Texture");

        GraphEditor = SNew(SGraphEditor)
            .Appearance(AppearanceInfo)
            .AdditionalCommands(GraphEditorCommands)
            .GraphToEdit(EditingAsset ? EditingAsset->GetOrCreateGraph() : nullptr)
            .AutoExpandActionMenu(true)
            .ShowGraphStateOverlay(false);
    }

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
        .Padding(4.0f)
        [
            GraphEditor.ToSharedRef()
        ];
}

