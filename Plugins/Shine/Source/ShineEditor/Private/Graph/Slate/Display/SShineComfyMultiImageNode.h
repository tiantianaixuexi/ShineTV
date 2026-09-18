#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Graph/Slate/SShineComfyGraphStandardNode.h"
#include "UObject/StrongObjectPtr.h"

class SUniformGridPanel;
class UShineComfyGraphNodeBase;

class SShineComfyMultiImageNode : public SShineComfyGraphStandardNode
{
public:
    SLATE_BEGIN_ARGS(SShineComfyMultiImageNode) {}
    SLATE_END_ARGS()

    /** 参数用的是节点基类，这样 Preview 节点可以直接复用这套节点身体。 */
    void Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode);

    virtual void UpdateGraphNode() override;

private:
    void RebuildGalleryTiles();
    int32 GetGalleryImageCount() const;
    int32 GetGalleryColumns() const;
    bool GetShowFrameNumbers() const;

    TSharedPtr<SUniformGridPanel> GalleryGrid;

    /** 缩略图用到的贴图和画刷：Slate 只拿裸指针，必须自己保活。 */
    TArray<TStrongObjectPtr<UTexture2D>> TileTextures;
    TArray<TSharedPtr<FSlateBrush>> TileBrushes;
};