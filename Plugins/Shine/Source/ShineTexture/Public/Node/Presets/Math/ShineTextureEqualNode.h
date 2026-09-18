#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureEqualNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureEqualNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureEqualNode();

protected:
    virtual void BuildNodePins() override;
};
