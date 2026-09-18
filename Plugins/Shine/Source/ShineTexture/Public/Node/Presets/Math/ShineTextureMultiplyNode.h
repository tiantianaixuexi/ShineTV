#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureMultiplyNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureMultiplyNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureMultiplyNode();

protected:
    virtual void BuildNodePins() override;
};
