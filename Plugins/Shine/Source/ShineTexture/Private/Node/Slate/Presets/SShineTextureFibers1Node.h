#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureFibers1Node;

class SShineTextureFibers1Node : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureFibers1Node) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureFibers1Node* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    void HandleNonSquareExpansionChanged(ECheckBoxState NewState) const;
    ECheckBoxState GetNonSquareExpansionState() const;
    UShineTextureFibers1Node* GetFibers1Node() const;
};