#include "Asset/ShineComfyAssetFactory.h"

#include "Asset/ShineComfyAsset.h"

UShineComfyAssetFactory::UShineComfyAssetFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UShineComfyAsset::StaticClass();
}

UObject* UShineComfyAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UShineComfyAsset* NewAsset = NewObject<UShineComfyAsset>(InParent, Class, Name, Flags | RF_Transactional);
    NewAsset->GetOrCreateGraph();
    return NewAsset;
}
