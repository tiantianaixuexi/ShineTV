#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureTransform2DNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureTransform2DNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureTransform2DNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    FVector2D GetOffset() const;
    FVector2D GetScale() const;
    float GetRotationDegrees() const;
    bool GetTilingEnabled() const;

    void SetOffset(FVector2D InOffset);
    void SetScale(FVector2D InScale);
    void SetRotationDegrees(float InRotationDegrees);
    void SetTilingEnabled(bool bInTilingEnabled);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node")
    FVector2D Offset = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.01", UIMin = "0.01"))
    FVector2D Scale = FVector2D(1.0, 1.0);

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "-180.0", ClampMax = "180.0", UIMin = "-180.0", UIMax = "180.0"))
    float RotationDegrees = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Node")
    bool bTilingEnabled = true;
};