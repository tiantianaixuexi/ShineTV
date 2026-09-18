#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ShineCharacterAssetFactory.generated.h"

/**
 * 在 Content Browser 里新建"Shine 角色"。
 *
 * 角色资产存在的意义是**跨镜头身份一致**：参考图散在每个分镜里的话，改一次形象要改所有分镜；
 * 做成资产后分镜只写 `@char:<角色>`，改形象只改一处。
 */
UCLASS()
class SHINEEDITOR_API UShineCharacterAssetFactory : public UFactory
{
    GENERATED_BODY()

public:
    UShineCharacterAssetFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
