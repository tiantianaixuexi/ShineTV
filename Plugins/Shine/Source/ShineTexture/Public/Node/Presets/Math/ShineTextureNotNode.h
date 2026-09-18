#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureNotNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureNotNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureNotNode();

protected:
    virtual void BuildNodePins() override;
};
