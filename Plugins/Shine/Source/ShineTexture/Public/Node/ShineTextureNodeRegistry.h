#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class SGraphNode;
class UEdGraphNode;
class UShineTextureGraphNodeBase;

struct FShineTextureNodeRegistration
{
    TSubclassOf<UShineTextureGraphNodeBase> NodeClass;
    FText MenuLabel;
    FText Tooltip;
    FText Category;
};

namespace ShineTextureNodeRegistry
{
    const TArray<FShineTextureNodeRegistration>& GetNodeRegistrations();
}
