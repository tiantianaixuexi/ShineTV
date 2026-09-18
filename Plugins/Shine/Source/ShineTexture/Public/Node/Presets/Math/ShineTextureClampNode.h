#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureClampNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureClampNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureClampNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    float GetMinValue() const;
    float GetMaxValue() const;
    void SetMinValue(float InMinValue);
    void SetMaxValue(float InMaxValue);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MinValue = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MaxValue = 1.0f;
};
