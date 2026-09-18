#pragma once

#include "CoreMinimal.h"

class UShineCharacterAsset;

/**
 * 提示词引用语法的解析器（`@image:` / `@char:` / `{{Mixed N}}`）。
 *
 * ## 为什么单独一层
 *
 * `FShineMiniMaxH3WorkflowBuilder` 刻意不做 IO：它只认"已经解析好的参考图标识 + 提示词"，
 * 这样才能拿 P0 的探针图做逐字段对拍。把"人话写的引用语法"翻译成"有序的参考图列表 +
 * 带 `<Picture N>` 标签的提示词"这件事，就落在这里。
 *
 * ## 语法的确切语义（这里是唯一权威说明）
 *
 * - `@image:<路径>`：插入一张参考图。路径可以是素材库相对路径，也可以是绝对路径；
 *   含空格时用双引号包起来（`@image:"D:/a b/c.png"`）。这个记号本身**不出现在**最终提示词里。
 * - `@char:<角色>`：插入一个角色资产携带的全部参考图，并把它的身份描述（IdentityPrompt）
 *   拼进提示词。角色可以按资产路径（`/Game/...`）或显示名（DisplayName）/资产名匹配。
 * - `{{Mixed N}}`：指代第 N 张参考图，解析成 `<Picture N>`。
 *
 * ⚠️ `<Picture i>` 是 tokenizer 按**连接顺序**自动生成的标签（见 H3-SPEC.md 坑 5），
 * 所以 `{{Mixed N}}` 是"引用第 N 张"，不是"声明第 N 张"。顺序改不动，只能照着写。
 *
 * ## 最终参考图的顺序（Picture 1..N）
 *
 * 1. 提示词里按**出现顺序**的 `@image:` / `@char:`（角色携带的图按资产里的顺序展开）；
 * 2. 分镜 `CharacterAssetPaths` 里**没被提及**的角色（同样展开其参考图，
 *    IdentityPrompt 追加到提示词末尾）；
 * 3. 分镜 `ReferenceImages` 里还没出现过的条目，按列表顺序。
 *
 * 同一张图（解析成绝对路径后相同）只算一次，重复的会被折叠——`{{Mixed N}}` 的编号
 * 是折叠之后的位置。之所以让"提示词里写的"排在"字段里存的"前面：面板是**拖素材进提示词框**
 * 生成记号的，写在这些记号后面的 `{{Mixed 1}}` 才符合"就是我刚刚插进去的那张"的直觉。
 */
struct FShineMentionResolveRequest
{
    /** 原始提示词（含引用记号）。 */
    FString Prompt;

    /** 分镜字段里的参考图（素材库相对路径或绝对路径）。 */
    TArray<FString> ReferenceImages;

    /** 分镜字段里显式引用的角色资产路径。 */
    TArray<FString> CharacterAssetPaths;

    /** 项目级角色表：供 `@char:<显示名>` 按名字匹配。 */
    TArray<FString> ProjectCharacterAssetPaths;
};

struct FShineMentionResolveResult
{
    bool bSuccess = false;
    FString ErrorMessage;

    /** 已经把引用记号替换成 `<Picture N>`（并拼上角色身份描述）的提示词。 */
    FString ResolvedPrompt;

    /** Picture 1..N 的顺序，元素是**磁盘上的绝对路径**（还没上传到 ComfyUI）。 */
    TArray<FString> OrderedImagePaths;

    /** 本次真正引到的角色显示名（去重，按引用顺序）。 */
    TArray<FString> ResolvedCharacters;

    /**
     * 若引用的角色里有 `bLockSeed`，这里给出统一种子。
     * 多个角色的种子冲突时取先出现的那个，并在 Warnings 里说清楚。
     */
    TOptional<int32> ForcedSeed;

    /** 非致命问题：找不到的角色、越界的 `{{Mixed N}}`、空的 `@image:` 等。 */
    TArray<FString> Warnings;
};

class SHINEEDITOR_API FShineMentionResolver
{
public:
    /** 解析一个分镜的提示词与引用。任何"找不到角色"之类都只算警告，不算失败。 */
    static FShineMentionResolveResult Resolve(const FShineMentionResolveRequest& Request);

    /**
     * 把素材标识解析成磁盘绝对路径。
     * 相对路径按素材库根目录（见 UShineComfyPathSettings::MediaLibraryDirectory）解析；
     * 绝对路径原样返回。空串返回空串。**不检查文件是否存在**——那是执行器的事。
     */
    static FString ResolveLocalPath(const FString& Identifier);

    /** 按角色标识找角色资产（先当对象路径加载，再按显示名/资产名在给定表里匹配）。 */
    static const UShineCharacterAsset* FindCharacterAsset(const FString& Identifier, const TArray<FString>& SearchPaths);

    /** H3 的参考图硬上限（`ref_images.ref_image_*` 的 max=9）。 */
    static constexpr int32 MaxReferenceImages = 9;
};
