#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ShineTextureAssetFactory.generated.h"

UCLASS()
class SHINETEXTUREEDITOR_API UShineTextureAssetFactory : public UFactory
{
    GENERATED_BODY()

public:
    UShineTextureAssetFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};