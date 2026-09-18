#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureBreakOutNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureBreakOutNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureBreakOutNode();

protected:
    virtual void BuildNodePins() override;
};
