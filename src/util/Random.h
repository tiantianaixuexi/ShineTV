#pragma once
// shine::util —— 随机与标识符工具（纯函数、无业务语义，可被任意模块复用）
//
// 约定：util 里只放"没有状态、没有业务含义"的东西；一旦带上模块语义（如 Comfy 队列）
// 就留在各自模块里，不要往这里堆。全部 header-only（inline），避免污染 CMakeLists。
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace shine::util {
namespace detail {

// 每个线程一个引擎；inline 函数内的 static thread_local 保证跨 TU 只有一个实例
[[nodiscard]] inline std::mt19937_64& Rng() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    return rng;
}

} // namespace detail

// 小写、零填充的十六进制串，长度 = bytes * 2
[[nodiscard]] inline std::string RandomHex(std::size_t bytes) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string out(bytes * 2, '0');
    auto& rng = detail::Rng();
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto v = static_cast<unsigned>(rng() & 0xFFu);
        out[i * 2] = kHex[v >> 4];
        out[i * 2 + 1] = kHex[v & 0x0Fu];
    }
    return out;
}

// 32 位十六进制（无连字符）—— ComfyUI 的 client_id / prompt_id 用的就是这种形式
[[nodiscard]] inline std::string RandomId32() { return RandomHex(16); }

// [0, maxExclusive) 均匀整数
[[nodiscard]] inline std::uint32_t RandomBelow(std::uint32_t maxExclusive) {
    if (maxExclusive == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(detail::Rng()() % maxExclusive);
}

// 随机种子（ComfyUI 的 seed 参数）
[[nodiscard]] inline std::int64_t RandomSeed() {
    return static_cast<std::int64_t>(detail::Rng()());
}

} // namespace shine::util
