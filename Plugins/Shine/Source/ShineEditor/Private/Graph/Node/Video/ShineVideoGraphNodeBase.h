#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "ShineVideoGraphNodeBase.generated.h"

/**
 * 视频域六类节点的共同基类。
 *
 * 它只加三件事，别的（pin、参数、标题、结果图路径、自绘控件）全部继承
 * `UShineComfyGraphNodeBase`——那套已经跑通过 Comfy 图那条线，重写一遍只会引入回归。
 *
 *   1. 参数装配的便捷函数（`Parameters` 数组在基类里是 protected，子类逐个手填太啰嗦）；
 *   2. 运行期状态字段（Transient，只给画布上色/显示步进用）；
 *   3. 一个"这个节点是什么"的短名字，编译报错时用它定位到具体节点。
 */
UCLASS(Abstract)
class UShineVideoGraphNodeBase : public UShineComfyGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoGraphNodeBase();

    /** 节点角色名（"分镜" / "视频组"…），用于编译日志与错误定位。 */
    virtual FText GetVideoNodeKind() const PURE_VIRTUAL(UShineVideoGraphNodeBase::GetVideoNodeKind, return FText::GetEmpty(););

    /**
     * 运行期状态回填：由 `FShineVideoGraphCompiler::ApplyTaskStatus` 在收到进度时调用。
     * 只改显示，不改数据。
     */
    void SetRuntimeState(EShineVideoNodeRuntimeState InState, const FString& InDetail, int32 InStep = 0, int32 InStepMax = 0);

    EShineVideoNodeRuntimeState GetRuntimeState() const { return RuntimeState; }
    const FString& GetRuntimeDetail() const { return RuntimeDetail; }
    int32 GetRuntimeStep() const { return RuntimeStep; }
    int32 GetRuntimeStepMax() const { return RuntimeStepMax; }

    /** 状态色的 Slate 侧取用点：绿=跑完、黄=在跑、红=出错、灰=没跑过。 */
    static FLinearColor GetStateColor(EShineVideoNodeRuntimeState State);

protected:
    /** 加一条文本参数。`bMultiLine` 决定节点上给单行输入框还是多行框。 */
    void AddTextParameter(FName ParameterName, const FString& Label, const FString& Value, bool bMultiLine = false);

    /** 加一条带下拉选项的文本参数（选项为空时退回普通文本框）。 */
    void AddOptionParameter(FName ParameterName, const FString& Label, const FString& Value, const TArray<FString>& Options);

    void AddFloatParameter(FName ParameterName, const FString& Label, double Value);
    void AddIntegerParameter(FName ParameterName, const FString& Label, int32 Value);
    void AddBoolParameter(FName ParameterName, const FString& Label, bool bValue);

    UPROPERTY(Transient)
    EShineVideoNodeRuntimeState RuntimeState = EShineVideoNodeRuntimeState::Idle;

    UPROPERTY(Transient)
    FString RuntimeDetail;

    UPROPERTY(Transient)
    int32 RuntimeStep = 0;

    UPROPERTY(Transient)
    int32 RuntimeStepMax = 0;
};
