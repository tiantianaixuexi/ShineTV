#pragma once
#include <string>

namespace shine {

struct AppSettings {
    std::string themeId = "dark";
    std::string comfyBaseUrl = "http://127.0.0.1:8188";
    // P4.2：媒体本地缓存目录（空 = 默认 %APPDATA%\ShineTVStudio\cache\media；反射序列化自动读写）
    std::string mediaCacheDir;

    // —— G-S0 S7：图片库设置（反射序列化自动读写，无需手写 yyjson）——
    std::string gallerySource = "local"; // local | comfy_output | comfy_input
    std::string galleryLocalDir;         // 上次浏览的本地目录
    std::string comfyOutputDir;          // ComfyUI output 绝对路径（空 = 该来源不可用）
    std::string comfyInputDir;           // ComfyUI input 绝对路径（空 = 该来源不可用）
    int galleryThumbSize = 256;          // 128 | 256 | 512
    int galleryGpuBudgetMB = 512;
    int galleryCpuBudgetMB = 256;
    bool viewerFitOnOpen = true;

    // R-S7：用户自定义主题配色（`name=#RRGGBBAA;…`，空 = 完全跟随预设）
    // 刻意不用 JSON 字符串字段名（`themeCustomJson`）：少一个解析依赖、肉眼可读
    std::string themeCustomColors;

    // —— P5.1 S4：视频分镜目录（反射序列化自动读写）——
    std::string videoProjectDir;  // 分镜工程（.json）默认目录
    std::string videoOutputDir;   // 生成视频的落盘目录（P5.5 S5 的 /view 下载目标）
    std::string mediaLibraryDir;  // 素材根目录：分镜里**相对路径**按它解析（P5.2 S5）

    // 小说工程根目录（空 = %APPDATA%\ShineTVStudio\novels）
    std::string novelRootDir;

    // —— 多模型 LLM（密钥仅本地/环境变量；日志禁止打印）——
    // provider: openai | mimo | minimax | custom
    std::string llmProvider = "openai";
    // OpenAI / custom（Responses 或 Chat，见 llmProtocol）
    std::string openaiBaseUrl = "https://api.openai.com/v1";
    std::string openaiApiKey;
    std::string openaiModelDefault = "gpt-4o-mini";
    std::string openaiModelPlanner;
    std::string openaiModelWriter;
    std::string openaiModelCritic;
    // Xiaomi MiMo
    // 按量：https://api.xiaomimimo.com/v1（sk-）
    // Token Plan：https://token-plan-cn.xiaomimimo.com/v1（tp-，另有 sgp/ams 集群）
    // 文档：https://mimo.mi.com/docs/zh-CN/quick-start/summary/first-api-call
    //       https://mimo.mi.com/docs/zh-CN/tokenplan/Token Plan/subscription
    bool mimoTokenPlan = false; // true → 用 Token Plan Base URL（下方 mimoBaseUrl 可覆盖集群）
    std::string mimoBaseUrl = "https://api.xiaomimimo.com/v1";
    std::string mimoApiKey; // sk- 或 tp-
    std::string mimoModel = "mimo-v2.5-pro";
    // MiniMax（OpenAI Chat Completions 兼容）
    // 文档：https://platform.minimax.cn/docs/api-reference/text-openai-api
    std::string minimaxBaseUrl = "https://api.minimax.cn/v1";
    std::string minimaxApiKey;
    std::string minimaxModel = "MiniMax-M3";
    bool minimaxDisableThinking = true; // M3 默认开思考；小说正文建议关
    // custom：沿用 openaiBaseUrl/Key/Model，协议由 llmProtocol 指定
    // protocol: chat_completions | responses | anthropic
    std::string llmProtocol = "chat_completions";

    bool showDemoWindow = false;
    bool firstRun = true;

    // —— P7.2：MCP HTTP Server（默认仅本机、无鉴权；运行时启停见 P7.5）——
    bool mcpEnabled = false;
    std::string mcpListenAddr = "127.0.0.1";
    int mcpPort = 8931;
};

[[nodiscard]] AppSettings& Settings();
void LoadSettings();
void SaveSettings();
[[nodiscard]] std::string SettingsPath();

} // namespace shine
