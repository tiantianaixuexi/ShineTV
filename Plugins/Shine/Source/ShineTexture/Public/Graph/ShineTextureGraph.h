#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "ShineTextureGraph.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureGraph : public UEdGraph
{
    GENERATED_BODY()

public:
    bool ExportGraphToJson(FString& OutJson) const;
    bool ImportGraphFromJson(const FString& InJson, FString* OutErrorMessage = nullptr);
};
