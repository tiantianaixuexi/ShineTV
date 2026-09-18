#include "Comfy/Builders/Models/ShineComfyModelLibraryBuilder.h"

namespace
{
    FString NormalizeLookupKey(const FString& Value)
    {
        return Value.ToLower().Replace(TEXT("-"), TEXT("_")).Replace(TEXT(" "), TEXT("_"));
    }

    const FShineComfyModelFolder* FindModelFolder(const TArray<FShineComfyModelFolder>& ModelFolders, const FString& FolderName)
    {
        const FString NormalizedFolderName = NormalizeLookupKey(FolderName);
        return ModelFolders.FindByPredicate([&NormalizedFolderName](const FShineComfyModelFolder& Folder)
        {
            return NormalizeLookupKey(Folder.FolderName) == NormalizedFolderName;
        });
    }
}

void FShineComfyModelLibraryBuilder::FillModelFolderFromJsonArray(const FString& FolderName, const TArray<TSharedPtr<FJsonValue>>& JsonArray, FShineComfyModelFolder& OutFolder)
{
    OutFolder.FolderName = FolderName;
    OutFolder.ModelNames.Reset();

    for (const TSharedPtr<FJsonValue>& ModelValue : JsonArray)
    {
        FString ModelName;
        if (ModelValue.IsValid() && ModelValue->TryGetString(ModelName))
        {
            OutFolder.ModelNames.Add(MoveTemp(ModelName));
        }
    }

    OutFolder.ModelNames.Sort();
}

void FShineComfyModelLibraryBuilder::ApplyModelLibraryToNodeDefinitions(TArray<FShineComfyNodeDefinition>& NodeDefinitions, const TArray<FShineComfyModelFolder>& ModelFolders)
{
    for (FShineComfyNodeDefinition& NodeDefinition : NodeDefinitions)
    {
        for (FShineComfyNodeParameterDefinition& ParameterDefinition : NodeDefinition.Parameters)
        {
            if (ParameterDefinition.OptionsSource != EShineComfyParameterOptionsSource::ModelFolder)
            {
                continue;
            }

            const FShineComfyModelFolder* ModelFolder = FindModelFolder(ModelFolders, ParameterDefinition.OptionsSourceName);
            if (!ModelFolder)
            {
                continue;
            }

            ParameterDefinition.StringOptions = ModelFolder->ModelNames;
            if (ParameterDefinition.StringOptions.Num() > 0 && !ParameterDefinition.StringOptions.Contains(ParameterDefinition.StringValue))
            {
                ParameterDefinition.StringValue = ParameterDefinition.StringOptions[0];
            }
        }
    }
}