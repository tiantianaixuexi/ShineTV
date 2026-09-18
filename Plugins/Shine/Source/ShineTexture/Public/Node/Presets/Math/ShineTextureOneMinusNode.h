#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureOneMinusNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureOneMinusNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureOneMinusNode();

protected:
    virtual void BuildNodePins() override;
};
