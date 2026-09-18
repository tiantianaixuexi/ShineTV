#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureSqrtNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureSqrtNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureSqrtNode();

protected:
    virtual void BuildNodePins() override;
};
