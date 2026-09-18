#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ShineComfyAssetFactory.generated.h"

UCLASS()
class SHINEEDITOR_API UShineComfyAssetFactory : public UFactory
{
    GENERATED_BODY()

public:
    UShineComfyAssetFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
