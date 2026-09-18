#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

class SGraphEditor;
class UShineTextureAsset;

class FShineTextureAssetEditor : public FAssetEditorToolkit, public FGCObject
{
public:
    virtual ~FShineTextureAssetEditor() override;

    void InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UShineTextureAsset* InAsset);

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

    virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

private:
    void BindGraphEditorCommands();
    void ExtendToolbar();
    bool CanExportGraphJson() const;
    void ExportGraphJsonToFile();
    void ExportGraphJsonToClipboard();
    bool CanImportGraphJsonFromFile() const;
    void ImportGraphJsonFromFile();
    bool CanImportGraphJsonFromClipboard() const;
    void ImportGraphJsonFromClipboard();
    bool CanDeleteSelectedNodes() const;
    void DeleteSelectedNodes();
    void HandleGraphChanged(const FEdGraphEditAction& InAction);
    void BindGraphEvents();
    void UnbindGraphEvents();

    TSharedRef<class SDockTab> SpawnGraphTab(const class FSpawnTabArgs& Args);
    TSharedRef<SWidget> CreateGraphEditorWidget();

    static const FName GraphTabId;

    TObjectPtr<UShineTextureAsset> EditingAsset;
    TSharedPtr<SGraphEditor> GraphEditor;
    TSharedPtr<FUICommandList> GraphEditorCommands;
    FDelegateHandle GraphChangedHandle;
};
