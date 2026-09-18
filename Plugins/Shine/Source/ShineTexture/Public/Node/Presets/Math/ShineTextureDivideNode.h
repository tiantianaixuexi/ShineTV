#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureDivideNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureDivideNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureDivideNode();

protected:
    virtual void BuildNodePins() override;
};
