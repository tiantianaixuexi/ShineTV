#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureFracNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureFracNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureFracNode();

protected:
    virtual void BuildNodePins() override;
};
