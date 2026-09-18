#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ShineVideoProjectFactory.generated.h"

/**
 * 在 Content Browser 里新建"Shine 视频项目"。
 *
 * 新建出来不是一片空白：默认带一个空分镜（标题「分镜 1」），因为一个没有任何分镜的
 * 项目连"哪里该填东西"都看不出来，而 H3 的参数（分辨率/帧数/CFG）都已经在
 * FShineVideoShot 上带好了 P0 实测的默认值。
 */
UCLASS()
class SHINEEDITOR_API UShineVideoProjectFactory : public UFactory
{
    GENERATED_BODY()

public:
    UShineVideoProjectFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
