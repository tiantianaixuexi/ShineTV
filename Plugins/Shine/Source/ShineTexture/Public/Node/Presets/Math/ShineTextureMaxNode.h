#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureMaxNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureMaxNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureMaxNode();

protected:
    virtual void BuildNodePins() override;
};
