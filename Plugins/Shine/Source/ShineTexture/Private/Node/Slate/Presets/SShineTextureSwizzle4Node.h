#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureSwizzle4Node;

class SShineTextureSwizzle4Node : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureSwizzle4Node) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureSwizzle4Node* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    UShineTextureSwizzle4Node* GetSwizzleNode() const;
};
