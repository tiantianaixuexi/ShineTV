#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureClampNode;

class SShineTextureClampNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureClampNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureClampNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureClampNode* GetClampNode() const;
};
