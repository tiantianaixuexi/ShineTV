#include "Editor/ShineComfyAssetEditor.h"

#include "Asset/ShineComfyAsset.h"
#include "DesktopPlatformModule.h"
#include "Graph/ShineComfyGraph.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SShineMainPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
    void ShowEditorNotification(const FText& Message, bool bSuccess)
    {
        FNotificationInfo Info(Message);
        Info.ExpireDuration = bSuccess ? 3.0f : 8.0f;
        Info.bUseSuccessFailIcons = true;
        if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
        {
            Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
        }
    }
}

const FName FShineComfyAssetEditor::GraphTabId(TEXT("ShineComfyAssetEditor.Graph"));

void FShineComfyAssetEditor::InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UShineComfyAsset* InAsset)
{
    EditingAsset = InAsset;
    EditingAsset->GetOrCreateGraph();

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("ShineComfyAssetEditorLayout_v1"))
        ->AddArea(
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Vertical)
            ->Split(
                FTabManager::NewStack()
                ->SetHideTabWell(true)
                ->AddTab(GraphTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("ShineComfyAssetEditorApp"), Layout, true, true, InAsset);
    ExtendToolbar();
    RegenerateMenusAndToolbars();
}

FName FShineComfyAssetEditor::GetToolkitFName() const
{
    return TEXT("ShineComfyAssetEditor");
}

FText FShineComfyAssetEditor::GetBaseToolkitName() const
{
    return NSLOCTEXT("FShineComfyAssetEditor", "ToolkitName", "Shine Comfy 图");
}

FString FShineComfyAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("ShineComfy");
}

FLinearColor FShineComfyAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.145f, 0.424f, 0.812f, 1.0f);
}

void FShineComfyAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FShineComfyAssetEditor::SpawnGraphTab))
        .SetDisplayName(NSLOCTEXT("FShineComfyAssetEditor", "GraphTabLabel", "Comfy 图"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FShineComfyAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(GraphTabId);
}

void FShineComfyAssetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(EditingAsset);
}

FString FShineComfyAssetEditor::GetReferencerName() const
{
    return TEXT("FShineComfyAssetEditor");
}

void FShineComfyAssetEditor::ExtendToolbar()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(GetToolMenuToolbarName()))
    {
        FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("ShineComfyGraph"));
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineComfyExportGraphJson"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineComfyAssetEditor::ExportGraphJsonToOutput),
                FCanExecuteAction::CreateSP(this, &FShineComfyAssetEditor::CanOperateOnGraph)),
            NSLOCTEXT("FShineComfyAssetEditor", "ExportGraphJsonLabel", "导出 Graph JSON"),
            NSLOCTEXT("FShineComfyAssetEditor", "ExportGraphJsonTooltip", "将当前图导出为 Graph JSON 并显示到输出面板。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save")));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineComfyImportGraphJson"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineComfyAssetEditor::ImportGraphJsonFromFileDialog),
                FCanExecuteAction::CreateSP(this, &FShineComfyAssetEditor::CanOperateOnGraph)),
            NSLOCTEXT("FShineComfyAssetEditor", "ImportGraphJsonLabel", "导入 Graph JSON"),
            NSLOCTEXT("FShineComfyAssetEditor", "ImportGraphJsonTooltip", "从 JSON 文件重建整张图（支持 Shine Graph JSON、ComfyUI workflow、ComfyUI API prompt）。当前图内容会被替换。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import")));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineComfyExportDagJson"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineComfyAssetEditor::ExportExecutionPlanToOutput),
                FCanExecuteAction::CreateSP(this, &FShineComfyAssetEditor::CanOperateOnGraph)),
            NSLOCTEXT("FShineComfyAssetEditor", "ExportDagJsonLabel", "导出 DAG JSON"),
            NSLOCTEXT("FShineComfyAssetEditor", "ExportDagJsonTooltip", "将当前图导出为 DAG JSON 并显示到输出面板。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save")));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineComfyExecuteGraph"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineComfyAssetEditor::ExecuteGraph),
                FCanExecuteAction::CreateSP(this, &FShineComfyAssetEditor::CanOperateOnGraph)),
            NSLOCTEXT("FShineComfyAssetEditor", "ExecuteGraphLabel", "运行图"),
            NSLOCTEXT("FShineComfyAssetEditor", "ExecuteGraphTooltip", "执行当前图并将结果显示到输出面板。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Toolbar.Play")));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineComfyZoomToFit"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineComfyAssetEditor::ZoomToFitGraph),
                FCanExecuteAction::CreateSP(this, &FShineComfyAssetEditor::CanOperateOnGraph)),
            NSLOCTEXT("FShineComfyAssetEditor", "ZoomToFitLabel", "缩放到适合"),
            NSLOCTEXT("FShineComfyAssetEditor", "ZoomToFitTooltip", "将当前图缩放到合适视图。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.ZoomToFit")));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineComfyDeleteSelectedNodes"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineComfyAssetEditor::DeleteSelectedNodes),
                FCanExecuteAction::CreateSP(this, &FShineComfyAssetEditor::CanDeleteSelectedNodes)),
            NSLOCTEXT("FShineComfyAssetEditor", "DeleteSelectedNodesLabel", "删除节点"),
            NSLOCTEXT("FShineComfyAssetEditor", "DeleteSelectedNodesTooltip", "删除当前选中的节点。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")));
    }
}

