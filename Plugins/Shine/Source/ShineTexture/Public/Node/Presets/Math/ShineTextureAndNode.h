#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureAndNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAndNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureAndNode();

protected:
    virtual void BuildNodePins() override;
};
