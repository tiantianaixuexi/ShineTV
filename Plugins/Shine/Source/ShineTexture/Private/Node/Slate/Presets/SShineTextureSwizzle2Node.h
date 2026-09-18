#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureSwizzle2Node;

class SShineTextureSwizzle2Node : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureSwizzle2Node) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureSwizzle2Node* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureSwizzle2Node* GetSwizzleNode() const;
};
