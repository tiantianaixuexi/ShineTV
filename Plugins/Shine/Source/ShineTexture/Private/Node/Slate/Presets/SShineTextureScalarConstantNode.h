#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureScalarConstantNode;

class SShineTextureScalarConstantNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureScalarConstantNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureScalarConstantNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureScalarConstantNode* GetScalarNode() const;
};
