#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShineTextureAsset.generated.h"

class UShineTextureGraph;
class UTexture;
class UTextureRenderTarget2D;

UCLASS(BlueprintType)
class SHINETEXTUREEDITOR_API UShineTextureAsset : public UObject
{
    GENERATED_BODY()

public:
    UShineTextureAsset();

    UShineTextureGraph* GetOrCreateGraph();
    UTexture* GetPreviewTexture() const;
    UTextureRenderTarget2D* GetOrCreatePreviewRenderTarget(FIntPoint Size);
    UTextureRenderTarget2D* GetOrCreatePreviewRenderTarget(int32 Size);

    virtual void PostLoad() override;

    UPROPERTY(VisibleAnywhere, Instanced, Category = "Graph")
    TObjectPtr<UShineTextureGraph> Graph;

    UPROPERTY(VisibleAnywhere, Transient, Category = "Preview")
    TObjectPtr<UTextureRenderTarget2D> PreviewTexture;

private:
    void CreateDefaultGraph();
};