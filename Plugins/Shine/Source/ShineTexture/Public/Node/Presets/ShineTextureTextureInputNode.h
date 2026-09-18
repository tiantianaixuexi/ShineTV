#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "ShineTextureTextureInputNode.generated.h"

class UTexture;
class UTexture2D;

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureTextureInputNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureTextureInputNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    UTexture2D* GetSourceTexture() const;
    void SetSourceTexture(UTexture2D* InSourceTexture);

    virtual UTexture* GetInlinePreviewTexture() const override;

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Node")
    TObjectPtr<UTexture2D> SourceTexture;
};
