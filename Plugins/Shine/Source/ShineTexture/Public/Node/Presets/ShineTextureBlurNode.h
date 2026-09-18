#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureBlurNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureBlurNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureBlurNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    float GetRadius() const;
    void SetRadius(float InRadius);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "32.0"))
    float Radius = 2.0f;
};
