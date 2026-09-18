#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureLevelsNode;

class SShineTextureLevelsNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureLevelsNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureLevelsNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureLevelsNode* GetLevelsNode() const;
};
