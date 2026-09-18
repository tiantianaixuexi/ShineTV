#include "Asset/ShineComfyAssetTypeActions.h"

#include "Asset/ShineComfyAsset.h"
#include "Editor/ShineComfyAssetEditor.h"

FShineComfyAssetTypeActions::FShineComfyAssetTypeActions(uint32 InAssetCategory)
    : AssetCategory(InAssetCategory)
{
}

FText FShineComfyAssetTypeActions::GetName() const
{
    return NSLOCTEXT("FShineComfyAssetTypeActions", "AssetTypeName", "Shine Comfy 图");
}

FColor FShineComfyAssetTypeActions::GetTypeColor() const
{
    return FColor(37, 108, 207);
}

UClass* FShineComfyAssetTypeActions::GetSupportedClass() const
{
    return UShineComfyAsset::StaticClass();
}

uint32 FShineComfyAssetTypeActions::GetCategories()
{
    return AssetCategory;
}

void FShineComfyAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
    const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

    for (UObject* Object : InObjects)
    {
        if (UShineComfyAsset* Asset = Cast<UShineComfyAsset>(Object))
        {
            TSharedRef<FShineComfyAssetEditor> AssetEditor = MakeShared<FShineComfyAssetEditor>();
            AssetEditor->InitAssetEditor(Mode, EditWithinLevelEditor, Asset);
        }
    }
}
