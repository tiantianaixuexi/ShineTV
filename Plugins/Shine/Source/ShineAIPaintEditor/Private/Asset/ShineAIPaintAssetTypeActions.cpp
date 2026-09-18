#include "Asset/ShineAIPaintAssetTypeActions.h"

#include "Asset/ShineAIPaintAsset.h"
#include "Editor/ShineAIPaintAssetEditor.h"

FShineAIPaintAssetTypeActions::FShineAIPaintAssetTypeActions(uint32 InAssetCategory)
    : AssetCategory(InAssetCategory)
{
}

FText FShineAIPaintAssetTypeActions::GetName() const
{
    return NSLOCTEXT("FShineAIPaintAssetTypeActions", "AssetTypeName", "Shine AI 贴图");
}

FColor FShineAIPaintAssetTypeActions::GetTypeColor() const
{
    return FColor(210, 110, 45);
}

UClass* FShineAIPaintAssetTypeActions::GetSupportedClass() const
{
    return UShineAIPaintAsset::StaticClass();
}

uint32 FShineAIPaintAssetTypeActions::GetCategories()
{
    return AssetCategory;
}

void FShineAIPaintAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
    const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

    for (UObject* Object : InObjects)
    {
        if (UShineAIPaintAsset* Asset = Cast<UShineAIPaintAsset>(Object))
        {
            TSharedRef<FShineAIPaintAssetEditor> AssetEditor = MakeShared<FShineAIPaintAssetEditor>();
            AssetEditor->InitShineAIPaintAssetEditor(Mode, EditWithinLevelEditor, Asset);
        }
    }
}
