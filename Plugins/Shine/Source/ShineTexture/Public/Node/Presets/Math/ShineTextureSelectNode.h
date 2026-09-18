#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureSelectNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureSelectNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureSelectNode();

protected:
    virtual void BuildNodePins() override;
};
