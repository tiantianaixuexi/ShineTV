#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureNoiseNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureNoiseNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureNoiseNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    float GetScale() const;
    int32 GetOctaves() const;
    void SetScale(float InScale);
    void SetOctaves(int32 InOctaves);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float Scale = 64.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "1", ClampMax = "8"))
    int32 Octaves = 4;
};