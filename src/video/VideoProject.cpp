#include "video/VideoProject.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Reflect.h"

#include <yyjson.h>

#include <system_error>
#include <utility>

namespace shine::video {
namespace {

// `settings.json` 所在目录（通常是 `%APPDATA%\ShineTVStudio`）
[[nodiscard]] std::filesystem::path AppDataDir() {
    return util::PathFromUtf8(SettingsPath()).parent_path();
}

} // namespace

VideoProject VideoProject::MakeDefault() { return VideoProject{}; }

// ———————————————————————————————————————————————————————————————— Sanitize（P5.1 S3）
//
// 三件事，全部"只往上改、只截末尾"，并且**每次改动都打印纠正前后值**（验收 ② 就查这几行）：
//   ① 宽高：非正 → 默认值；再对齐到 32 的倍数；
//   ② 帧数：非正 → 默认值；再对齐到 `n % 17 == 5`；
//   ③ 参考图：超过 9 张 → 截断末尾。
bool VideoProject::Sanitize() {
    bool changed = false;

    for (std::size_t i = 0; i < shots.size(); ++i) {
        Shot& shot = shots[i];
        const std::size_t no = i + 1; // 日志用 1 起序号，与 UI 的"序号"列一致

        // ① 宽度
        if (shot.width <= 0) {
            log::Warn("分镜 #{} 宽度 {} 非法 → {}（默认值）", no, shot.width, kDefaultWidth);
            shot.width = kDefaultWidth;
            changed = true;
        }
        if (const int aligned = AlignToMultiple(shot.width); aligned != shot.width) {
            log::Warn("分镜 #{} 宽度 {} → {}（对齐到 {} 的倍数）", no, shot.width, aligned, kSizeMultiple);
            shot.width = aligned;
            changed = true;
        }

        // ① 高度
        if (shot.height <= 0) {
            log::Warn("分镜 #{} 高度 {} 非法 → {}（默认值）", no, shot.height, kDefaultHeight);
            shot.height = kDefaultHeight;
            changed = true;
        }
        if (const int aligned = AlignToMultiple(shot.height); aligned != shot.height) {
            log::Warn("分镜 #{} 高度 {} → {}（对齐到 {} 的倍数）", no, shot.height, aligned, kSizeMultiple);
            shot.height = aligned;
            changed = true;
        }

        // ② 帧数
        if (shot.length <= 0) {
            log::Warn("分镜 #{} 帧数 {} 非法 → {}（默认值）", no, shot.length, kDefaultLength);
            shot.length = kDefaultLength;
            changed = true;
        }
        if (const int aligned = AlignFrameCount(shot.length); aligned != shot.length) {
            log::Warn("分镜 #{} 帧数 {} → {}（对齐到 n % {} == {}）", no, shot.length, aligned, kFrameGridStride,
                      kFrameGridOffset);
            shot.length = aligned;
            changed = true;
        }

        // ③ 参考图上限
        if (shot.referenceImages.size() > static_cast<std::size_t>(kMaxReferenceImagesPerShot)) {
            const std::size_t before = shot.referenceImages.size();
            shot.referenceImages.resize(static_cast<std::size_t>(kMaxReferenceImagesPerShot));
            log::Warn("分镜 #{} 参考图 {} 张 → {} 张（超出上限，截断末尾）", no, before, kMaxReferenceImagesPerShot);
            changed = true;
        }
    }

    return changed;
}

// ———————————————————————————————————————————————————————————————— 存盘
std::string VideoProject::ToJson(bool pretty) const { return util::reflect::ToJsonString(*this, pretty); }

bool VideoProject::LoadFromJson(std::string_view text) {
    // 先分清两种失败：**文件坏了**（解析不了 / 根不是对象）和**只是缺字段**。
    // 前者保持原工程不变并报错；后者交给反射宽容读，缺的部分取默认值。
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (!doc) {
        log::Error("工程 JSON 解析失败（文件损坏或不是 JSON）—— 已保持当前工程不变");
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        log::Error("工程 JSON 根不是对象 —— 已保持当前工程不变");
        yyjson_doc_free(doc);
        return false;
    }

    VideoProject loaded = MakeDefault();
    const std::size_t hit = util::reflect::ReadObject(root, loaded);
    yyjson_doc_free(doc);

    log::Info("工程已载入：命中 {} 个字段、{} 个分镜", hit, loaded.shots.size());
    loaded.Sanitize(); // 手改过的工程文件也要回到合法规格
    *this = std::move(loaded);
    return true;
}

bool VideoProject::SaveToFile(const std::filesystem::path& path) const {
    const std::string json = ToJson();
    if (json.empty()) {
        log::Error("工程序列化失败，未写入 {}", util::PathToUtf8(path));
        return false;
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec); // 建不出来让下面写文件时报错
    }
    if (!util::WriteFileBytes(path, json)) {
        log::Error("工程写入失败：{}", util::PathToUtf8(path));
        return false;
    }
    log::Info("工程已保存：{}（{} 字节，{} 个分镜）", util::PathToUtf8(path), json.size(), shots.size());
    return true;
}

bool VideoProject::LoadFromFile(const std::filesystem::path& path) {
    const std::optional<std::string> bytes = util::ReadFileBytes(path);
    if (!bytes) {
        log::Error("工程文件打不开：{}", util::PathToUtf8(path));
        return false;
    }
    if (bytes->empty()) {
        log::Error("工程文件为空：{}", util::PathToUtf8(path));
        return false;
    }
    log::Info("读取工程文件：{}（{} 字节）", util::PathToUtf8(path), bytes->size());
    return LoadFromJson(*bytes);
}

// ———————————————————————————————————————————————————————————————— 便捷操作
std::size_t VideoProject::SubmittableCount() const noexcept {
    std::size_t n = 0;
    for (const Shot& shot : shots) {
        if (shot.IsSubmittable()) {
            ++n;
        }
    }
    return n;
}

Shot& VideoProject::AddShot() {
    shots.push_back(MakeDefaultShot());
    return shots.back();
}

void VideoProject::RemoveShot(std::size_t index) noexcept {
    if (index < shots.size()) {
        shots.erase(shots.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

// ———————————————————————————————————————————————————————————————— 目录
std::filesystem::path VideoProjectDir() {
    const std::string& dir = Settings().videoProjectDir;
    return dir.empty() ? AppDataDir() / L"video" : util::PathFromUtf8(dir);
}

std::filesystem::path VideoOutputDir() {
    const std::string& dir = Settings().videoOutputDir;
    return dir.empty() ? VideoProjectDir() / L"output" : util::PathFromUtf8(dir);
}

std::filesystem::path MediaLibraryDir() {
    const std::string& dir = Settings().mediaLibraryDir;
    return dir.empty() ? AppDataDir() / L"media" : util::PathFromUtf8(dir);
}

} // namespace shine::video
