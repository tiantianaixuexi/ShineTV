#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureVector2ConstantNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureVector2ConstantNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureVector2ConstantNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    FVector2D GetValue() const;
    void SetValue(FVector2D InValue);
    void SetValueX(float InValueX);
    void SetValueY(float InValueY);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node")
    FVector2D Value = FVector2D::ZeroVector;
};
