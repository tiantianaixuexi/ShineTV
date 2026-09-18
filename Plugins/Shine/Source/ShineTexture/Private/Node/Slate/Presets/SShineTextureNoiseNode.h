#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureNoiseNode;

class SShineTextureNoiseNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureNoiseNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureNoiseNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureNoiseNode* GetNoiseNode() const;
};