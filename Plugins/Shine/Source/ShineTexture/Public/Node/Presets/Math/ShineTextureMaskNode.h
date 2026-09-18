#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureMaskNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureMaskNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureMaskNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    bool GetMaskX() const;
    bool GetMaskY() const;
    bool GetMaskZ() const;
    bool GetMaskW() const;

    void SetMaskX(bool bInMaskX);
    void SetMaskY(bool bInMaskY);
    void SetMaskZ(bool bInMaskZ);
    void SetMaskW(bool bInMaskW);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node")
    bool bMaskX = true;

    UPROPERTY(EditAnywhere, Category = "Node")
    bool bMaskY = true;

    UPROPERTY(EditAnywhere, Category = "Node")
    bool bMaskZ = false;

    UPROPERTY(EditAnywhere, Category = "Node")
    bool bMaskW = false;
};
