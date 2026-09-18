#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureFloorNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureFloorNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureFloorNode();

protected:
    virtual void BuildNodePins() override;
};
