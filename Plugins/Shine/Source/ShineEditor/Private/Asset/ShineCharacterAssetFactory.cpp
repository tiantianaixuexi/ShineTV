#include "Asset/ShineCharacterAssetFactory.h"

#include "Asset/ShineCharacterAsset.h"

UShineCharacterAssetFactory::UShineCharacterAssetFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UShineCharacterAsset::StaticClass();
}

UObject* UShineCharacterAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UShineCharacterAsset* NewCharacter = NewObject<UShineCharacterAsset>(InParent, Class, Name, Flags | RF_Transactional);

    // 显示名默认取资产名：`@char:<显示名>` 的匹配靠它，先用资产名省得手工再填一遍。
    NewCharacter->Sanitize();

    return NewCharacter;
}
