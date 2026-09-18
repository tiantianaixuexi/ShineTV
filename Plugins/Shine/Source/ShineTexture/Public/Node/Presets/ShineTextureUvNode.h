#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureUvNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureUvNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureUvNode();

protected:
    virtual void BuildNodePins() override;
};
