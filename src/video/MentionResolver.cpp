#include "video/MentionResolver.h"

#include "core/Log.h"
#include "util/Encoding.h"
#include "util/Strings.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::video {
namespace {

constexpr std::string_view kImagePrefix = "@image:";
constexpr std::string_view kCharPrefix = "@char:";
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

[[nodiscard]] bool StartsWithNoCase(std::string_view s, std::string_view prefix) noexcept {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsSpaceChar(char c) noexcept { return std::isspace(static_cast<unsigned char>(c)) != 0; }

using util::FromInt;
[[nodiscard]] std::string IntToString(int value) { return FromInt(value); }

// —————————————————————————————————————————————————————————— 记号扫描

enum class MentionKind { Image, Character };

struct Mention {
    MentionKind kind = MentionKind::Image;
    std::string ref;           // 去掉引号后的引用原文
    std::size_t end = 0;       // 记号在提示词里的结束位置（不含）
    bool unterminated = false; // 双引号没闭合
};

// 在 `text[pos]`（必须已确认是 `@`）处解析一个记号；不是记号 → nullopt
[[nodiscard]] std::optional<Mention> ParseMentionAt(std::string_view text, std::size_t pos) {
    Mention m;
    std::size_t refBegin = 0;
    if (StartsWithNoCase(text.substr(pos), kImagePrefix)) {
        m.kind = MentionKind::Image;
        refBegin = pos + kImagePrefix.size();
    } else if (StartsWithNoCase(text.substr(pos), kCharPrefix)) {
        m.kind = MentionKind::Character;
        refBegin = pos + kCharPrefix.size();
    } else {
        return std::nullopt;
    }

    if (refBegin < text.size() && text[refBegin] == '"') {
        const std::size_t close = text.find('"', refBegin + 1);
        if (close == std::string_view::npos) {
            m.unterminated = true;
            m.ref.assign(text.substr(refBegin + 1));
            m.end = text.size();
            return m;
        }
        m.ref.assign(text.substr(refBegin + 1, close - refBegin - 1));
        m.end = close + 1;
        return m;
    }

    // 不带引号：到空白或下一个 '@' 为止（`@image:D:/a b/c.png` 这种含空格的写法必须加引号）
    std::size_t e = refBegin;
    while (e < text.size() && !IsSpaceChar(text[e]) && text[e] != '@') {
        ++e;
    }
    m.ref.assign(text.substr(refBegin, e - refBegin));
    m.end = e;
    return m;
}

// —————————————————————————————————————————————————————————— 路径与角色

[[nodiscard]] std::filesystem::path NormalizeAbsolute(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec) {
        abs = p;
    }
    return abs.lexically_normal();
}

// 去重 key = 规范化 + 全路径 + 小写（S4 规定）
[[nodiscard]] std::string DedupKey(const std::filesystem::path& p) {
    return util::ToLower(util::PathToUtf8(NormalizeAbsolute(p)));
}

struct ResolvedPath {
    std::filesystem::path absolute;
    std::string error; // 非空 = 失败（中文，含原始引用）
};

// 相对路径按 `mediaDir` 展开；空 mediaDir 直接报错（不猜 CWD）
[[nodiscard]] ResolvedPath ResolveInputPath(std::string_view raw, const std::filesystem::path& mediaDir,
                                           bool requireFiles) {
    ResolvedPath out;
    if (raw.empty()) {
        out.error = "引用里的路径是空的";
        return out;
    }
    std::filesystem::path p = util::PathFromUtf8(raw);
    if (!p.is_absolute()) {
        if (mediaDir.empty()) {
            out.error = "相对路径「" + std::string(raw) + "」无法解析：请在设置里填写素材库目录";
            return out;
        }
        p = mediaDir / p;
    }
    std::filesystem::path abs = NormalizeAbsolute(p);

    if (requireFiles) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(abs, ec);
        if (ec || !exists) {
            out.error = "文件不存在：" + std::string(raw) + "（解析为 " + util::PathToUtf8(abs) + "）";
            return out;
        }
    }
    out.absolute = std::move(abs);
    return out;
}

[[nodiscard]] std::string CharacterLabel(const CharacterAsset& asset) {
    return asset.displayName.empty() ? asset.name : asset.displayName;
}

