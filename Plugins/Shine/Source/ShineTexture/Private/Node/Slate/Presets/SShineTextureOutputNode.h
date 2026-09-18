#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureOutputNode;

class SShineTextureOutputNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureOutputNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureOutputNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;
};