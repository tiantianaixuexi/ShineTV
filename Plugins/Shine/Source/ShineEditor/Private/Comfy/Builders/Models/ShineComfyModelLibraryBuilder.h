#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"

class FJsonValue;

class FShineComfyModelLibraryBuilder
{
public:
    static void FillModelFolderFromJsonArray(const FString& FolderName, const TArray<TSharedPtr<FJsonValue>>& JsonArray, FShineComfyModelFolder& OutFolder);
    static void ApplyModelLibraryToNodeDefinitions(TArray<FShineComfyNodeDefinition>& NodeDefinitions, const TArray<FShineComfyModelFolder>& ModelFolders);
};