#pragma once
// 密钥/模型/BaseURL 解析 —— 密钥只从环境变量或本地 Settings 读，**禁止打日志**
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/Settings.h"
#include "openai/OpenAIProvider.h"

namespace shine::openai {

// 按当前 llmProvider 解析密钥（env 优先）
[[nodiscard]] inline std::string ResolveApiKey() {
    return ResolveActiveProfile().apiKey;
}

// role: "planner" | "writer" | "critic" | ""（默认）；当前 provider 的模型
[[nodiscard]] inline std::string ResolveModel(std::string_view role) {
    const AppSettings& s = Settings();
    const Provider p = ParseProvider(s.llmProvider);
    // 🔴 S53 修：原先 MiniMax / MiMo **直接 `return ResolveActiveProfile().model;`（无视 `role`）**
    // ⇒ 后果不是"少个功能"，而是**死锁**：`06` §2.7 M5 的 auto 前置⑤要求
    // 「`critic` 模型 ≠ `writer` 模型」，而这里两者永远同值 ⇒ **MiniMax 用户永远被拒启动
    // `--novel-run auto`**（真跑实测：改了 `openaiModelCritic=MiniMax-M2.7` 也无效，
    //  `cross_review_ok` 仍为 false）。**按角色的模型配置对所有 provider 都该生效。**
    if (p == Provider::MiMo || p == Provider::MiniMax) {
        if (role == "planner" && !s.openaiModelPlanner.empty()) return s.openaiModelPlanner;
        if (role == "writer" && !s.openaiModelWriter.empty()) return s.openaiModelWriter;
        if (role == "critic" && !s.openaiModelCritic.empty()) return s.openaiModelCritic;
        return ResolveActiveProfile().model;
    }
    if (role == "planner" && !s.openaiModelPlanner.empty()) return s.openaiModelPlanner;
    if (role == "writer" && !s.openaiModelWriter.empty()) return s.openaiModelWriter;
    if (role == "critic" && !s.openaiModelCritic.empty()) return s.openaiModelCritic;
    if (!s.openaiModelDefault.empty()) return s.openaiModelDefault;
    return "gpt-4o-mini";
}

// BaseURL → /responses 绝对地址（容忍尾斜杠与已带 /v1）
[[nodiscard]] inline std::string ResponsesUrl(std::string_view baseUrl) {
    std::string base{baseUrl.empty() ? std::string_view{"https://api.openai.com/v1"} : baseUrl};
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (base.empty()) {
        base = "https://api.openai.com/v1";
    }
    return base + "/responses";
}

// 日志用：只露前后各 3 字符，中间打码（密钥本身绝不进日志）
[[nodiscard]] inline std::string MaskKey(std::string_view key) {
    if (key.empty()) return "<空>";
    if (key.size() <= 8) return "****";
    return std::string{key.substr(0, 3)} + "****" + std::string{key.substr(key.size() - 3)};
}

} // namespace shine::openai
