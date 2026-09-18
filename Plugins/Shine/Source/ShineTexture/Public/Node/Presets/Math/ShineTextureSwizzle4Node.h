#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureSwizzle4Node.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureSwizzle4Node : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureSwizzle4Node();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    int32 GetXComponent() const;
    int32 GetYComponent() const;
    int32 GetZComponent() const;
    int32 GetWComponent() const;

    void SetXComponent(int32 InComponent);
    void SetYComponent(int32 InComponent);
    void SetZComponent(int32 InComponent);
    void SetWComponent(int32 InComponent);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
    int32 XComponent = 0;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
    int32 YComponent = 1;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
    int32 ZComponent = 2;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
    int32 WComponent = 3;
};
