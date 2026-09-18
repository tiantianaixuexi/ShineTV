#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureLevelsNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureLevelsNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureLevelsNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    float GetInputLow() const;
    float GetInputHigh() const;
    float GetGamma() const;
    float GetOutputLow() const;
    float GetOutputHigh() const;
    void SetInputLow(float InValue);
    void SetInputHigh(float InValue);
    void SetGamma(float InValue);
    void SetOutputLow(float InValue);
    void SetOutputHigh(float InValue);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InputLow = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InputHigh = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.1", ClampMax = "4.0"))
    float Gamma = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OutputLow = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OutputHigh = 1.0f;
};
