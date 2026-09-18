#include "Asset/ShineTextureAssetFactory.h"

#include "Asset/ShineTextureAsset.h"

UShineTextureAssetFactory::UShineTextureAssetFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UShineTextureAsset::StaticClass();
}

UObject* UShineTextureAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UShineTextureAsset* NewAsset = NewObject<UShineTextureAsset>(InParent, Class, Name, Flags | RF_Transactional);
    NewAsset->GetOrCreateGraph();
    return NewAsset;
}