// @char: 的引用可能是一个"目录外的资产文件路径" → 给出几个候选绝对路径
[[nodiscard]] std::vector<std::filesystem::path> CharacterPathCandidates(std::string_view ref,
                                                                        const std::filesystem::path& characterDir,
                                                                        const std::filesystem::path& mediaDir) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path p = util::PathFromUtf8(ref);
    if (p.is_absolute()) {
        out.push_back(NormalizeAbsolute(p));
        return out;
    }
    if (!characterDir.empty()) {
        out.push_back(NormalizeAbsolute(characterDir / p));
    }
    if (!mediaDir.empty()) {
        out.push_back(NormalizeAbsolute(mediaDir / p));
    }
    return out;
}

[[nodiscard]] bool LooksLikePath(std::string_view ref) {
    return ref.find('/') != std::string_view::npos || ref.find('\\') != std::string_view::npos ||
           util::EndsWithNoCase(ref, ".json") || util::PathFromUtf8(ref).is_absolute();
}

// 匹配顺序（S3）：①资产路径 ②显示名 ③资产名（后两者不区分大小写）
[[nodiscard]] std::size_t FindCharacter(const std::vector<CharacterAsset>& assets, std::string_view ref,
                                        const std::filesystem::path& characterDir,
                                        const std::filesystem::path& mediaDir) {
    if (LooksLikePath(ref)) {
        std::vector<std::string> keys;
        const std::filesystem::path raw = util::PathFromUtf8(ref);
        if (raw.is_absolute()) {
            keys.push_back(DedupKey(raw));
        }
        for (const std::filesystem::path& cand : CharacterPathCandidates(ref, characterDir, mediaDir)) {
            keys.push_back(DedupKey(cand));
        }
        for (std::size_t i = 0; i < assets.size(); ++i) {
            if (std::ranges::find(keys, DedupKey(util::PathFromUtf8(assets[i].assetPath))) != keys.end()) {
                return i;
            }
        }
    }
    const std::string lowerRef = util::ToLower(ref);
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (!assets[i].displayName.empty() && util::ToLower(assets[i].displayName) == lowerRef) {
            return i;
        }
    }
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (!assets[i].name.empty() && util::ToLower(assets[i].name) == lowerRef) {
            return i;
        }
    }
    return kNoIndex;
}

// —————————————————————————————————————————————————————————— 提示词改写

void AppendClause(std::string& base, std::string_view clause) {
    const std::string_view trimmed = util::Trim(clause);
    if (trimmed.empty()) {
        return;
    }
    if (!base.empty()) {
        base += ", ";
    }
    base.append(trimmed);
}

// `{{Mixed N}}` → `<Picture N>`；其他 `{{...}}` 原样保留；N 越界只告警不改写语义
[[nodiscard]] std::string RewriteMixed(std::string_view text, int pictureCount, std::vector<std::string>& warnings) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find("{{", i);
        if (open == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        const std::size_t close = text.find("}}", open + 2);
        if (close == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        out.append(text.substr(i, open - i));

        const std::string_view inner = util::Trim(text.substr(open + 2, close - open - 2));
        int index = 0;
        bool isMixed = false;
        if (StartsWithNoCase(inner, "mixed")) {
            if (const std::optional<int> parsed = util::ToInt(util::Trim(inner.substr(5)))) {
                index = *parsed;
                isMixed = true;
            }
        }
        if (isMixed) {
            out += "<Picture " + IntToString(index) + ">";
            if (index < 1 || index > pictureCount) {
                warnings.push_back("提示词里的 {{Mixed " + IntToString(index) + "}} 越界（本次只有 " +
                                   IntToString(pictureCount) + " 张参考图）");
            }
        } else {
            out.append(text.substr(open, close + 2 - open));
        }
        i = close + 2;
    }
    return out;
}

} // namespace

std::string ImageDedupKey(std::string_view raw, const std::filesystem::path& mediaLibraryDir) {
    std::filesystem::path p = util::PathFromUtf8(raw);
    if (!p.is_absolute() && !mediaLibraryDir.empty()) {
        p = mediaLibraryDir / p;
    }
    return DedupKey(p); // 与 `Resolve` 里 `pushImage` 的口径一致（都是 NormalizeAbsolute + 小写）
}

