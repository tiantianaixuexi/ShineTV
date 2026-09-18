#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureModuloNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureModuloNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureModuloNode();

protected:
    virtual void BuildNodePins() override;
};
