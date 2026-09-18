#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureAppendVector2Node.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAppendVector2Node : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureAppendVector2Node();

protected:
    virtual void BuildNodePins() override;
};
