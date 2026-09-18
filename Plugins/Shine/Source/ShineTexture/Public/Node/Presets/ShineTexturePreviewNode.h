#pragma once

#include "CoreMinimal.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Node/ShineTextureNodeConstants.h"
#include "ShineTexturePreviewNode.generated.h"

class UTexture;
class UTextureRenderTarget2D;

UCLASS()
class SHINETEXTUREEDITOR_API UShineTexturePreviewNode : public UShineTextureGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineTexturePreviewNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    int32 GetPreviewResolution() const;
    void SetPreviewResolution(int32 InPreviewResolution);
    UTexture* GetPreviewTexture() const;
    UTextureRenderTarget2D* GetOrCreatePreviewRenderTarget(int32 Size);
    UTextureRenderTarget2D* GetOrCreatePreviewRenderTarget(FIntPoint Size);

    virtual FIntPoint GetInlinePreviewSize() const override;
    virtual void SetInlinePreviewSize(FIntPoint InPreviewSize) override;
    virtual int32 GetInlinePreviewResolution() const override;
    virtual void SetInlinePreviewResolution(int32 InPreviewResolution) override;
    virtual UTexture* GetInlinePreviewTexture() const override;
    virtual UTextureRenderTarget2D* GetOrCreateInlinePreviewRenderTarget(FIntPoint Size) override;

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "64", ClampMax = "1024", UIMin = "64", UIMax = "1024"))
    int32 PreviewResolution = ShineTextureNodeConstants::DefaultInternalPreviewResolution;

    UPROPERTY(VisibleAnywhere, Transient, Category = "Preview")
    TObjectPtr<UTextureRenderTarget2D> PreviewTexture;
};
