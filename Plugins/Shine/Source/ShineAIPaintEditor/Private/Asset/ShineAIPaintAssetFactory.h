#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ShineAIPaintAssetFactory.generated.h"

/** 在内容浏览器里新建「Shine AI 贴图」资产。 */
UCLASS()
class SHINEAIPAINTEDITOR_API UShineAIPaintAssetFactory : public UFactory
{
    GENERATED_BODY()

public:
    UShineAIPaintAssetFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
