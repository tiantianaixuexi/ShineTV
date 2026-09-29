#include "ui/imgui/verify/Capture.h"

#include "core/Log.h"
#include "paint/PngCodec.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace shine::imguiverify {

bool SavePng(const std::vector<std::uint8_t>& rgbaPixels, std::uint32_t width, std::uint32_t height,
             const std::filesystem::path& path) {
    if (width == 0 || height == 0 || rgbaPixels.size() < static_cast<std::size_t>(width) * height * 4u) {
        shine::log::Error("capture: bad frame {}x{} ({} bytes)", width, height, rgbaPixels.size());
        return false;
    }

    // 输入已经是 RGBA8（左上角原点），直接编码 —— **不要再交换通道**。
    // 这层交换在 D3D11 时代写死过一次，而交换链实际是 R8G8B8A8，
    // 于是每张证据图的 R 和 B 都对调，界面配色看着「偏暖发黄」，
    // 差点被当成主题映射错了去改主题。抓图后端换 GL 后这层歧义根本不存在了。
    const std::span<const std::byte> rgba{reinterpret_cast<const std::byte*>(rgbaPixels.data()),
                                          rgbaPixels.size()};

    auto encoded = shine::paint::EncodePng(width, height, rgba);
    if (!encoded) {
        shine::log::Error("capture: png encode failed for {}", path.string());
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        shine::log::Error("capture: cannot write {}", path.string());
        return false;
    }
    const auto& bytes = *encoded;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

void WriteManifest(const std::filesystem::path& manifest, std::string_view line) {
    std::error_code ec;
    if (manifest.has_parent_path()) {
        std::filesystem::create_directories(manifest.parent_path(), ec);
    }
    std::ofstream out(manifest, std::ios::app);
    if (!out) {
        return;
    }
    out << line << '\n';
}

bool GrabAndSave(const std::vector<std::uint8_t>& bgraPixels, std::uint32_t width,
                 std::uint32_t height, const std::filesystem::path& dir, std::string_view name) {
    const std::filesystem::path file = dir / (std::string(name) + ".png");
    if (!SavePng(bgraPixels, width, height, file)) {
        WriteManifest(dir / "manifest.txt", std::string(name) + " 0 FAILED");
        return false;
    }
    const auto size = std::filesystem::file_size(file);
    // manifest 行格式是既有契约，scripts/*.ps1 依赖
    WriteManifest(dir / "manifest.txt", std::string(name) + " " + std::to_string(size) + " saved");
    return true;
}

} // namespace shine::imguiverify
