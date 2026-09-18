#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureBlendNode;

class SShineTextureBlendNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureBlendNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureBlendNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureBlendNode* GetBlendNode() const;
};