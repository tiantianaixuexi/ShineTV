#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureChannelPackNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureChannelPackNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureChannelPackNode();

    float GetDefaultAlpha() const;
    void SetDefaultAlpha(float InDefaultAlpha);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DefaultAlpha = 1.0f;
};
