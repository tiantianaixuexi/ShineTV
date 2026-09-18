#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/** Content Browser 里的"Shine 视频项目"：双击打开视频工作台。 */
class FShineVideoProjectTypeActions : public FAssetTypeActions_Base
{
public:
    explicit FShineVideoProjectTypeActions(uint32 InAssetCategory);

    virtual FText GetName() const override;
    virtual FColor GetTypeColor() const override;
    virtual UClass* GetSupportedClass() const override;
    virtual uint32 GetCategories() override;
    virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor) override;

private:
    uint32 AssetCategory;
};
