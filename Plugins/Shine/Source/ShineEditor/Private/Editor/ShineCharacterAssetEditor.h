#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

class UShineCharacterAsset;
class IDetailsView;

/**
 * Shine 角色的编辑器：一页属性表。
 *
 * 角色资产没有"流程"可看（它只是参考图的集合 + 一段固定身份描述），所以不值得给它造面板，
 * 双击能看到、能改参考图顺序就够了。真正要紧的是**顺序**：H3 的 `<Picture i>` 标签是
 * tokenizer 按连接顺序自动生成的，调整参考图顺序会直接改变提示词里 `{{Mixed N}}` 的指代。
 */
class FShineCharacterAssetEditor : public FAssetEditorToolkit, public FGCObject
{
public:
    void InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UShineCharacterAsset* InAsset);

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

    virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

private:
    TSharedRef<class SDockTab> SpawnDetailsTab(const class FSpawnTabArgs& Args);

    static const FName DetailsTabId;

    TObjectPtr<UShineCharacterAsset> EditingAsset;
    TSharedPtr<IDetailsView> DetailsView;
};
