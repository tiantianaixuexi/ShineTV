#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FShineAIPaintSession;
class UTexture2D;

/**
 * 「属性」页签：资产本身的参数。
 *
 * 目标贴图 / 画笔 / 遮罩 / AI 更新 四组，配「材质」和「预览网格」的资产挑选框。
 * 视口、2D 画布、放置灯光都是各自独立的可停靠页签。
 */
class SShineAIPaintEditorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineAIPaintEditorPanel) {}
        SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SShineAIPaintEditorPanel() override;

private:
    // ---- 布局 ----
    TSharedRef<SWidget> BuildTargetGroup();
    TSharedRef<SWidget> BuildBrushGroup();
    TSharedRef<SWidget> BuildMaskGroup();
    TSharedRef<SWidget> BuildAIGroup();

    // ---- 刷新 ----
    void HandleStructureChanged();
    void RefreshTargetList();

    /** 换了目标贴图（或网格）之后：让会话重新从资产读一次。 */
    void ReloadSessionFromAsset();

    // ---- 资产挑选 ----
    void HandlePickedTargetTexture(const FAssetData& AssetData);
    void HandlePickedSourceMaterial(const FAssetData& AssetData);
    void HandlePickedMesh(int32 Index, const FAssetData& AssetData);
    FReply HandleAddMeshSlot();
    FReply HandleRemoveMeshSlot(int32 Index);

    // ---- 动作 ----
    FReply HandleCreateTexture();
    FReply HandleScanMaterialTextures();
    FReply HandleUseMaterialTexture(int32 CandidateIndex);
    FReply HandleReloadTexture();
    FReply HandleWriteTexture();
    FReply HandleRequestAI();
    FReply HandleFetchCheckpoints();

    FString GetEffectiveServiceUrl() const;
    FString GetTargetTextureSummary() const;

    TSharedPtr<FShineAIPaintSession> Session;

    TSharedPtr<SVerticalBox> TargetListBox;

    TArray<TSharedPtr<FString>> SizeOptions;
    TArray<TSharedPtr<FString>> CheckpointOptions;
    TArray<FString> FetchedCheckpointNames;

    /** 从材质扫出来的贴图候选（与下拉里的字符串一一对应）。 */
    TArray<UTexture2D*> MaterialTextureCandidates;
    TArray<TSharedPtr<FString>> MaterialTextureOptions;
};
