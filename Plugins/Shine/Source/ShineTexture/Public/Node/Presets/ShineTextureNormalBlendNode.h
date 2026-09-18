#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureNormalBlendNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureNormalBlendNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureNormalBlendNode();

    float GetDetailStrength() const;
    void SetDetailStrength(float InStrength);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float DetailStrength = 1.0f;
};
