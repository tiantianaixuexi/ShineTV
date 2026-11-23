#pragma once
// shine::kit —— FNV-1a 文本指纹（UI 侧「数据变了才重建」缓存共用）
//
// 从 kit/Image.cpp 提出来：图片两档 key 与页面缓存（KV 表 / 页眉文案 / 时间线
// 格子）都要对一条字符串算**稳定**哈希。同一个算法只留一份 —— 别处再抄一份
// 就是第二份真相，日后改了其中一份，两边的缓存失效判据就对不上了。
//
// 用途模式（页面层每帧零分配的缓存）：
//   key = HashMix(0, Fnv1a(text1));
//   key = HashMix(key, static_cast<std::uint64_t>(someInt));
//   if (key != cachedKey_) { cachedKey_ = key; Rebuild(); }
#include <cstdint>
#include <cstring>
#include <string_view>

namespace shine::kit {

// FNV-1a 64 位。只做 UI 侧的指纹 / 缓存 key，不是密码学哈希。
inline std::uint64_t Fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;  // FNV offset basis
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull;  // FNV prime
    }
    return hash;
}

// 把一个整数混进指纹。seed 传上一轮结果（起点传 0）。
inline std::uint64_t HashMix(std::uint64_t seed, std::uint64_t value) noexcept {
    return (seed ^ value) * 1099511628211ull;
}

// double **按位**混入。直接 cast 会截断小数 —— 6.0 与 6.5 显示不同，指纹必须不同。
inline std::uint64_t HashMix(std::uint64_t seed, double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return (seed ^ bits) * 1099511628211ull;
}

} // namespace shine::kit
