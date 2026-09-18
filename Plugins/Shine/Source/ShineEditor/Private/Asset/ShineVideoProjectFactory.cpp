#include "Asset/ShineVideoProjectFactory.h"

#include "Asset/ShineVideoProject.h"

UShineVideoProjectFactory::UShineVideoProjectFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UShineVideoProject::StaticClass();
}

UObject* UShineVideoProjectFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UShineVideoProject* NewProject = NewObject<UShineVideoProject>(InParent, Class, Name, Flags | RF_Transactional);

    NewProject->ProjectName = Name.ToString();

    // 留一个空分镜：新建出来就能直接往里写提示词，且采样参数已经是 P0 实测的默认值。
    FShineVideoShot FirstShot;
    FirstShot.Title = TEXT("分镜 1");
    NewProject->Shots.Add(MoveTemp(FirstShot));

    return NewProject;
}
