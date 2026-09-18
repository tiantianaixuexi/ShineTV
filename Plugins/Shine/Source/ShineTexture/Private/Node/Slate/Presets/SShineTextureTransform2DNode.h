#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureTransform2DNode;

class SShineTextureTransform2DNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureTransform2DNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureTransform2DNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    void HandleTilingChanged(ECheckBoxState NewState) const;
    ECheckBoxState GetTilingCheckState() const;
    UShineTextureTransform2DNode* GetTransform2DNode() const;
};