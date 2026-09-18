#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureMaskNode;

class SShineTextureMaskNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureMaskNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureMaskNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureMaskNode* GetMaskNode() const;
};
