#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureBlurNode;

class SShineTextureBlurNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureBlurNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureBlurNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureBlurNode* GetBlurNode() const;
};
