#include "Asset/ShineComfyAsset.h"

#include "Graph/ShineComfyGraph.h"
#include "Graph/ShineComfyGraphSchema.h"

UShineComfyAsset::UShineComfyAsset()
{
}

UShineComfyGraph* UShineComfyAsset::GetOrCreateGraph()
{
    CreateDefaultGraph();
    return Graph;
}

void UShineComfyAsset::PostLoad()
{
    Super::PostLoad();
    CreateDefaultGraph();
}

void UShineComfyAsset::CreateDefaultGraph()
{
    if (!Graph)
    {
        Graph = NewObject<UShineComfyGraph>(this, TEXT("ComfyGraph"), RF_Transactional);
    }

    if (Graph)
    {
        Graph->Schema = UShineComfyGraphSchema::StaticClass();
        Graph->bEditable = true;
    }
}
