#pragma once

#include "CoreMinimal.h"
#include "Graph/ShineComfyGraphTypes.h"

class FJsonObject;

/**
 * JSON 驱动的导演台节点配置。
 *
 * 从 JSON 文件加载节点的参数定义、Pin 定义和导出扩展模板，
 * 使得添加/修改自定义节点只需改 JSON，无需改 C++。
 *
 * JSON 格式见 Plugins/Shine/Resources/DirectorNode.json。
 */
struct FShineComfyDirectorPinDef
{
    FString Name;
    FString Category;
};

struct FShineComfyDirectorExpansionNode
{
    FString Id;
    FString ClassType;
    /** inputs 字段的原始 JSON（延迟求值） */
    TSharedPtr<class FJsonObject> InputsJson;
    /** 可选条件：{"$paramTrue": "ParamName"} 表示仅当参数为 true 时才包含此节点 */
    TSharedPtr<class FJsonObject> ConditionJson;
};

struct FShineComfyDirectorExpansionOutput
{
    FString PinName;
    FString TargetNodeId;
    int32 OutputIndex = 0;
};

struct FShineComfyDirectorExpansion
{
    TArray<FShineComfyDirectorExpansionNode> Nodes;
    TArray<FShineComfyDirectorExpansionOutput> Outputs;
};

struct FShineComfyDirectorConfig
{
    FString Preset = TEXT("Director");
    FString DisplayName = TEXT("导演台");
    FString Description;
    FLinearColor AccentColor = FLinearColor(0.55f, 0.25f, 0.75f, 1.0f);
    TArray<FShineComfyNodeParameter> Parameters;
    TArray<FShineComfyDirectorPinDef> InputPins;
    TArray<FShineComfyDirectorPinDef> OutputPins;
    FShineComfyDirectorExpansion Expansion;
    bool bIsValid = false;

    /** 从 JSON 字符串解析配置 */
    static bool ParseFromJson(const FString& JsonText, FShineComfyDirectorConfig& OutConfig, FString& OutError);

    /** 从文件加载配置（搜索插件 Resources 目录） */
    static bool LoadFromFile(const FString& FileName, FShineComfyDirectorConfig& OutConfig, FString& OutError);

    /** 获取配置单例（懒加载，默认文件名 DirectorNode.json） */
    static const FShineComfyDirectorConfig& Get();
};
