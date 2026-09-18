#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureAppendVector3Node.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAppendVector3Node : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureAppendVector3Node();

protected:
    virtual void BuildNodePins() override;
};
