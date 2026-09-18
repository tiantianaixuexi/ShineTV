#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureVector2ConstantNode;

class SShineTextureVector2ConstantNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureVector2ConstantNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureVector2ConstantNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureVector2ConstantNode* GetVector2Node() const;
};
