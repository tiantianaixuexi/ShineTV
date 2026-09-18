#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureLessNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureLessNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureLessNode();

protected:
    virtual void BuildNodePins() override;
};