bool FShineComfyAssetEditor::CanOperateOnGraph() const
{
    return MainPanelWidget.IsValid() && EditingAsset && EditingAsset->GetOrCreateGraph();
}

bool FShineComfyAssetEditor::CanDeleteSelectedNodes() const
{
    return MainPanelWidget.IsValid() && MainPanelWidget->CanDeleteSelectedNodes();
}

void FShineComfyAssetEditor::ExportGraphJsonToOutput()
{
    if (MainPanelWidget.IsValid())
    {
        MainPanelWidget->ExportGraphJsonToOutput();
    }
}

void FShineComfyAssetEditor::ImportGraphJsonFromFileDialog()
{
    UShineComfyGraph* Graph = EditingAsset ? EditingAsset->GetOrCreateGraph() : nullptr;
    if (!Graph)
    {
        return;
    }

    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return;
    }

    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    TArray<FString> OpenedFiles;
    if (!DesktopPlatform->OpenFileDialog(ParentWindowHandle,
        TEXT("选择 Graph JSON"),
        FPaths::ProjectContentDir(),
        TEXT(""),
        TEXT("JSON 文件 (*.json)|*.json|所有文件 (*.*)|*.*"),
        0,
        OpenedFiles) || OpenedFiles.Num() == 0)
    {
        return;
    }

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *OpenedFiles[0]))
    {
        ShowEditorNotification(NSLOCTEXT("FShineComfyAssetEditor", "ImportGraphJsonReadFailed", "读取 JSON 文件失败。"), false);
        return;
    }

    FString ErrorMessage;
    if (Graph->ImportGraphJson(JsonText, ErrorMessage))
    {
        ShowEditorNotification(NSLOCTEXT("FShineComfyAssetEditor", "ImportGraphJsonSucceeded", "Graph JSON 导入成功。"), true);
    }
    else
    {
        ShowEditorNotification(FText::FromString(FString::Printf(TEXT("Graph JSON 导入失败：%s"), *ErrorMessage)), false);
    }
}

void FShineComfyAssetEditor::ExportExecutionPlanToOutput()
{
    if (MainPanelWidget.IsValid())
    {
        MainPanelWidget->ExportExecutionPlanToOutput();
    }
}

void FShineComfyAssetEditor::ExecuteGraph()
{
    if (MainPanelWidget.IsValid())
    {
        MainPanelWidget->ExecuteGraphToOutput();
    }
}

void FShineComfyAssetEditor::ZoomToFitGraph()
{
    if (MainPanelWidget.IsValid())
    {
        MainPanelWidget->ZoomToFitGraph();
    }
}

void FShineComfyAssetEditor::DeleteSelectedNodes()
{
    if (MainPanelWidget.IsValid())
    {
        MainPanelWidget->DeleteSelectedNodes();
    }
}

TSharedRef<SDockTab> FShineComfyAssetEditor::SpawnGraphTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(NSLOCTEXT("FShineComfyAssetEditor", "GraphTabTitle", "Comfy 图"))
        [
            SAssignNew(MainPanelWidget, SShineMainPanel)
            .Graph(EditingAsset ? EditingAsset->GetOrCreateGraph() : nullptr)
        ];
}
