#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"
#include "Styling/SlateBrush.h"

struct FAssetData;
class UShineTextureTextureInputNode;

class SShineTextureTextureInputNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureTextureInputNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureTextureInputNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    const FSlateBrush* GetTexturePreviewBrush() const;
    FString GetTextureObjectPath() const;
    void HandleTextureChanged(const FAssetData& AssetData) const;
    UShineTextureTextureInputNode* GetTextureInputNode() const;

    mutable FSlateBrush TexturePreviewBrush;
};
