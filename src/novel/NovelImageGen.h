#pragma once
// P9 出图：ImageBackend 抽象 + 按 Settings 选后端
// 原则：生成默认 PROPOSED；HTTP 只在 worker；密钥不进日志/仓库
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace shine::novelcore {

struct ImageGenRequest {
    std::string prompt;
    std::string negative;
    int width = 1024;
    int height = 1024;
    int steps = 20; // 扩散/Comfy 用；OpenAI Images 可忽略
    std::string model;
    std::filesystem::path outputPath; // 调用方指定落盘（含扩展名 .png）
    std::string extraJson = "{}";
};

struct ImageGenError {
    int http_status = 0;
    std::string code;    // mock|no_key|network|http_*|json|io|unsupported
    std::string message; // 中文，可直接展示
};

struct ImageGenResult {
    std::filesystem::path path;
    std::string raw_meta = "{}";
};

// **仅 worker**；Generate 阻塞至完成
class ImageBackend {
public:
    virtual ~ImageBackend() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::expected<ImageGenResult, ImageGenError>
    Generate(const ImageGenRequest& req) = 0;
};

// Settings().imageBackend：mock | openai_images | comfy（comfy 首期返回 unsupported）
[[nodiscard]] std::unique_ptr<ImageBackend> MakeImageBackend();

// 工程内相对目录（Settings().imageOutputRelDir，默认 visual/gen）
[[nodiscard]] std::filesystem::path ImageRelDir();

// <projectDir>/<relDir>
[[nodiscard]] std::filesystem::path
ProjectImageDir(const std::filesystem::path& projectDir);

// 离线自检：mock 落盘 + no_key 错误路径（不发网络）
[[nodiscard]] bool RunImageGenSelfCheck();

} // namespace shine::novelcore
