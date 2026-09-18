#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureGreaterNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureGreaterNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureGreaterNode();

protected:
    virtual void BuildNodePins() override;
};
