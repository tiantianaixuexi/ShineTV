#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureBlendNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureBlendNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureBlendNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    float GetBlendFactor() const;
    void SetBlendFactor(float InBlendFactor);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BlendFactor = 0.5f;
};