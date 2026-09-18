#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureToFloatNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureToFloatNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureToFloatNode();

protected:
    virtual void BuildNodePins() override;
};
