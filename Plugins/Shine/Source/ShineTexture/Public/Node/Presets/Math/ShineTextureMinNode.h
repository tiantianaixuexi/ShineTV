#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureMinNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureMinNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureMinNode();

protected:
    virtual void BuildNodePins() override;
};
