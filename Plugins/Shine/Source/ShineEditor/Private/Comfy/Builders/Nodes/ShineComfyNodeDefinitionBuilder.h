#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"

class FJsonObject;

class FShineComfyNodeDefinitionBuilder
{
public:
    static bool ParseNodeDefinitions(const TSharedPtr<FJsonObject>& RootObject, TArray<FShineComfyNodeDefinition>& OutNodes, FString& OutErrorMessage);

private:
    static bool ParseInputSection(const TSharedPtr<FJsonObject>& InputSection, bool bIsOptional, FShineComfyNodeDefinition& InOutDefinition);
};