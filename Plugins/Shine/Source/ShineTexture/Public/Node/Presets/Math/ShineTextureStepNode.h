#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureStepNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureStepNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureStepNode();

protected:
    virtual void BuildNodePins() override;
};
