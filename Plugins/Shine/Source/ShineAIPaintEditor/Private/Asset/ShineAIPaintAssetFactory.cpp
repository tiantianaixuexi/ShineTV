#include "Asset/ShineAIPaintAssetFactory.h"

#include "Asset/ShineAIPaintAsset.h"

UShineAIPaintAssetFactory::UShineAIPaintAssetFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UShineAIPaintAsset::StaticClass();
}

UObject* UShineAIPaintAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    // 新资产还没有目标贴图：进编辑器后先"新建一张贴图"或从材质里指认一张。
    return NewObject<UShineAIPaintAsset>(InParent, Class, Name, Flags | RF_Transactional);
}
