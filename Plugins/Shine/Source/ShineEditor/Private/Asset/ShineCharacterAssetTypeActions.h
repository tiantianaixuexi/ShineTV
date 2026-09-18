#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/** Content Browser 里的"Shine 角色"：双击打角色属性（参考图顺序 = <Picture N> 的顺序）。 */
class FShineCharacterAssetTypeActions : public FAssetTypeActions_Base
{
public:
    explicit FShineCharacterAssetTypeActions(uint32 InAssetCategory);

    virtual FText GetName() const override;
    virtual FColor GetTypeColor() const override;
    virtual UClass* GetSupportedClass() const override;
    virtual uint32 GetCategories() override;
    virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor) override;

private:
    uint32 AssetCategory;
};
