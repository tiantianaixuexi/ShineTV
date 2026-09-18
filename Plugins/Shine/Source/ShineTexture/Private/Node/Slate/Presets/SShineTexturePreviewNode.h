#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTexturePreviewNode;

class SShineTexturePreviewNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTexturePreviewNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTexturePreviewNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTexturePreviewNode* GetPreviewNode() const;
};
