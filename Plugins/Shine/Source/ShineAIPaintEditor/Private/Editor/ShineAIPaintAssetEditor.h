#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

class FShineAIPaintSession;
class UShineAIPaintAsset;

/**
 * 「Shine AI 贴图」资产的编辑器。
 *
 * 四个可停靠页签（和静态网格体编辑器一个套路）：
 *   视口 / 放置灯光 / 属性 / 画布。
 * 页签都能拖、能关，关掉后可以从 Window 菜单里找回来。
 */
class FShineAIPaintAssetEditor : public FAssetEditorToolkit, public FGCObject
{
public:
    virtual ~FShineAIPaintAssetEditor() override;

    void InitShineAIPaintAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UShineAIPaintAsset* InAsset);

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

    virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

    /** 点"保存"时先把会话里的像素写回资产。 */
    virtual void SaveAsset_Execute() override;

private:
    TSharedRef<class SDockTab> SpawnViewportTab(const class FSpawnTabArgs& Args);
    TSharedRef<class SDockTab> SpawnPlaceTab(const class FSpawnTabArgs& Args);
    TSharedRef<class SDockTab> SpawnDetailsTab(const class FSpawnTabArgs& Args);
    TSharedRef<class SDockTab> SpawnCanvasTab(const class FSpawnTabArgs& Args);

    static const FName ViewportTabId;
    static const FName PlaceTabId;
    static const FName DetailsTabId;
    static const FName CanvasTabId;

    TObjectPtr<UShineAIPaintAsset> EditingAsset;
    TSharedPtr<FShineAIPaintSession> Session;
};
