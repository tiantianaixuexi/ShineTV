#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureScalarConstantNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureScalarConstantNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureScalarConstantNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    float GetValue() const;
    void SetValue(float InValue);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node")
    float Value = 0.0f;
};
