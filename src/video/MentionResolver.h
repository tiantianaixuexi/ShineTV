#pragma once
// shine::video —— `@image` / `@char` / `{{Mixed N}}` 引用解析器（P5.2）
//
// 把分镜提示词里的**引用记号**变成两张东西：
//   ① 改写后的提示词（记号本身被移除、角色 `identityPrompt` 被拼进去）；
//   ② `orderedImages`：最终参考图顺序，就是 H3 提示词里 `<Picture N>` 的编号顺序。
//
// 记号语法（大小写不敏感；引用内容含空格时用**双引号**包起来）：
//   `@image:D:/a.png`          插入一张参考图
//   `@image:"D:/a b/c.png"`    路径含空格
//   `@char:主角`               插入该角色全部参考图 + 拼 `identityPrompt`
//   `@char:"D:/工程/characters/主角.json"`  也可按资产路径匹配
//
// 顺序规则（S4，**只此一份实现**）：
//   ① 提示词里**出现顺序**的 `@image:` / `@char:`（`@char:` 展开成它的全部参考图）
//   ② 分镜 `characterAssetPaths` 里**未被提及**的角色（按列表顺序）
//   ③ 分镜 `referenceImages` 里**还没出现过**的条目（按列表顺序）
//   去重 key = 规范化绝对路径 + 小写；超过 9 张 → 截断末尾并 `log::Warn`。
#include "video/CharacterAsset.h"
#include "video/VideoTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::video {

// 输入：**分镜的工作副本**（按值传 —— P5.5 上传时也是改工作副本，语义一致）+ 两个目录
struct ResolveRequest {
    Shot shot;                            // 待解析的分镜
    std::filesystem::path characterDir;   // 角色资产目录（`CharacterAssetDir(videoProjectDir)`）
    std::filesystem::path mediaLibraryDir;// **相对路径**的解析根（`Settings().mediaLibraryDir`）
    // `true`（默认）= 素材文件必须存在（提交前必须）；`false` = 只做路径**规范化**、不查磁盘（P5.4 的 dryRun 预览用）
    bool requireFiles = true;
};

struct ResolveResult {
    bool ok = false;                      // 有任何一条硬错误就是 false（此时不要往下走）
    std::string error;                    // 中文错误，**含出错的记号/路径/角色名**
    std::vector<std::string> warnings;    // 非致命提示（截断 / 种子冲突 / `<Picture N>` 越界）

    Shot shot;                            // 改写后的分镜：提示词已去记号、`forcedSeed` 已回填
    std::vector<std::string> orderedImages; // `<Picture N>` 的顺序 = 本数组顺序（已归一化/去重/≤9）
    std::int64_t forcedSeed = -1;         // 角色 `lockSeed` 回填的种子（-1 = 不锁）
    std::vector<std::string> usedCharacters; // 本次实际用到的角色（显示名优先，否则资产名）
};

// 解析。**纯函数**（只读磁盘，不改全局状态），可在 worker 线程调用。
[[nodiscard]] ResolveResult Resolve(const ResolveRequest& req);

// 解析器内部用的**去重 key**（规范化绝对路径 + 小写），对外暴露一份给 UI 做编号对照
// （UI 想把"分镜里的参考图条目"标成 `<Picture N>` 时，必须用与解析器**完全相同**的口径）。
// `mediaLibraryDir` 只对相对路径有意义；传绝对路径时可以留空。**只做字符串运算，不查磁盘**。
[[nodiscard]] std::string ImageDedupKey(std::string_view raw, const std::filesystem::path& mediaLibraryDir);

} // namespace shine::video
