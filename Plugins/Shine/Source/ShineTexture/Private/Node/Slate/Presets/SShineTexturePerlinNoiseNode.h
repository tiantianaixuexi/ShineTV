#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTexturePerlinNoiseNode;

class SShineTexturePerlinNoiseNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTexturePerlinNoiseNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTexturePerlinNoiseNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    void HandleNonSquareExpansionChanged(ECheckBoxState NewState) const;
    ECheckBoxState GetNonSquareExpansionState() const;

    UShineTexturePerlinNoiseNode* GetPerlinNoiseNode() const;
};
