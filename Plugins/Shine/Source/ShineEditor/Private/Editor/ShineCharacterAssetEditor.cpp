#include "Editor/ShineCharacterAssetEditor.h"

#include "Asset/ShineCharacterAsset.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FShineCharacterAssetEditor"

const FName FShineCharacterAssetEditor::DetailsTabId(TEXT("ShineCharacterAssetEditor.Details"));

void FShineCharacterAssetEditor::InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UShineCharacterAsset* InAsset)
{
    EditingAsset = InAsset;
    EditingAsset->Sanitize();

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(InAsset);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("ShineCharacterAssetEditorLayout_v1"))
        ->AddArea(
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Vertical)
            ->Split(
                FTabManager::NewStack()
                ->SetHideTabWell(true)
                ->AddTab(DetailsTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(
        Mode, InitToolkitHost, TEXT("ShineCharacterAssetEditorApp"), Layout, true, true, InAsset);
}

FName FShineCharacterAssetEditor::GetToolkitFName() const
{
    return TEXT("ShineCharacterAssetEditor");
}

FText FShineCharacterAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("ToolkitName", "Shine 角色");
}

FString FShineCharacterAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("ShineCharacter");
}

FLinearColor FShineCharacterAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.84f, 0.52f, 0.19f, 1.0f);
}

void FShineCharacterAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FShineCharacterAssetEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTabLabel", "角色参考图"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FShineCharacterAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
}

void FShineCharacterAssetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(EditingAsset);
}

FString FShineCharacterAssetEditor::GetReferencerName() const
{
    return TEXT("FShineCharacterAssetEditor");
}

TSharedRef<SDockTab> FShineCharacterAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("DetailsTabTitle", "角色参考图"))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f, 6.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .Padding(8.0f)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(LOCTEXT("ReferenceOrderHint",
                        "参考图的顺序 = 提示词里 <Picture 1..N> 的编号顺序（H3 的 tokenizer 按连接顺序自动编号，改顺序就会改 {{Mixed N}} 的指代）。\n"
                        "H3 的参考图上限是 9 张，超出的会被静默忽略，所以这里会先裁到 9 张（裁的是末尾，避免前面的编号整体错位）。"))
                ]
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                DetailsView.ToSharedRef()
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
