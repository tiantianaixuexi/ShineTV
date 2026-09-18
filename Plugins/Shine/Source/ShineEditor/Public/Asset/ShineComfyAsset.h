#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShineComfyAsset.generated.h"

class UShineComfyGraph;

UCLASS(BlueprintType, meta = (DisplayName = "Shine Comfy 图"))
class SHINEEDITOR_API UShineComfyAsset : public UObject
{
    GENERATED_BODY()

public:
    UShineComfyAsset();

    UShineComfyGraph* GetOrCreateGraph();

    virtual void PostLoad() override;

    UPROPERTY(EditAnywhere, Category = "Comfy")
    FString ComfyBaseUrl = TEXT("http://127.0.0.1:8188");

    UPROPERTY(VisibleAnywhere, Instanced, Category = "Graph")
    TObjectPtr<UShineComfyGraph> Graph;

private:
    void CreateDefaultGraph();
};
