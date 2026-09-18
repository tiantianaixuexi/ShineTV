#include "Asset/ShineCharacterAssetTypeActions.h"

#include "Asset/ShineCharacterAsset.h"
#include "Editor/ShineCharacterAssetEditor.h"

FShineCharacterAssetTypeActions::FShineCharacterAssetTypeActions(uint32 InAssetCategory)
    : AssetCategory(InAssetCategory)
{
}

FText FShineCharacterAssetTypeActions::GetName() const
{
    return NSLOCTEXT("FShineCharacterAssetTypeActions", "AssetTypeName", "Shine 角色");
}

FColor FShineCharacterAssetTypeActions::GetTypeColor() const
{
    return FColor(214, 132, 48);
}

UClass* FShineCharacterAssetTypeActions::GetSupportedClass() const
{
    return UShineCharacterAsset::StaticClass();
}

uint32 FShineCharacterAssetTypeActions::GetCategories()
{
    return AssetCategory;
}

void FShineCharacterAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
    const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

    for (UObject* Object : InObjects)
    {
        if (UShineCharacterAsset* Character = Cast<UShineCharacterAsset>(Object))
        {
            const TSharedRef<FShineCharacterAssetEditor> CharacterEditor = MakeShared<FShineCharacterAssetEditor>();
            CharacterEditor->InitAssetEditor(Mode, EditWithinLevelEditor, Character);
        }
    }
}
