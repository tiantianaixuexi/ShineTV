#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Node/ShineTextureNodeConstants.h"
#include "ShineTextureOutputNode.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureOutputNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTextureOutputNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
    virtual FIntPoint GetInlinePreviewSize() const override;
    virtual void SetInlinePreviewSize(FIntPoint InPreviewSize) override;
    int32 GetPreviewResolution() const;
    void SetPreviewResolution(int32 InPreviewResolution);

    virtual int32 GetInlinePreviewResolution() const override;
    virtual void SetInlinePreviewResolution(int32 InPreviewResolution) override;
    virtual UTexture* GetInlinePreviewTexture() const override;
    virtual UTextureRenderTarget2D* GetOrCreateInlinePreviewRenderTarget(FIntPoint Size) override;

protected:
    virtual void BuildNodePins() override;

private:
    // UHT requires numeric literals in UPROPERTY metadata, so the UI range stays literal here.
    UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "64", ClampMax = "4096", UIMin = "64", UIMax = "4096"))
    int32 PreviewResolution = ShineTextureNodeConstants::DefaultInternalPreviewResolution;
};
