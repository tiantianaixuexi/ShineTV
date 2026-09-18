#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureOrNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureOrNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureOrNode();

protected:
    virtual void BuildNodePins() override;
};
