#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureSubtractNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureSubtractNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureSubtractNode();

protected:
    virtual void BuildNodePins() override;
};
