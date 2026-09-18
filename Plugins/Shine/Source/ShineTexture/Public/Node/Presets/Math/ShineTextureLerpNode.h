#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureLerpNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureLerpNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureLerpNode();

protected:
    virtual void BuildNodePins() override;
};
