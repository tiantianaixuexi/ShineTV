#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureToIntNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureToIntNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureToIntNode();

protected:
    virtual void BuildNodePins() override;
};
