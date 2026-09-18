#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureFibers1Node.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureFibers1Node : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureFibers1Node();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    int32 GetTiling() const;
    bool GetNonSquareExpansion() const;
    void SetTiling(int32 InTiling);
    void SetNonSquareExpansion(bool bInNonSquareExpansion);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "16"))
    int32 Tiling = 4;

    UPROPERTY(EditAnywhere, Category = "Node")
    bool bNonSquareExpansion = false;
};