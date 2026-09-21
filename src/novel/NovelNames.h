#pragma once
// ★ **实体名字归一化的唯一实现**（R10 诊断 与 resolver 共用）。
//
// 为什么单独抽出来（用户明确要求）：**R10**（`NovelRepair`：找"同 kind + 同 `name_norm` 的重复实体"）
// 与 **resolver**（`NovelCommit`：把"名字引用"落到真实 id）必须用**完全相同的口径**。
// 若 R10 在 SQL 里自己写一套 `lower(replace(...))`、resolver 在 C++ 里再写一套，两边一定会漂 ——
// 现象就是"扫描说没有重复，但引用解析说'有歧义'"（或反之），那种不一致极难查。
//
// 归一化只做这些（**别加模糊逻辑**：模糊匹配只是**报告用**的提示，绝不用于解析）：
//   · 去掉所有 ASCII 空白（空格 / 制表 / 换行 / 回车）与**全角空格 U+3000**；
//   · ASCII 大写 → 小写（中文及其它字节原样保留）；
//   · **不**去标点、**不**去括号、**不**做同义词或繁简转换（`李默（小）` 与 `李默` 是**两个名字**）。
//
// ⚠️ 由此："`旧信号塔` vs `旧信号塔␠`（尾空格）"算**同一个名字**；
//    "`旧信号塔` vs `老信号塔`" **不算** —— 后者只能靠报文里的"相近名字"提示（**只提示，不自动采用**）。

#include "novel/NovelTypes.h" // `kind::` 常量（kind 匹配表要用）

#include <string>
#include <string_view>

namespace shine::novelcore {

// 去首尾 ASCII 空白（**不做**归一化：报文里要显示调用方原本写的样子）
[[nodiscard]] inline std::string TrimEntityRef(std::string_view s) {
    const auto isWs = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isWs(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && isWs(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return std::string{s.substr(b, e - b)};
}

// 名字归一化（见文件头）。**唯一实现** —— 别在别处再写一份。
[[nodiscard]] inline std::string NormalizeEntityName(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            continue;
        }
        if (ch == 0xE3 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            static_cast<unsigned char>(s[i + 2]) == 0x80) { // U+3000 全角空格
            i += 2;
            continue;
        }
        out.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
    }
    return out;
}

// ★ **「期望 kind」↔「实际 kind」匹配的唯一实现** —— K03（`NovelChecks.cpp`）、resolver
// （`NovelCommit.cpp`）、诊断（`NovelRepair.cpp` 的 R6）**全都用这一份**，别再各写一张表。
//
// ⚠️ 为什么 `item` 期望要**放宽**（这不是偷懒，是**真跑逼出来的**）：契约里"被持有物"的合法类别
//    不止 `item` —— 第 25 章实证：模型写 `"item_ref":"磁带"`，而库里 #106「磁带」的 kind 是 **prop**。
//    若只认 `item`，就出现最坏那种不一致：**同一个引用，填数字 id 走 K03 放行、写名字被 resolver 拒**。
[[nodiscard]] inline bool EntityKindMatches(std::string_view actual, std::string_view expected) {
    if (expected == kind::item) { // 期望"物品" ⇒ 接受同类可持有物（与 K03 原口径一致）
        return actual == kind::item || actual == kind::treasure || actual == kind::prop ||
               actual == kind::clothing || actual == kind::resource;
    }
    return actual == expected;
}

} // namespace shine::novelcore
