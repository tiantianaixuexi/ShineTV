#include "video/CharacterAsset.h"

#include "core/Log.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Reflect.h"
#include "util/Strings.h"

#include <algorithm>
#include <optional>
#include <system_error>
#include <utility>

namespace shine::video {
namespace {

// 载入时统一收口的两件小事：名字兜底 + 参考图截断（与 Sanitize 的上限规则一致）
void NormalizeAsset(CharacterAsset& asset, const std::filesystem::path& path) {
    if (asset.name.empty()) {
        asset.name = util::PathToUtf8(path.stem()); // 资产名默认 = 文件名（去扩展名）
    }
    if (asset.referenceImages.size() > static_cast<std::size_t>(kMaxReferenceImagesPerShot)) {
        const std::size_t before = asset.referenceImages.size();
        asset.referenceImages.resize(static_cast<std::size_t>(kMaxReferenceImagesPerShot));
        log::Warn("角色「{}」参考图 {} 张 → {} 张（超出上限，截断末尾）", asset.name, before,
                  kMaxReferenceImagesPerShot);
    }
}

} // namespace

std::filesystem::path CharacterAssetDir(const std::filesystem::path& projectDir) {
    return projectDir / L"characters";
}

std::filesystem::path CharacterAssetPath(const std::filesystem::path& projectDir, std::string_view name) {
    // 资产名不带扩展名；传进来已带 `.json` 也不重复追加
    const std::string fileName = util::EndsWithNoCase(name, ".json") ? std::string(name) : std::string(name) + ".json";
    return CharacterAssetDir(projectDir) / util::PathFromUtf8(fileName);
}

std::expected<CharacterAsset, std::string> LoadCharacterAsset(const std::filesystem::path& path) {
    const std::optional<std::string> bytes = util::ReadFileBytes(path);
    if (!bytes) {
        return std::unexpected("角色资产打不开：" + util::PathToUtf8(path));
    }
    if (bytes->empty()) {
        return std::unexpected("角色资产是空文件：" + util::PathToUtf8(path));
    }

    CharacterAsset asset;
    const std::size_t hit = util::reflect::FromJsonString(*bytes, asset);
    if (hit == 0) {
        return std::unexpected("角色资产不是有效 JSON 或没有可识别字段：" + util::PathToUtf8(path));
    }
    asset.lockedSeed = -1; // 运行期字段，永远不信任文件里的值
    asset.assetPath = util::PathToUtf8(path);
    NormalizeAsset(asset, path);

    if (!asset.IsValid()) {
        return std::unexpected("角色资产既没有 name 也没有 displayName：" + util::PathToUtf8(path));
    }
    return asset;
}

bool SaveCharacterAsset(const CharacterAsset& asset, const std::filesystem::path& path) {
    const std::string json = util::reflect::ToJsonString(asset);
    if (json.empty()) {
        log::Error("角色资产序列化失败，未写入 {}", util::PathToUtf8(path));
        return false;
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (!util::WriteFileBytes(path, json)) {
        log::Error("角色资产写入失败：{}", util::PathToUtf8(path));
        return false;
    }
    log::Info("角色资产已保存：{}（{} 个字段，{} 张参考图）", util::PathToUtf8(path),
              util::reflect::FieldCount<CharacterAsset>(), asset.referenceImages.size());
    return true;
}

std::vector<CharacterAsset> LoadCharacterAssetDir(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    if (!dir.empty() && std::filesystem::is_directory(dir)) {
        std::error_code ec;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::filesystem::path& p = entry.path();
            if (util::EndsWithNoCase(util::PathToUtf8(p.filename()), ".json")) {
                files.push_back(p);
            }
        }
    }

    // 排序保证"扫描 → 列表 → 匹配"的顺序稳定（不依赖文件系统返回顺序）
    std::ranges::sort(files, [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return util::ToLower(util::PathToUtf8(a)) < util::ToLower(util::PathToUtf8(b));
    });

    std::vector<CharacterAsset> out;
    out.reserve(files.size());
    for (const std::filesystem::path& file : files) {
        std::expected<CharacterAsset, std::string> loaded = LoadCharacterAsset(file);
        if (loaded) {
            out.push_back(std::move(*loaded));
        } else {
            log::Warn("跳过坏掉的角色资产（{}）", loaded.error());
        }
    }
    log::Info("角色资产目录扫描完成：{} → {} 个角色（dir={}）", files.size(), out.size(), util::PathToUtf8(dir));
    return out;
}

} // namespace shine::video
