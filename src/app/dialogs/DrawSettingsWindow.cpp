// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/dialogs/DrawSettingsWindow.h"
#include "app/AppIncludes.h"
#include "app/ui/Widgets.h"     // R-S1：PathPickerRow（同一段"路径+浏览"原先手写了三遍）
#include "app/FileDialog.h"    // R-S9：配色导出/导入
#include "openai/OpenAIClient.h"
#include "openai/OpenAIProvider.h"
#include "core/Settings.h"
#include "db/Db.h"
#include "db/redis/RedisError.h"
#include "novel/NovelDb.h"
#include "novel/NovelImageGen.h"
#include "novel/NovelMcpTools.h"
#include "util/Encoding.h"
#include "core/Async.h"
#include "mcp/HttpServer.h"
#include "mcp/MCPServer.h"
#include "theme/ThemeTokens.h"  // R-S7/S8：外观段遍历 token 表

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace shine::app {

void DrawSettingsWindow() {
    if (!State().showSettings) return;
    ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("设置", &State().showSettings, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (State().editComfyUrl.empty()) {
        State().editComfyUrl = Settings().comfyBaseUrl;
        State().editThemeId = Settings().themeId;
    }
    ImGui::SeparatorText("主题");
    if (ImGui::BeginTable("theme_table", 4, ImGuiTableFlags_SizingStretchSame)) {
        for (const auto& p : theme::Presets()) {
            ImGui::TableNextColumn();
            const bool selected = (State().editThemeId == p.id);
            if (ImGui::Selectable(p.name.c_str(), selected, 0, ImVec2(-1, 36))) {
                State().editThemeId = p.id;
                Settings().themeId = p.id;
                theme::ApplyPresetById(p.id);
                SaveSettings();
                log::Info("主题 -> {}", p.id);
            }
            const ImVec2 r = ImGui::GetItemRectMin();
            const ImVec2 rs = ImGui::GetItemRectSize();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(r.x + 4, r.y + rs.y - 8),
                ImVec2(r.x + rs.x - 4, r.y + rs.y - 4),
                ImGui::ColorConvertFloat4ToU32(ImVec4(p.colors.accent[0], p.colors.accent[1],
                                                      p.colors.accent[2], 1.f)),
                2.f);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("ComfyUI");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("服务地址", &State().editComfyUrl)) {
        Settings().comfyBaseUrl = State().editComfyUrl;
    }
    if (ImGui::Button("保存并连接")) {
        SaveSettings();
        comfy::ComfySession::Instance().SetBaseUrl(State().editComfyUrl);
    }
    ImGui::SameLine();
    if (ImGui::Button("保存")) {
        SaveSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", SettingsPath().c_str());
    ImGui::Spacing();
    DrawConnectionStatus();
    ImGui::TextDisabled("clientId：%s", comfy::ComfySession::Instance().ClientId().c_str());

    // —— 多模型 LLM（OpenAI / MiMo / MiniMax / custom）——
    ImGui::SeparatorText("LLM 模型");
    if (State().editOpenaiBaseUrl.empty() && State().editMimoBaseUrl.empty()) {
        State().editOpenaiBaseUrl = Settings().openaiBaseUrl;
        State().editOpenaiApiKey = Settings().openaiApiKey;
        State().editOpenaiModelDefault = Settings().openaiModelDefault;
        State().editMimoBaseUrl = Settings().mimoBaseUrl;
        State().editMimoApiKey = Settings().mimoApiKey;
        State().editMimoModel = Settings().mimoModel;
        State().editMinimaxBaseUrl = Settings().minimaxBaseUrl;
        State().editMinimaxApiKey = Settings().minimaxApiKey;
        State().editMinimaxModel = Settings().minimaxModel;
        State().editLlmProvider = static_cast<int>(
            openai::ParseProvider(Settings().llmProvider));
    }
    static const char* kProviders[] = {"OpenAI", "Xiaomi MiMo", "MiniMax", "自定义"};
    if (ImGui::Combo("当前 Provider", &State().editLlmProvider, kProviders, 4)) {
        Settings().llmProvider = std::string{openai::ProviderId(
            static_cast<openai::Provider>(State().editLlmProvider))};
        SaveSettings();
        log::Info("LLM Provider -> {}", Settings().llmProvider);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("切换后「生成本章」立即使用新家");
    {
        const auto active = openai::ResolveActiveProfile();
        ImGui::TextDisabled("生效：%s · %s · 密钥 %s",
                            std::string{openai::ProviderLabel(active.provider)}.c_str(),
                            active.model.c_str(),
                            active.apiKey.empty() ? "未配置" : "已配置");
    }

    const auto prov = static_cast<openai::Provider>(State().editLlmProvider);
    if (prov == openai::Provider::MiMo) {
        ImGui::Checkbox("使用 Token Plan（订阅，tp- 密钥）", &Settings().mimoTokenPlan);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "按量：api.xiaomimimo.com/v1 + sk-\nToken Plan：token-plan-cn.xiaomimimo.com/v1 + tp-\n"
                "新加坡：token-plan-sgp…  欧洲：token-plan-ams…\n"
                "注意：官方约定 Token Plan 主要供编程工具，滥用可能封 Key");
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("MiMo Base URL", &State().editMimoBaseUrl)) {
            Settings().mimoBaseUrl = State().editMimoBaseUrl;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("重置 Base")) {
            State().editMimoBaseUrl = Settings().mimoTokenPlan
                                          ? "https://token-plan-cn.xiaomimimo.com/v1"
                                          : "https://api.xiaomimimo.com/v1";
            Settings().mimoBaseUrl = State().editMimoBaseUrl;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("MiMo API Key", &State().editMimoApiKey, ImGuiInputTextFlags_Password)) {
            Settings().mimoApiKey = State().editMimoApiKey;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("优先环境变量 MIMO_API_KEY；按量 sk- / Token Plan tp-");
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("MiMo 模型", &State().editMimoModel)) {
            Settings().mimoModel = State().editMimoModel;
        }
        ImGui::TextDisabled("模型：mimo-v2.5-pro / mimo-v2.5（v2 系列已下线）");
    } else if (prov == openai::Provider::MiniMax) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("MiniMax Base URL", &State().editMinimaxBaseUrl)) {
            Settings().minimaxBaseUrl = State().editMinimaxBaseUrl;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("MiniMax API Key", &State().editMinimaxApiKey,
                             ImGuiInputTextFlags_Password)) {
            Settings().minimaxApiKey = State().editMinimaxApiKey;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("优先环境变量 MINIMAX_API_KEY");
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("MiniMax 模型", &State().editMinimaxModel)) {
            Settings().minimaxModel = State().editMinimaxModel;
        }
        ImGui::Checkbox("关闭 M3 思考（正文更干净）", &Settings().minimaxDisableThinking);
        ImGui::TextDisabled("文档：platform.minimax.cn · 默认 https://api.minimax.cn/v1 · MiniMax-M3");
    } else {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("Base URL", &State().editOpenaiBaseUrl)) {
            Settings().openaiBaseUrl = State().editOpenaiBaseUrl;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("API Key", &State().editOpenaiApiKey, ImGuiInputTextFlags_Password)) {
            Settings().openaiApiKey = State().editOpenaiApiKey;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("优先环境变量 OPENAI_API_KEY");
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("默认模型", &State().editOpenaiModelDefault)) {
            Settings().openaiModelDefault = State().editOpenaiModelDefault;
        }
        int proto = Settings().llmProtocol == "responses"   ? 1
                    : Settings().llmProtocol == "anthropic" ? 2
                                                            : 0;
        if (ImGui::Combo("协议", &proto, "Chat Completions\0Responses API\0Anthropic Messages\0")) {
            Settings().llmProtocol = proto == 1   ? "responses"
                                     : proto == 2 ? "anthropic"
                                                  : "chat_completions";
        }
        ImGui::TextDisabled("Anthropic：MiMo/MiniMax 的 /anthropic 兼容端（x-api-key）");
    }

    if (ImGui::Button("保存 LLM 设置")) {
        SaveSettings();
        log::Info("LLM 设置已保存：provider={} model={}", Settings().llmProvider,
                  openai::ResolveActiveProfile().model);
    }
    ImGui::SameLine();
    if (ImGui::Button("Chat 自检")) {
        (void)openai::RunChatSelfCheck();
        (void)openai::RunChatSessionSelfCheck();
    }
    ImGui::SameLine();
    if (ImGui::Button("Responses 自检")) {
        (void)openai::RunParseSelfCheck();
    }
    ImGui::SameLine();
    if (ImGui::Button("联调-简单")) {
        async::RunOnWorker([]() {
            auto r = openai::LivePing(0);
            if (r) {
                log::Info("联调成功：{}", r->substr(0, std::min<std::size_t>(r->size(), 80)));
            } else {
                log::Error("联调失败：{} {}", r.error().code, r.error().message);
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("联调-JSON")) {
        async::RunOnWorker([]() {
            auto r = openai::LivePing(1);
            if (r) {
                log::Info("JSON 联调成功：{}", r->substr(0, std::min<std::size_t>(r->size(), 120)));
            } else {
                log::Error("JSON 联调失败：{} {}", r.error().code, r.error().message);
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("联调-工具")) {
        async::RunOnWorker([]() {
            auto r = openai::LivePing(2);
            if (r) {
                log::Info("工具联调成功：{}", r->substr(0, std::min<std::size_t>(r->size(), 120)));
            } else {
                log::Error("工具联调失败：{} {}", r.error().code, r.error().message);
            }
        });
    }
    ImGui::SameLine();
    ImGui::TextDisabled("密钥不进仓库/日志");

    // —— Garnet / Redis（P10.4）——
    ImGui::SeparatorText("Garnet / Redis");
    ImGui::Checkbox("启用 Redis 池（Garnet 兼容）", &Settings().redisEnabled);
    ImGui::SetNextItemWidth(180.f);
    ImGui::InputText("Redis 主机", &Settings().redisHost);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.f);
    ImGui::InputInt("端口", &Settings().redisPort);
    ImGui::SetNextItemWidth(180.f);
    if (ImGui::InputText("Redis 密码", &Settings().redisPassword, ImGuiInputTextFlags_Password)) {
    }
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("DB", &Settings().redisDb);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("最大连接", &Settings().redisMaxConn);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("最小连接", &Settings().redisMinConn);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0=懒连接（Garnet 未起也不挡启动）；>0 启动预热");
    }
    ImGui::TextDisabled("池状态：ready=%s · max=%zu idle=%zu（挂起时业务走 SQLite）",
                        db::redisReady() ? "是" : "否", db::redisStats().maxConnections,
                        db::redisStats().idle);
    if (ImGui::Button("应用 Redis 设置")) {
        SaveSettings();
        db::Shutdown();
        db::DbConfig cfg;
        cfg.enableRedis = Settings().redisEnabled;
        cfg.redis.connect.host =
            Settings().redisHost.empty() ? "127.0.0.1" : Settings().redisHost;
        cfg.redis.connect.port = Settings().redisPort > 0 ? Settings().redisPort : 6379;
        cfg.redis.connect.password = Settings().redisPassword;
        cfg.redis.connect.db = Settings().redisDb;
        cfg.redis.maxConnections = Settings().redisMaxConn > 0
                                       ? static_cast<std::size_t>(Settings().redisMaxConn)
                                       : 8u;
        cfg.redis.minConnections = Settings().redisMinConn >= 0
                                       ? static_cast<std::size_t>(Settings().redisMinConn)
                                       : 0u;
        cfg.redis.acquireTimeout = std::chrono::milliseconds{2000};
        if (auto r = db::Init(cfg); !r) {
            log::Warn("Redis 重载失败：{}", db::redis::ToChar(r.error().kind));
        } else {
            log::Info("Redis 重载 OK ready={}", db::redisReady() ? 1 : 0);
        }
    }

    // —— P9.1：小说出图后端 ——
    ImGui::SeparatorText("出图（小说视觉）");
    if (State().editImageBaseUrl.empty() && State().editImageModel.empty()) {
        State().editImageBackend = Settings().imageBackend;
        State().editImageBaseUrl = Settings().imageBaseUrl;
        State().editImageApiKey = Settings().imageApiKey;
        State().editImageModel = Settings().imageModel;
        State().editImageWidth = Settings().imageWidth;
        State().editImageHeight = Settings().imageHeight;
        State().editImageSteps = Settings().imageSteps;
    }
    {
        int backendIdx = 0; // mock
        if (Settings().imageBackend == "openai_images" || Settings().imageBackend == "openai") {
            backendIdx = 1;
        } else if (Settings().imageBackend == "comfy") {
            backendIdx = 2;
        }
        static const char* kImageBackends[] = {"mock（离线占位）", "openai_images（HTTP）", "comfy（P9.2）"};
        if (ImGui::Combo("出图后端", &backendIdx, kImageBackends, 3)) {
            switch (backendIdx) {
            case 1:
                Settings().imageBackend = "openai_images";
                break;
            case 2:
                Settings().imageBackend = "comfy";
                break;
            default:
                Settings().imageBackend = "mock";
                break;
            }
            State().editImageBackend = Settings().imageBackend;
            SaveSettings();
            log::Info("出图后端 -> {}", Settings().imageBackend);
        }
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("出图 Base URL", &State().editImageBaseUrl)) {
        Settings().imageBaseUrl = State().editImageBaseUrl;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("空 = 跟随 OpenAI Base URL；自托管请填到 /v1");
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("出图 API Key", &State().editImageApiKey, ImGuiInputTextFlags_Password)) {
        Settings().imageApiKey = State().editImageApiKey;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("空 = OPENAI_API_KEY 或 LLM 的 openaiApiKey；禁止打日志");
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("出图模型", &State().editImageModel)) {
        Settings().imageModel = State().editImageModel;
    }
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("宽", &State().editImageWidth)) {
        if (State().editImageWidth < 64) State().editImageWidth = 64;
        Settings().imageWidth = State().editImageWidth;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("高", &State().editImageHeight)) {
        if (State().editImageHeight < 64) State().editImageHeight = 64;
        Settings().imageHeight = State().editImageHeight;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("步数", &State().editImageSteps)) {
        if (State().editImageSteps < 1) State().editImageSteps = 1;
        Settings().imageSteps = State().editImageSteps;
    }
    ImGui::TextDisabled("落盘：工程 visual/gen · 生成结果默认 PROPOSED");
    if (ImGui::Button("保存出图设置")) {
        SaveSettings();
        log::Info("出图设置已保存：backend={} model={} {}x{}", Settings().imageBackend,
                  Settings().imageModel, Settings().imageWidth, Settings().imageHeight);
    }
    ImGui::SameLine();
    if (ImGui::Button("出图自检（离线）")) {
        (void)::shine::novelcore::RunImageGenSelfCheck();
    }

    // —— G-S0 S8：图库设置（本地目录 / ComfyUI output·input / 缩略图档 / 预算）——
    ImGui::SeparatorText("图库");
    if (State().editGalleryLocalDir.empty()) {
        State().editGalleryLocalDir = Settings().galleryLocalDir;
        State().editGalleryOutputDir = Settings().comfyOutputDir;
        State().editGalleryInputDir = Settings().comfyInputDir;
    }
    // R-S1：三段同构的「路径 + 浏览」改用 app::ui::PathPickerRow（行为不变）
    if (ui::PathPickerRow("本地目录", "例如 D:\\Pictures", State().editGalleryLocalDir, "选择图片文件夹", /*folder=*/true)) {
        Settings().galleryLocalDir = State().editGalleryLocalDir;
    }
    ImGui::SameLine();
    if (ImGui::Button("打开图片文件夹...##open")) {
        const std::string picked = SelectFolderDialog("打开图片文件夹", State().editGalleryLocalDir);
        if (!picked.empty()) {
            State().editGalleryLocalDir = picked;
            Settings().galleryLocalDir = picked;
            SaveSettings();
            log::Info("图库：已选择本地目录 {}", picked);
        }
    }
    if (ui::PathPickerRow("ComfyUI output 目录", "例如 F:\\AI\\ComfyUI-aki-v3\\ComfyUI\\output",
                          State().editGalleryOutputDir, "选择 ComfyUI output 目录", /*folder=*/true)) {
        Settings().comfyOutputDir = State().editGalleryOutputDir;
    }
    if (ui::PathPickerRow("ComfyUI input 目录", "例如 F:\\AI\\ComfyUI-aki-v3\\ComfyUI\\input",
                          State().editGalleryInputDir, "选择 ComfyUI input 目录", /*folder=*/true)) {
        Settings().comfyInputDir = State().editGalleryInputDir;
    }
    static const char* kThumbLabels[] = {"128", "256", "512"};
    static const int kThumbValues[] = {128, 256, 512};
    int thumbIndex = 1;
    for (int i = 0; i < 3; ++i) {
        if (kThumbValues[i] == Settings().galleryThumbSize) {
            thumbIndex = i;
        }
    }
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("缩略图尺寸", &thumbIndex, kThumbLabels, 3)) {
        Settings().galleryThumbSize = kThumbValues[thumbIndex];
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("GPU 预算 MB", &Settings().galleryGpuBudgetMB);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("CPU 预算 MB", &Settings().galleryCpuBudgetMB);
    ImGui::Checkbox("查看器打开时适应窗口", &Settings().viewerFitOnOpen);
    if (ImGui::Button("保存图库设置")) {
        SaveSettings();
        log::Info("图库设置已保存：本地={} output={} input={} 缩略图={}px GPU={}MB CPU={}MB",
                  Settings().galleryLocalDir.empty() ? "<空>" : Settings().galleryLocalDir,
                  Settings().comfyOutputDir.empty() ? "<空>" : Settings().comfyOutputDir,
                  Settings().comfyInputDir.empty() ? "<空>" : Settings().comfyInputDir,
                  Settings().galleryThumbSize, Settings().galleryGpuBudgetMB, Settings().galleryCpuBudgetMB);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("设置文件：%s", SettingsPath().c_str());

    // —— R-S7/S8：外观（25 个主题颜色都可改，改完立即生效；点保存才落盘）——
    ImGui::SeparatorText("外观");
    ImGui::TextDisabled("改动立即生效；点「保存配色」写入 settings.json（重启保留）");
    for (const char* group : {"基础", "强调与状态", "控件", "标签页"}) {
        if (!ImGui::CollapsingHeader(group)) {
            continue;
        }
        for (const theme::ThemeToken& token : theme::Tokens()) {
            if (std::strcmp(token.group, group) != 0) {
                continue;
            }
            float rgba[4] = {0.f, 0.f, 0.f, 1.f};
            std::memcpy(rgba, &(theme::Current().*(token.member)), sizeof(rgba));
            ImGui::PushID(token.name);
            ImGui::SetNextItemWidth(140);
            const std::string editId = std::string{"##"} + token.name;
            if (ImGui::ColorEdit4(editId.c_str(), rgba,
                                  ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_NoLabel)) {
                theme::SetOverride(token.name, {rgba[0], rgba[1], rgba[2], rgba[3]});
                theme::ReapplyTheme(); // 立即生效
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(token.name);
            ImGui::SameLine();
            if (ImGui::SmallButton("↺")) {
                theme::ClearOverride(token.name);
                theme::ReapplyTheme();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("把 %s 恢复为预设值", token.name);
            }
            ImGui::PopID();
        }
    }
    if (ImGui::Button("保存配色")) {
        Settings().themeCustomColors = theme::ExportOverrides();
        SaveSettings();
        log::Info("外观：已保存 {} 个自定义颜色", theme::OverrideCount());
    }
    ImGui::SameLine();
    if (ImGui::Button("重置为预设")) {
        theme::ClearOverrides();
        Settings().themeCustomColors.clear();
        theme::ReapplyTheme();
        SaveSettings();
        log::Info("外观：已重置为预设 {}", Settings().themeId);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("已自定义 %zu / %zu 个颜色", theme::OverrideCount(), theme::Tokens().size());

    // —— R-S9：配色导出 / 导入（纯文本 `name=#RRGGBBAA;…`，可分享给同事）——
    if (ImGui::Button("导出配色…")) {
        const std::string path =
            SaveFileDialog("导出配色", "shine-theme.txt", {{"配色文本", "*.txt"}, {"全部文件", "*.*"}});
        if (!path.empty()) {
            std::ofstream out(path, std::ios::binary);
            out << theme::ExportOverrides();
            log::Info("外观：配色已导出到 {}（{} 个颜色）", path, theme::OverrideCount());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("导入配色…")) {
        const std::string path = OpenFileDialog("导入配色", {{"配色文本", "*.txt"}, {"全部文件", "*.*"}});
        if (!path.empty()) {
            std::ifstream in(path, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            theme::ApplyOverrides(text); // 立即生效
            Settings().themeCustomColors = theme::ExportOverrides();
            SaveSettings();
            log::Info("外观：已从 {} 导入，命中 {} 个颜色", path, theme::OverrideCount());
        }
    }

    // —— R-S9：色板预览（25 个 token 各一格，改完一眼看到整体效果）——
    ImGui::TextDisabled("色板预览");
    for (const theme::ThemeToken& token : theme::Tokens()) {
        const float* rgba = theme::Current().*(token.member);
        ImGui::ColorButton(token.name, ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(18.f, 18.f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s = %s", token.name, theme::ToHex(rgba).c_str());
        }
        if (ImGui::GetCursorPosX() > 0.f) { /* 依次横向排列 */
        }
    }
    ImGui::NewLine();

    // —— P7.5：MCP 服务设置（运行时启停 + 端口校验）——
    ImGui::SeparatorText("MCP 服务");
    ImGui::Checkbox("启用 MCP HTTP（仅本机，无鉴权）", &Settings().mcpEnabled);
    ImGui::Checkbox("允许 MCP 写工具（默认关；写仍 PROPOSED）", &Settings().mcpAllowWrite);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("也可用环境变量 SHINE_MCP_ALLOW_WRITE=1\n写路径会记 audit_logs，结果默认 PROPOSED");
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("小说库 novel.db（MCP）", &Settings().mcpNovelDbPath)) {
        // 即时可挂库（若尚未打开）
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("空 = 不自动挂库；stdio/HTTP 调用 novel_* 前需有库\n也可用环境变量 SHINE_NOVEL_DB");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("用当前工程")) {
        if (::shine::novelcore::NovelDb::Instance().isOpen()) {
            Settings().mcpNovelDbPath =
                util::PathToUtf8(::shine::novelcore::NovelDb::Instance().path());
        }
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("MCP 监听地址", &Settings().mcpListenAddr);
    ImGui::SetNextItemWidth(160.f);
    ImGui::InputInt("MCP 端口", &Settings().mcpPort);
    static std::string g_mcpPortError;
    if (Settings().mcpPort < 1024 || Settings().mcpPort > 65535) {
        g_mcpPortError = "端口必须在 1024–65535（默认 8931）";
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.f), "%s", g_mcpPortError.c_str());
    } else {
        g_mcpPortError.clear();
    }
    if (ImGui::Button("应用 MCP 设置")) {
        if (!g_mcpPortError.empty()) {
            log::Warn("MCP 设置拒绝保存：{}", g_mcpPortError);
        } else {
            SaveSettings();
            ::shine::novelcore::SetMcpAllowWrite(Settings().mcpAllowWrite);
            // 运行时启停：先停旧监听，再按新配置启动
            ::shine::mcp::StopHttpFromSettings();
            if (Settings().mcpEnabled) {
                if (auto r = ::shine::mcp::StartHttpFromSettings(); !r) {
                    log::Error("MCP 启动失败：{}", r.error());
                }
            } else {
                log::Info("MCP 已停用");
            }
        }
    }
    ImGui::SameLine();
    const auto& mcpSrv = ::shine::mcp::HttpServer::Instance();
    ImGui::Text("状态：%s%s", mcpSrv.Running() ? "运行中" : "未监听",
                mcpSrv.Running() ? "" : "（启用后点「应用」）");
    if (mcpSrv.Running()) {
        ImGui::TextDisabled("http://%s:%d/mcp  tools=%llu", mcpSrv.ListenAddr().c_str(),
                            mcpSrv.Port(),
                            static_cast<unsigned long long>(::shine::mcp::McpRequestCount()));
    }
    {
        const auto last = ::shine::mcp::GetLastCallInfo();
        if (!last.tool.empty()) {
            ImGui::TextDisabled("最近 tools/call：%s %s %s", last.tool.c_str(),
                                last.ok ? "成功" : "失败", last.timeText.c_str());
        }
    }

    ImGui::SeparatorText("关于");
    ImGui::TextUnformatted("ShineTV Studio 0.2.0（P2 GraphHost）");
    ImGui::TextUnformatted("C++26 / ImGui Docking / VisualNodeSystem");
    ImGui::TextUnformatted("GCC 16.1.0 (MinGW64)");
    ImGui::TextUnformatted("mimalloc / spdlog+fmt / stdexec / libhv / yyjson");
    ImGui::TextUnformatted("界面字体：微软雅黑（简体中文）");
    ImGui::End();
}

} // namespace shine::app
