#include "Asset/ShineVideoProjectTypeActions.h"

#include "Asset/ShineVideoProject.h"
#include "Editor/ShineVideoGraphEditor.h"

FShineVideoProjectTypeActions::FShineVideoProjectTypeActions(uint32 InAssetCategory)
    : AssetCategory(InAssetCategory)
{
}

FText FShineVideoProjectTypeActions::GetName() const
{
    return NSLOCTEXT("FShineVideoProjectTypeActions", "AssetTypeName", "Shine 视频项目");
}

FColor FShineVideoProjectTypeActions::GetTypeColor() const
{
    // 与"Shine Comfy 图"（蓝）区分开：项目走青绿，角色走橙。
    return FColor(32, 156, 148);
}

UClass* FShineVideoProjectTypeActions::GetSupportedClass() const
{
    return UShineVideoProject::StaticClass();
}

uint32 FShineVideoProjectTypeActions::GetCategories()
{
    return AssetCategory;
}

void FShineVideoProjectTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
    const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

    for (UObject* Object : InObjects)
    {
        if (UShineVideoProject* Project = Cast<UShineVideoProject>(Object))
        {
            // 打开的是"工作台"（画布 + 项目属性 + 流程），而不是一个分镜属性表：
            // 分镜表是画布的编译产物，直接手编它迟早会和画布对不上。
            const TSharedRef<FShineVideoGraphEditor> GraphEditor = MakeShared<FShineVideoGraphEditor>();
            GraphEditor->InitAssetEditor(Mode, EditWithinLevelEditor, Project);
        }
    }
}
