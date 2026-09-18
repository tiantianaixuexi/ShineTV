#include "gallery/cache/DiskThumbCache.h"

#include "core/Log.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Strings.h"

#include <fmt/format.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace shine::gallery::disk_cache {
namespace {

std::atomic<std::size_t> g_hits{0};
std::atomic<std::size_t> g_lookups{0};

[[nodiscard]] std::uint64_t Fnv1a64(std::string_view s, std::uint64_t h = 1469598103934665603ull) noexcept {
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

[[nodiscard]] std::filesystem::path AppData() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L".";
    return std::filesystem::path{base} / L"ShineTVStudio";
}

[[nodiscard]] std::uint64_t CacheKey(const std::filesystem::path& file, std::uint32_t bucket) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    const auto mtime = std::filesystem::last_write_time(file, ec);
    const auto mcount = mtime.time_since_epoch().count();
    std::string norm = util::ToLower(util::PathToUtf8(file.lexically_normal()));
    std::uint64_t h = kThumbAlgorithmVersion;
    h = Fnv1a64(norm, h);
    h = Fnv1a64(fmt::format("|{}|{}|{}", static_cast<unsigned long long>(size),
                            static_cast<long long>(mcount), bucket),
                h);
    return h;
}

} // namespace

std::uint32_t SizeBucket(std::uint32_t targetSide) noexcept {
    if (targetSide <= 192u) {
        return 128u;
    }
    if (targetSide <= 384u) {
        return 256u;
    }
    return 512u;
}

std::filesystem::path RootDir() { return AppData() / L"cache" / L"thumbs"; }

std::filesystem::path DirForBucket(std::uint32_t bucket) { return RootDir() / std::to_wstring(bucket); }

std::optional<Image> Load(const std::filesystem::path& file, std::uint32_t targetSide) {
    ++g_lookups;
    const std::uint32_t bucket = SizeBucket(targetSide);
    const std::filesystem::path path = DirForBucket(bucket) / (fmt::format("{:016x}.thumb", CacheKey(file, bucket)));
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    const auto bytes = util::ReadFileBytes(path);
    if (!bytes || bytes->size() < 8) {
        return std::nullopt;
    }
    const auto* p = reinterpret_cast<const std::uint8_t*>(bytes->data());
    const std::uint32_t w = static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    const std::uint32_t h = static_cast<std::uint32_t>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    const std::size_t need = 8 + static_cast<std::size_t>(w) * h * 4u;
    if (w == 0 || h == 0 || bytes->size() < need) {
        return std::nullopt;
    }
    Image img = Image::Allocate(w, h);
    if (!img.valid()) {
        return std::nullopt;
    }
    std::memcpy(img.data, bytes->data() + 8, img.bytes);
    ++g_hits;
    return img;
}

bool Store(const std::filesystem::path& file, std::uint32_t targetSide, const Image& img) {
    if (!img.valid()) {
        return false;
    }
    const std::uint32_t bucket = SizeBucket(targetSide);
    const std::filesystem::path dir = DirForBucket(bucket);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / (fmt::format("{:016x}.thumb", CacheKey(file, bucket)));

    std::string blob;
    blob.resize(8 + img.bytes);
    const std::uint32_t w = img.width;
    const std::uint32_t h = img.height;
    blob[0] = static_cast<char>(w & 0xFF);
    blob[1] = static_cast<char>((w >> 8) & 0xFF);
    blob[2] = static_cast<char>((w >> 16) & 0xFF);
    blob[3] = static_cast<char>((w >> 24) & 0xFF);
    blob[4] = static_cast<char>(h & 0xFF);
    blob[5] = static_cast<char>((h >> 8) & 0xFF);
    blob[6] = static_cast<char>((h >> 16) & 0xFF);
    blob[7] = static_cast<char>((h >> 24) & 0xFF);
    std::memcpy(blob.data() + 8, img.data, img.bytes);
    if (!util::WriteFileBytes(path, blob)) {
        log::Warn("磁盘缩略图写失败：{}", util::PathToUtf8(path));
        return false;
    }
    return true;
}

std::size_t Hits() noexcept { return g_hits.load(std::memory_order_relaxed); }
std::size_t Lookups() noexcept { return g_lookups.load(std::memory_order_relaxed); }
double HitRate() noexcept {
    const auto l = Lookups();
    return l == 0 ? 0.0 : static_cast<double>(Hits()) / static_cast<double>(l);
}
void ResetStats() noexcept {
    g_hits = 0;
    g_lookups = 0;
}

std::string Summary() {
    return fmt::format("命中 {}/{}（{:.0f}%）", Hits(), Lookups(), HitRate() * 100.0);
}

} // namespace shine::gallery::disk_cache
