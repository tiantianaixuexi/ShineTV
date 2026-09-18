#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureAddNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAddNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureAddNode();

protected:
    virtual void BuildNodePins() override;
};
