#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

class UShineComfyAsset;
class SShineMainPanel;

class FShineComfyAssetEditor : public FAssetEditorToolkit, public FGCObject
{
public:
    void InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UShineComfyAsset* InAsset);

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

    virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

private:
    void ExtendToolbar();
    bool CanOperateOnGraph() const;
    bool CanDeleteSelectedNodes() const;
    void ExportGraphJsonToOutput();
    void ImportGraphJsonFromFileDialog();
    void ExportExecutionPlanToOutput();
    void ExecuteGraph();
    void ZoomToFitGraph();
    void DeleteSelectedNodes();
    TSharedRef<class SDockTab> SpawnGraphTab(const class FSpawnTabArgs& Args);

    static const FName GraphTabId;

    TObjectPtr<UShineComfyAsset> EditingAsset;
    TSharedPtr<SShineMainPanel> MainPanelWidget;
};
