// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/dialogs/DrawSettingsWindow.h"
#include "app/AppIncludes.h"
#include "app/ui/Widgets.h"     // R-S1：PathPickerRow（同一段"路径+浏览"原先手写了三遍）
#include "app/FileDialog.h"    // R-S9：配色导出/导入
#include "openai/OpenAIClient.h"
#include "openai/OpenAIProvider.h"
#include "core/Async.h"
#include "mcp/HttpServer.h"
#include "mcp/MCPServer.h"
#include "theme/ThemeTokens.h"  // R-S7/S8：外观段遍历 token 表

#include <cstdlib>
#include <fstream>
#include <iterator>

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
        int proto = Settings().llmProtocol == "responses" ? 1 : 0;
        if (ImGui::Combo("协议", &proto, "Chat Completions\0Responses API\0")) {
            Settings().llmProtocol = proto == 1 ? "responses" : "chat_completions";
        }
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
    ImGui::Checkbox("启用 MCP（仅本机，无鉴权）", &Settings().mcpEnabled);
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
