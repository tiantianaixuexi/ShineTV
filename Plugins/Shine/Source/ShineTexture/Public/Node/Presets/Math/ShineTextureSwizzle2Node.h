#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureSwizzle2Node.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureSwizzle2Node : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureSwizzle2Node();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    int32 GetFirstComponent() const;
    int32 GetSecondComponent() const;
    void SetFirstComponent(int32 InComponent);
    void SetSecondComponent(int32 InComponent);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
    int32 FirstComponent = 0;

    UPROPERTY(EditAnywhere, Category = "Node", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
    int32 SecondComponent = 1;
};
