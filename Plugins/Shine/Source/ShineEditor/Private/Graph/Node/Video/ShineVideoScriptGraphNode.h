#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "ShineVideoScriptGraphNode.generated.h"

/**
 * 剧本节点。
 *
 * 定位刻意收窄（见 PLAN 3.6）：**只存文本，并作为全局风格/设定注入每个分镜的提示词**。
 * 不做"剧本 → 自动拆分镜"——那需要接文本模型，属于另一件事。
 *
 * 注入规则（编译器实现，画布上看得见）：分镜节点的「剧本」输入接到哪个剧本节点，
 * 就把那个节点的 `Style` 拼到该分镜提示词最前面、`Scene`/`Period` 拼成一句
 * 环境说明拼在后面。**只注入连了线的**，不做"全图广播"——否则图上多写几个剧本草稿
 * 会互相污染，而且看画布根本看不出来谁影响了谁。
 */
UCLASS()
class UShineVideoScriptGraphNode : public UShineVideoGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoScriptGraphNode();

    virtual FText GetVideoNodeKind() const override;
    virtual FName GetNodePreset() const override;
    virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

protected:
    virtual void BuildNodePins() override;
};

namespace ShineVideoScriptNode
{
    const FName Preset(TEXT("VideoScript"));

    const FName TitleParameter(TEXT("Title"));
    const FName PeriodParameter(TEXT("Period"));
    const FName SceneParameter(TEXT("Scene"));
    const FName StyleParameter(TEXT("Style"));
    const FName BodyParameter(TEXT("Body"));
    const FName ForeshadowParameter(TEXT("Foreshadow"));

    const FName OutputPin(TEXT("Script"));
}
