#include "Editor/ShineAIPaintAssetEditor.h"

#include "Asset/ShineAIPaintAsset.h"
#include "Editor/ShineAIPaintEditorTabs.h"
#include "Editor/SShineAIPaintEditorPanel.h"
#include "Paint/ShineAIPaintSession.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "FShineAIPaintAssetEditor"

const FName FShineAIPaintAssetEditor::ViewportTabId(TEXT("ShineAIPaintEditor.Viewport"));
const FName FShineAIPaintAssetEditor::PlaceTabId(TEXT("ShineAIPaintEditor.Place"));
const FName FShineAIPaintAssetEditor::DetailsTabId(TEXT("ShineAIPaintEditor.Details"));
const FName FShineAIPaintAssetEditor::CanvasTabId(TEXT("ShineAIPaintEditor.Canvas"));

FShineAIPaintAssetEditor::~FShineAIPaintAssetEditor()
{
    // 关掉编辑器时也把像素写回资产，避免最后几笔丢失。
    if (Session.IsValid())
    {
        Session->SaveToAsset();
        Session.Reset();
    }
}

void FShineAIPaintAssetEditor::InitShineAIPaintAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UShineAIPaintAsset* InAsset)
{
    EditingAsset = InAsset;

    if (EditingAsset)
    {
        // 遮罩缓冲跟着目标贴图尺寸走。
        const FIntPoint TargetSize = EditingAsset->GetTargetSize();
        EditingAsset->EnsureMaskBuffer(TargetSize.X, TargetSize.Y);
    }

    Session = FShineAIPaintSession::CreateForAsset(EditingAsset);
    if (Session.IsValid())
    {
        Session->LoadFromAsset();
    }

    // 布局：左「放置」| 中间上下（视口 / 画布）| 右「属性」，三个分隔条都能拖。
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("ShineAIPaintAssetEditorLayout_v2"))
        ->AddArea
        (
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Horizontal)
            ->Split
            (
                FTabManager::NewStack()
                ->SetSizeCoefficient(0.20f)
                ->AddTab(PlaceTabId, ETabState::OpenedTab)
            )
            ->Split
            (
                FTabManager::NewSplitter()
                ->SetOrientation(Orient_Vertical)
                ->SetSizeCoefficient(0.58f)
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.72f)
                    ->AddTab(ViewportTabId, ETabState::OpenedTab)
                )
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.28f)
                    ->AddTab(CanvasTabId, ETabState::OpenedTab)
                )
            )
            ->Split
            (
                FTabManager::NewStack()
                ->SetSizeCoefficient(0.22f)
                ->AddTab(DetailsTabId, ETabState::OpenedTab)
            )
        );

    FAssetEditorToolkit::InitAssetEditor(
        Mode,
        InitToolkitHost,
        TEXT("ShineAIPaintAssetEditorApp"),
        Layout,
        true,
        true,
        InAsset);
}

void FShineAIPaintAssetEditor::SaveAsset_Execute()
{
    if (Session.IsValid())
    {
        Session->SaveToAsset();
    }

    FAssetEditorToolkit::SaveAsset_Execute();
}

FName FShineAIPaintAssetEditor::GetToolkitFName() const
{
    return TEXT("ShineAIPaintAssetEditor");
}

FText FShineAIPaintAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("ToolkitName", "Shine AI 贴图");
}

FString FShineAIPaintAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("ShineAIPaint");
}

FLinearColor FShineAIPaintAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.820f, 0.430f, 0.180f, 1.0f);
}

void FShineAIPaintAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    // SetGroup(WorkspaceMenuCategory) 之后，页签关掉也能从 Window 菜单里找回来。
    InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FShineAIPaintAssetEditor::SpawnViewportTab))
        .SetDisplayName(LOCTEXT("ViewportTabLabel", "视口"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(PlaceTabId, FOnSpawnTab::CreateSP(this, &FShineAIPaintAssetEditor::SpawnPlaceTab))
        .SetDisplayName(LOCTEXT("PlaceTabLabel", "放置灯光"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FShineAIPaintAssetEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTabLabel", "属性"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(CanvasTabId, FOnSpawnTab::CreateSP(this, &FShineAIPaintAssetEditor::SpawnCanvasTab))
        .SetDisplayName(LOCTEXT("CanvasTabLabel", "画布"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FShineAIPaintAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

    InTabManager->UnregisterTabSpawner(ViewportTabId);
    InTabManager->UnregisterTabSpawner(PlaceTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    InTabManager->UnregisterTabSpawner(CanvasTabId);
}

void FShineAIPaintAssetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(EditingAsset);
}

FString FShineAIPaintAssetEditor::GetReferencerName() const
{
    return TEXT("FShineAIPaintAssetEditor");
}

TSharedRef<SDockTab> FShineAIPaintAssetEditor::SpawnViewportTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("ViewportTabTitle", "视口"))
        [
            SNew(SShineAIPaintViewportTab)
            .Session(Session)
        ];
}

TSharedRef<SDockTab> FShineAIPaintAssetEditor::SpawnPlaceTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("PlaceTabTitle", "放置灯光"))
        [
            SNew(SShineAIPaintPlaceActorsTab)
            .Session(Session)
        ];
}

TSharedRef<SDockTab> FShineAIPaintAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("DetailsTabTitle", "属性"))
        [
            SNew(SShineAIPaintEditorPanel)
            .Session(Session)
        ];
}

TSharedRef<SDockTab> FShineAIPaintAssetEditor::SpawnCanvasTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("CanvasTabTitle", "画布"))
        [
            SNew(SShineAIPaintCanvasTab)
            .Session(Session)
        ];
}

#undef LOCTEXT_NAMESPACE
