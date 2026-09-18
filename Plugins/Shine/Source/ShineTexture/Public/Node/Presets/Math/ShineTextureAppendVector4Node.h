#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureAppendVector4Node.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAppendVector4Node : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureAppendVector4Node();

protected:
    virtual void BuildNodePins() override;
};
