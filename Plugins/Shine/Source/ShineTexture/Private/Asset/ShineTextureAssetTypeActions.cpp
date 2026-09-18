#include "Asset/ShineTextureAssetTypeActions.h"

#include "Asset/ShineTextureAsset.h"
#include "Editor/ShineTextureAssetEditor.h"

FShineTextureAssetTypeActions::FShineTextureAssetTypeActions(uint32 InAssetCategory)
    : AssetCategory(InAssetCategory)
{
}

FText FShineTextureAssetTypeActions::GetName() const
{
    return NSLOCTEXT("FShineTextureAssetTypeActions", "AssetTypeName", "Shine Texture Graph");
}

FColor FShineTextureAssetTypeActions::GetTypeColor() const
{
    return FColor(40, 133, 232);
}

UClass* FShineTextureAssetTypeActions::GetSupportedClass() const
{
    return UShineTextureAsset::StaticClass();
}

uint32 FShineTextureAssetTypeActions::GetCategories()
{
    return AssetCategory;
}

void FShineTextureAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
    const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

    for (UObject* Object : InObjects)
    {
        if (UShineTextureAsset* Asset = Cast<UShineTextureAsset>(Object))
        {
            TSharedRef<FShineTextureAssetEditor> AssetEditor = MakeShared<FShineTextureAssetEditor>();
            AssetEditor->InitAssetEditor(Mode, EditWithinLevelEditor, Asset);
        }
    }
}

