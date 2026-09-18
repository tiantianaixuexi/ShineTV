#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureSolidColorNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureSolidColorNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureSolidColorNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    FLinearColor GetColorValue() const;
    void SetColorValue(FLinearColor InColorValue);

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node")
    FLinearColor ColorValue = FLinearColor::White;
};