#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureAbsNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAbsNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureAbsNode();

protected:
    virtual void BuildNodePins() override;
};