// ———————————————————————————————————————————————————————————————— Resolve
ResolveResult Resolve(const ResolveRequest& req) {
    ResolveResult result;
    result.shot = req.shot;

    std::vector<CharacterAsset> assets;
    if (!req.characterDir.empty()) {
        assets = LoadCharacterAssetDir(req.characterDir);
    }
    std::vector<bool> used(assets.size(), false);

    std::vector<std::string> ordered;      // orderedImages（顺序 = <Picture N>）
    std::vector<std::string> dedupKeys;
    std::vector<std::string> identityClauses; // 只由 `@char:` 产生（任务书 S3 的语义）

    auto pushImage = [&](const std::filesystem::path& abs) {
        const std::string key = DedupKey(abs);
        if (std::ranges::find(dedupKeys, key) != dedupKeys.end()) {
            return; // 去重：同一张图（哪怕写法不同）只出现一次
        }
        dedupKeys.push_back(key);
        ordered.push_back(util::PathToUtf8(abs));
    };

    // 用到一个角色：回填锁定种子（S4）
    auto applySeedLock = [&](std::size_t index) {
        const CharacterAsset& asset = assets[index];
        if (asset.lockSeed < 0) {
            return;
        }
        if (result.forcedSeed < 0) {
            result.forcedSeed = asset.lockSeed;
            assets[index].lockedSeed = asset.lockSeed;
        } else if (result.forcedSeed != asset.lockSeed) {
            result.warnings.push_back("角色「" + CharacterLabel(asset) + "」的锁定种子 " +
                                      IntToString(static_cast<int>(asset.lockSeed)) + " 与先出现的 " +
                                      IntToString(static_cast<int>(result.forcedSeed)) + " 不一致，取先出现的");
        }
    };

    // 找角色：先在已扫到的资产里按 路径/显示名/资产名 匹配；还不行且看着像路径 → 直接载入这个文件
    auto findOrLoadCharacter = [&](std::string_view ref) -> std::size_t {
        const std::size_t found = FindCharacter(assets, ref, req.characterDir, req.mediaLibraryDir);
        if (found != kNoIndex || !LooksLikePath(ref)) {
            return found;
        }
        for (const std::filesystem::path& cand : CharacterPathCandidates(ref, req.characterDir, req.mediaLibraryDir)) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(cand, ec)) {
                continue;
            }
            std::expected<CharacterAsset, std::string> loaded = LoadCharacterAsset(cand);
            if (!loaded) {
                continue;
            }
            assets.push_back(std::move(*loaded));
            used.push_back(false);
            return assets.size() - 1;
        }
        return kNoIndex;
    };

    // 收一个角色的全部参考图
    auto collectCharacter = [&](std::size_t index, const char* where) -> bool {
        const CharacterAsset& asset = assets[index];
        for (const std::string& raw : asset.referenceImages) {
            const ResolvedPath resolved = ResolveInputPath(raw, req.mediaLibraryDir, req.requireFiles);
            if (!resolved.error.empty()) {
                result.error = std::string("角色「") + CharacterLabel(asset) + "」的参考图" + resolved.error + "（来自" +
                               where + "）";
                return false;
            }
            pushImage(resolved.absolute);
        }
        applySeedLock(index);
        if (!used[index]) {
            used[index] = true;
            result.usedCharacters.push_back(CharacterLabel(asset));
        }
        return true;
    };

    // ———— ① 扫提示词：摘记号（记号本身不进提示词）、按出现顺序取图 ————
    const std::string_view text = req.shot.prompt;
    std::string stripped;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t at = text.find('@', cursor);
        if (at == std::string_view::npos) {
            stripped.append(text.substr(cursor));
            break;
        }
        stripped.append(text.substr(cursor, at - cursor));

        const std::optional<Mention> mention = ParseMentionAt(text, at);
        if (!mention) {
            stripped.push_back('@');
            cursor = at + 1;
            continue;
        }
        const char* prefix = mention->kind == MentionKind::Image ? "@image:" : "@char:";
        if (mention->unterminated) {
            result.error = std::string("提示词里的 ") + prefix + " 双引号没有闭合：" + mention->ref;
            return result;
        }
        if (mention->ref.empty()) {
            result.error = std::string("提示词里的 ") + prefix + " 后面没有内容";
            return result;
        }

        if (mention->kind == MentionKind::Image) {
            const ResolvedPath resolved = ResolveInputPath(mention->ref, req.mediaLibraryDir, req.requireFiles);
            if (!resolved.error.empty()) {
                result.error = "参考图" + resolved.error;
                return result;
            }
            pushImage(resolved.absolute);
        } else {
            const std::size_t index = findOrLoadCharacter(mention->ref);
            if (index == kNoIndex) {
                result.error = "找不到角色「" + mention->ref + "」（在 " +
                               (req.characterDir.empty() ? std::string("（未设置角色目录）")
                                                         : util::PathToUtf8(req.characterDir)) +
                               " 下按 资产路径/显示名/资产名 都没匹配到）";
                return result;
            }
            if (!collectCharacter(index, "@char:")) {
                return result;
            }
            if (const std::string_view identity = util::Trim(assets[index].identityPrompt); !identity.empty()) {
                identityClauses.emplace_back(identity);
            }
        }
        cursor = mention->end;
    }

    // ———— ② 分镜 characterAssetPaths 里"未被提及"的角色 ————
    for (const std::string& ref : req.shot.characterAssetPaths) {
        const std::size_t index = findOrLoadCharacter(ref);
        if (index == kNoIndex) {
            result.error = "分镜里登记的角色资产找不到：「" + ref + "」（角色目录 " +
                           (req.characterDir.empty() ? std::string("未设置") : util::PathToUtf8(req.characterDir)) + "）";
            return result;
        }
        if (used[index]) {
            continue;
        }
        if (!collectCharacter(index, "characterAssetPaths")) {
            return result;
        }
    }

    // ———— ③ 分镜 referenceImages 里"还没出现过"的条目 ————
    for (const std::string& raw : req.shot.referenceImages) {
        const ResolvedPath resolved = ResolveInputPath(raw, req.mediaLibraryDir, req.requireFiles);
        if (!resolved.error.empty()) {
            result.error = "参考图" + resolved.error;
            return result;
        }
        pushImage(resolved.absolute);
    }

    // ———— 截断（> 9 张，末尾丢弃）————
    if (ordered.size() > static_cast<std::size_t>(kMaxReferenceImagesPerShot)) {
        const std::size_t before = ordered.size();
        ordered.resize(static_cast<std::size_t>(kMaxReferenceImagesPerShot));
        log::Warn("分镜「{}」参考图 {} 张 → {} 张（超出 H3 上限，截断末尾）", req.shot.title, before,
                  kMaxReferenceImagesPerShot);
        result.warnings.push_back("参考图 " + IntToString(static_cast<int>(before)) + " 张 → " +
                                  IntToString(kMaxReferenceImagesPerShot) + " 张（超出 H3 上限，截断末尾）");
    }

    // ———— 提示词收尾：`{{Mixed N}}` → `<Picture N>`，再拼角色身份 ————
    std::string prompt = RewriteMixed(stripped, static_cast<int>(ordered.size()), result.warnings);
    for (const std::string& clause : identityClauses) {
        AppendClause(prompt, clause);
    }
    result.shot.prompt.assign(util::Trim(prompt));

    // ———— 首帧图（可空 = 链式接上一段）————
    if (!req.shot.firstFramePath.empty()) {
        const ResolvedPath resolved = ResolveInputPath(req.shot.firstFramePath, req.mediaLibraryDir, req.requireFiles);
        if (!resolved.error.empty()) {
            result.error = "首帧图" + resolved.error;
            return result;
        }
        result.shot.firstFramePath = util::PathToUtf8(resolved.absolute);
    }

    if (result.forcedSeed >= 0) {
        result.shot.forcedSeed = result.forcedSeed;
    }

    result.ok = true;
    result.error.clear();
    result.orderedImages = std::move(ordered);
    log::Info("分镜「{}」解析完成：{} 张参考图、{} 个记号、{} 个告警", req.shot.title, result.orderedImages.size(),
              identityClauses.size(), result.warnings.size());
    return result;
}

} // namespace shine::video
