#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTexturePerlinNoiseNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTexturePerlinNoiseNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTexturePerlinNoiseNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    int32 GetScale() const;
    float GetDisorder() const;
    float GetDisorderSpeed() const;
    FVector2D GetTileOffset() const;
    bool GetNonSquareExpansion() const;

    void SetScale(int32 InScale);
    void SetDisorder(float InDisorder);
    void SetDisorderSpeed(float InDisorderSpeed);
    void SetTileOffset(const FVector2D& InTileOffset);
    void SetNonSquareExpansion(bool bInNonSquareExpansion);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "1", ClampMax = "256", UIMin = "1", UIMax = "256"))
    int32 Scale = 16;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float Disorder = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float DisorderSpeed = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Node")
    FVector2D TileOffset = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Node")
    bool bNonSquareExpansion = false;
};
