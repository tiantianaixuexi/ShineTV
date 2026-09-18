#pragma once
// shine::video —— 角色资产（P5.2 S1）
//
// 一个角色 = 一组参考图（≤ `kMaxReferenceImagesPerShot`）+ 固定身份提示词（`identityPrompt`，
// 用到该角色的分镜会把它拼进提示词）+ 可选**锁定种子**（`lockSeed`：让同一角色在不同分镜里"长得一样"）。
//
// 存放：**工程目录内** `<videoProjectDir>/characters/<资产名>.json`，走静态反射存盘
// （字段名即 JSON 键，见 `Doc/RULES-LANG.md` §13.6）。
// 匹配：`@char:` 支持三种写法 —— ①资产文件路径 ②`displayName` ③`name`（资产名）。
#include "video/VideoTypes.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::video {

struct CharacterAsset {
    std::string name;          // 资产名（默认取文件名去扩展名）；`@char:` 的第三种匹配方式
    std::string displayName;   // 显示名；`@char:` 的第二种匹配方式
    std::vector<std::string> referenceImages; // 参考图（超 9 张在载入时截断并告警）
    std::string identityPrompt;// 身份描述，解析时拼进提示词
    std::int64_t lockSeed = -1;   // 用户设定：≥0 表示锁定该角色的出片种子
    std::int64_t lockedSeed = -1; // **运行期回填**：本次解析实际采用的锁定种子

    // 资产 JSON 的绝对路径（**运行期由 `LoadCharacterAsset` 回填**；文件里就算写了也不算数）
    std::string assetPath;

    [[nodiscard]] bool IsValid() const noexcept { return !name.empty() || !displayName.empty(); }
};

// 角色资产目录：`<projectDir>/characters`
[[nodiscard]] std::filesystem::path CharacterAssetDir(const std::filesystem::path& projectDir);

// 某个资产的默认落盘路径：`<projectDir>/characters/<name>.json`
[[nodiscard]] std::filesystem::path CharacterAssetPath(const std::filesystem::path& projectDir, std::string_view name);

// 读一个角色资产；失败返回**中文原因**（打不开 / 空文件 / 不是 JSON / 无可识别字段）
[[nodiscard]] std::expected<CharacterAsset, std::string> LoadCharacterAsset(const std::filesystem::path& path);

// 写一个角色资产（父目录自动创建）；成功返回 true
[[nodiscard]] bool SaveCharacterAsset(const CharacterAsset& asset, const std::filesystem::path& path);

// 扫一个目录下全部 `*.json` 角色资产（按路径不区分大小写排序，保证顺序稳定）。
// 目录不存在/为空 → 空表；单个文件坏掉 → 跳过并 `log::Warn`（不中断整表）
[[nodiscard]] std::vector<CharacterAsset> LoadCharacterAssetDir(const std::filesystem::path& dir);

} // namespace shine::video
