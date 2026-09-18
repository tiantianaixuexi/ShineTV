#pragma once
// shine::util —— **文件读写（宽字符路径安全）**
//
// 为什么要有这个头：Windows 上用**窄字符串**打开文件（`std::ifstream(std::string)`、`fopen("…")`）
// 走的是 ANSI 代码页，**中文（非 ASCII）路径必然打不开**；
// 而 `std::ifstream(std::filesystem::path)` 在 MinGW/libstdc++ 下会走 `_wfopen`，宽字符安全。
// 所以：**统一的文件入口都收在这里**，业务侧只传 `std::filesystem::path`。
//
// 配套：路径与字符串互转在 `util/Encoding.h`（`PathFromUtf8 / PathToUtf8`）。
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace shine::util {

// 只读打开（默认二进制）；用 `if (!in)` 判断失败
[[nodiscard]] inline std::ifstream OpenInput(const std::filesystem::path& path,
                                            std::ios::openmode mode = std::ios::binary) {
    return std::ifstream{path, mode};
}

// 写入打开（默认二进制 + 截断）
[[nodiscard]] inline std::ofstream OpenOutput(const std::filesystem::path& path,
                                             std::ios::openmode mode = std::ios::binary | std::ios::trunc) {
    return std::ofstream{path, mode};
}

// 一次读完整文件（二进制安全）；打不开返回 `nullopt`
[[nodiscard]] inline std::optional<std::string> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream in = OpenInput(path);
    if (!in) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);
    std::string out(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        in.read(out.data(), static_cast<std::streamsize>(out.size()));
        out.resize(static_cast<std::size_t>(in.gcount())); // 处理短读
    }
    return out;
}

// 一次写完整文件（二进制安全）；成功返回 true
[[nodiscard]] inline bool WriteFileBytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out = OpenOutput(path);
    if (!out) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

} // namespace shine::util
