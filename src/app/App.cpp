#include "app/App.h"
#include "app/DockLayout.h"
#include "app/FileDialog.h"
#include "net/LibhvReady.h" // EnsureLibhvReady()（必须在建线程池前初始化 libhv）
#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/Gallery.h" // G-S5：图库模块入口（Init/Shutdown/Tick/RequestScan）
#include "gallery/ImageLoader.h"
#include "graph/GraphHost.h"
#include "graph/WorkflowIO.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTextureCache.h"
#include "gpu/GpuTextureManager.h"
#include "app/gallery/FolderPicker.h" // G-S5：PickAndScanLocalFolder（UI 层编排）
#include "media/MediaLibrary.h"
#include "app/output/OutputView.h"
#include "openai/OpenAIClient.h"
#include "openai/OpenAIProvider.h"
#include "openai/OpenAIAnthropic.h"
#include "agent/ContextBuilder.h"
#include "agent/ToolRegistry.h"
#include "agent/NovelDirector.h"
#include "agent/AgentKit.h"
#include "mcp/McpBootstrap.h"
#include "mcp/HttpServer.h"
#include "mcp/MCPServer.h"
#include "app/novel/NovelView.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "novel/NovelFields.h"
#include "novel/NovelMcpTools.h"
#include "theme/Theme.h"
#include "util/Strings.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "app/AppInternal.h"   // R-S0：搬出去的窗口入口声明 + 过渡别名
#include "theme/ThemeTokens.h" // R-S7：自定义配色（预设 + 覆盖）
#include "app/UiState.h"                        // R-S0：应用级 UI 状态（原 g_* 全局）
#include "app/views/temp/TextureSelfCheck.h"    // R-S0：已搬出 App.cpp（TEMP-G3）
#include "app/shots/ShotTableView.h"            // P5.3：分镜模块 Tick
#include "video/SceneToImageBuilder.h"          // P5.7：SHINE_SCENE_IMAGE_CHECK

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <string>
#include <vector>

namespace shine::app {
namespace {

// 六区布局：①顶栏 ②活动栏 ③侧栏 ④中央图 ⑤右栏 ⑥底栏
// ② 切换 ③ 的内容



// —— 自检模式（R-S0 收尾）：`SHINE_EXIT_AFTER_SEC=N` → 跑 N 秒后**自行退出**。
// 动机：AI/CI 跑 GUI 看日志时，若程序常驻，外部结束它会留下"尸体进程条目"（0 线程/0 句柄、
// 谁都删不掉、还会短暂锁住 exe）。让程序自己退出最干净 —— 见 `MEMORY.md` 的验收约定。
double g_selfExitAfterSec = 0.0;
std::chrono::steady_clock::time_point g_selfExitStart{};
bool g_selfExitRequested = false;





// ---------- 活动栏（最左，切换侧栏视图） ----------


// ---------- 各侧栏视图内容 ----------














// ---------- 中央 / 右栏 / 底栏 ----------















// VS Code 风格底栏






} // namespace


// 注意：计时逻辑必须放在这里 —— 主循环每轮都会调用 `ShouldExit()`，
// 而 `DrawFrame()` 在「窗口被遮挡」的分支里会被 `continue` **跳过**（Present 返回 OCCLUDED），
// 放在 DrawFrame 里会导致最小化/被挡住时永不退出。
bool ShouldExit() noexcept {
    if (g_selfExitAfterSec > 0.0 && !g_selfExitRequested &&
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_selfExitStart).count() >=
            g_selfExitAfterSec) {
        g_selfExitRequested = true;
        log::Info("自检模式：已到 {} 秒，正常退出", g_selfExitAfterSec);
    }
    return g_selfExitRequested;
}

bool Init() {
    log::Init();
    // ⚠️ **必须在这里**（创建线程池 / 任何模块之前）初始化 libhv：
    // libhv 的默认 logger 惰性初始化且无锁，首次写日志会 `atexit(...)`（`third/libhv/base/hlog.c:534`）；
    // 若第一个碰 libhv 的是 **worker 线程**，而主线程同时也在初始化（或已在 CRT 退出流程里），
    // 就会在 msvcrt 的 `_onexit` 临界区上死锁 → worker 永不返回。详见 `comfy/ComfyHttp.h`。
    net::EnsureLibhvReady();
    if (const char* raw = std::getenv("SHINE_EXIT_AFTER_SEC"); raw != nullptr && *raw != '\0') {
        g_selfExitAfterSec = std::atof(raw);
        g_selfExitStart = std::chrono::steady_clock::now();
        log::Info("自检模式：{} 秒后自动退出（SHINE_EXIT_AFTER_SEC）", g_selfExitAfterSec);
    }
    // 自检辅助（截图验收用）：`SHINE_SIDE_VIEW=shots|assets|nodes|workflows|comfy|gallery` 直接停在某个侧栏
    if (const char* raw = std::getenv("SHINE_SIDE_VIEW"); raw != nullptr && *raw != '\0') {
        const std::string want = util::ToLower(raw);
        SideView target = State().sideView;
        bool matched = true;
        if (want == "assets") target = SideView::Assets;
        else if (want == "nodes") target = SideView::Nodes;
        else if (want == "workflows") target = SideView::Workflows;
        else if (want == "comfy") target = SideView::Comfy;
        else if (want == "shots") target = SideView::Shots;
        else if (want == "gallery") target = SideView::Gallery;
        else matched = false;
        if (matched) {
            State().sideView = target;
            State().sideOpen = true;
            log::Info("自检模式：侧栏停在「{}」（SHINE_SIDE_VIEW={}）", SideViewTitle(target), raw);
        } else {
            log::Warn("SHINE_SIDE_VIEW={} 不认识（可用：assets/nodes/workflows/comfy/shots/gallery）", raw);
        }
    }
    // 自检辅助（截图验收用）：`SHINE_WINDOW=nodes|templates|shots|gallery` 直接把对应的独立浮窗/中央页打开
    if (const char* raw = std::getenv("SHINE_WINDOW"); raw != nullptr && *raw != '\0') {
        const std::string want = util::ToLower(raw);
        if (want == "nodes") {
            State().showNodeWindow = true;
            log::Info("自检模式：打开节点浏览器（SHINE_WINDOW=nodes）");
        } else if (want == "templates") {
            State().showTemplateWindow = true;
            log::Info("自检模式：打开工作流模板窗（SHINE_WINDOW=templates）");
        } else if (want == "shots" || want == "gallery") {
            // 中央区切到「分镜」/「图库」页（dock 的选中页由 imgui.ini 决定 → 开局抢几帧焦点）
            State().focusFrames = 10;
            State().focusWindow = (want == "shots") ? FocusWindow::Shots : FocusWindow::Gallery;
            log::Info("自检模式：中央区切到「{}」（SHINE_WINDOW={}）", want == "shots" ? "分镜" : "图库", raw);
        } else {
            log::Warn("SHINE_WINDOW={} 不认识（可用：nodes/templates/shots/gallery）", raw);
        }
    }
    if (gpu::Ready()) {
        log::Info("gpu device attached"); // P4.1 S1 验收判据（此处 logger 已就绪）
    } else {
        log::Warn("GPU 设备未注入：纹理会不可用（检查 main.cpp 的 AttachDevice 顺序）");
    }
    async::Init();
    LoadSettings();
    // P1 自检：SHINE_OPENAI_CHECK=1 跑离线验收后自动退出（不发网络）
    if (const char* raw = std::getenv("SHINE_OPENAI_CHECK"); raw != nullptr && *raw != '\0' &&
        std::string_view{raw} != "0") {
        const bool pass = openai::RunOfflineSelfCheck();
        log::Info("SHINE_OPENAI_CHECK：{}", pass ? "PASS" : "FAIL");
        g_selfExitRequested = true;
    }
    // P2 自检：SHINE_NOVEL_GRAPH_CHECK=1
    if (const char* raw = std::getenv("SHINE_NOVEL_GRAPH_CHECK"); raw != nullptr && *raw != '\0' &&
        std::string_view{raw} != "0") {
        const bool schemaOk = ::shine::novelcore::NovelDb::RunSchemaSelfCheck();
        const bool graphOk = ::shine::novelcore::NovelGraph::RunGraphSelfCheck();
        const bool ctxOk = agent::ContextBuilder::RunSelfCheck();
        const bool toolsOk = agent::RunToolsSelfCheck();
        const bool dirOk = agent::NovelDirector::RunSelfCheck();
        const bool mvpOk = app::novel::RunMvpSelfCheck();
        const bool chatOk = openai::RunChatSelfCheck();
        const bool sessOk = openai::RunChatSessionSelfCheck();
        const bool anthOk = openai::RunAnthropicSelfCheck();
        const bool visOk = ::shine::novelcore::NovelVisual::RunSelfCheck();
        const bool fieldsOk = ::shine::novelcore::NovelFields::RunSelfCheck();
        const bool agentsOk = agent::RunMultiAgentSelfCheck();
        const bool jsonOk = app::novel::RunJsonArrayParseSelfCheck();
        const bool novelMcpOk = ::shine::novelcore::RunNovelMcpSelfCheck();
        if (const char* p = std::getenv("SHINE_NOVEL_CHECK_OUT"); p && *p) {
            FILE* f = std::fopen(p, "ab");
            if (f) {
                const std::string line = fmt::format(
                    "chat:{}\nsess:{}\nanthropic:{}\nvisual:{}\nfields:{}\nagents:{}\njsonparse:{}\nnovelmcp:{}\n",
                    chatOk ? "ok" : "fail", sessOk ? "ok" : "fail", anthOk ? "ok" : "fail",
                    visOk ? "ok" : "fail", fieldsOk ? "ok" : "fail", agentsOk ? "ok" : "fail",
                    jsonOk ? "ok" : "fail", novelMcpOk ? "ok" : "fail");
                std::fwrite(line.data(), 1, line.size(), f);
                std::fclose(f);
            }
        }
        log::Info(
            "SHINE_NOVEL_GRAPH_CHECK：schema={} graph={} context={} tools={} director={} mvp={} chat={} sess={} anth={} visual={} fields={} agents={} json={} novelmcp={}",
            schemaOk ? "ok" : "fail", graphOk ? "ok" : "fail", ctxOk ? "ok" : "fail",
            toolsOk ? "ok" : "fail", dirOk ? "ok" : "fail", mvpOk ? "ok" : "fail",
            chatOk ? "ok" : "fail", sessOk ? "ok" : "fail", anthOk ? "ok" : "fail",
            visOk ? "ok" : "fail", fieldsOk ? "ok" : "fail", agentsOk ? "ok" : "fail",
            jsonOk ? "ok" : "fail", novelMcpOk ? "ok" : "fail");
        g_selfExitRequested = true;
    }
    // P7.1 自检：SHINE_MCP_CHECK=1 跑 MCP 注册表验收后自动退出（无网络）
    if (const char* raw = std::getenv("SHINE_MCP_CHECK"); raw != nullptr && *raw != '\0' &&
        std::string_view{raw} != "0") {
        const bool pass = ::shine::mcp::RunRegistrySelfCheck();
        log::Info("SHINE_MCP_CHECK：{}", pass ? "PASS" : "FAIL");
        g_selfExitRequested = true;
    }
    // P7.2 自检：SHINE_MCP_HTTP_CHECK=1 跑 HTTP Server 骨架验收（含本机 curl 自测）
    if (const char* raw = std::getenv("SHINE_MCP_HTTP_CHECK"); raw != nullptr && *raw != '\0' &&
        std::string_view{raw} != "0") {
        const bool pass = ::shine::mcp::RunHttpServerSelfCheck();
        log::Info("SHINE_MCP_HTTP_CHECK：{}", pass ? "PASS" : "FAIL");
        g_selfExitRequested = true;
    }
    // P7.3 自检：SHINE_MCP_PROTO_CHECK=1
    if (const char* raw = std::getenv("SHINE_MCP_PROTO_CHECK"); raw != nullptr && *raw != '\0' &&
        std::string_view{raw} != "0") {
        const bool pass = ::shine::mcp::RunMcpProtocolSelfCheck();
        log::Info("SHINE_MCP_PROTO_CHECK：{}", pass ? "PASS" : "FAIL");
        g_selfExitRequested = true;
    }
    // P5.7 自检：SHINE_SCENE_IMAGE_CHECK=1（离线，不依赖 SD 模型）
    if (const char* raw = std::getenv("SHINE_SCENE_IMAGE_CHECK"); raw != nullptr && *raw != '\0' &&
        std::string_view{raw} != "0") {
        const int fail = ::shine::video::RunSceneToImageSelfCheck();
        log::Info("SHINE_SCENE_IMAGE_CHECK：{}", fail == 0 ? "PASS" : "FAIL");
        g_selfExitRequested = true;
    }
    // 真实联调：SHINE_LLM_LIVE=1|json|tool（独立开关，需已配置 Key）
    if (const char* live = std::getenv("SHINE_LLM_LIVE"); live && *live && *live != '0') {
        const int mode = (std::string_view{live} == "json")   ? 1
                         : (std::string_view{live} == "tool") ? 2
                                                              : 0;
        auto r = openai::LivePing(mode);
        const char* outPath = std::getenv("SHINE_LLM_LIVE_OUT");
        const std::string reportPath =
            outPath && *outPath ? std::string{outPath}
                                : (SettingsPath() + ".llm-live.txt");
        FILE* lf = std::fopen(reportPath.c_str(), "wb");
        if (r) {
            log::Info("SHINE_LLM_LIVE PASS：{}",
                      r->substr(0, std::min<std::size_t>(r->size(), 160)));
            if (lf) {
                const std::string msg = "PASS\n" + r->substr(0, 400) + "\n";
                std::fwrite(msg.data(), 1, msg.size(), lf);
            }
        } else {
            log::Error("SHINE_LLM_LIVE FAIL：{} {}", r.error().code, r.error().message);
            if (lf) {
                const std::string msg =
                    "FAIL\n" + r.error().code + "\n" + r.error().message + "\n";
                std::fwrite(msg.data(), 1, msg.size(), lf);
            }
        }
        if (lf) std::fclose(lf);
        g_selfExitRequested = true;
    }
    theme::ApplyPresetById(Settings().themeId);
    theme::ApplyOverrides(Settings().themeCustomColors); // R-S7：预设之后再叠加用户自定义配色
    comfy::ComfySession::Instance().Init(Settings().comfyBaseUrl);
    graph::Init();
    ::shine::gallery::Init(); // G-S5：图库（读上次来源 + 目录可用就自动扫一次）
    ::shine::gallery::RegisterBuiltinDecoders();     // G-S2：注册内置解码器（PNG）
    ::shine::mcp::RegisterAllModules(::shine::mcp::ToolRegistry::Instance()); // P7.1：MCP 工具注册地基
    // P7.2：MCP HTTP Server（Settings.mcpEnabled 时监听；默认 127.0.0.1:8931）
    if (auto r = ::shine::mcp::StartHttpFromSettings(); !r) {
        log::Error("mcp HTTP 启动失败：{}", r.error());
    }
    media::MediaLibrary::Instance().Init(); // P4.2：订阅 WS 预览帧 + 建缓存目录
    media::MediaLibrary::Instance().Refresh(100);
    log::Info("ShineTV Studio 已初始化（P2 GraphHost）");
    log::Info("主题：{} | Comfy：{}", Settings().themeId, Settings().comfyBaseUrl);
    return true;
}

void Shutdown() {
    ::shine::mcp::StopHttpFromSettings(); // P7.2：先停 MCP HTTP（P8 顺序：mcp 最先）
    media::MediaLibrary::Instance().Shutdown();
    gpu::TextureCache().Clear();  // 纹理必须先于设备销毁（P4.1 S8：退出无 D3D11 Live Object 报错）
    gpu::Textures().ReleaseAll();
    log::Info("GPU 纹理已释放：剩余 {} 张 / {} 字节", gpu::Textures().TextureCount(), gpu::Textures().UsedBytes());
    graph::Shutdown();
    comfy::ComfySession::Instance().Shutdown();
    SaveSettings();
    ::shine::gallery::Shutdown(); // G-S5：图库收尾（排在 SaveSettings() 之后、async::Shutdown() 之前）
    async::Shutdown();
    // 不调用 `log::Shutdown()`：`async::Shutdown()` 已不再 join worker（见其注释），
    // 可能仍有 worker 在写日志；日志器保持存活直到进程退出，避免被后台线程踩到已析构对象。
}

void DrawFrame() {
    static double lastSec = 0.0;
    const double now = ImGui::GetTime();
    const float dt = lastSec > 0.0 ? static_cast<float>(now - lastSec) : 0.016f;
    lastSec = now;
    comfy::ComfySession::Instance().Tick(dt);
    async::DrainUiQueue();
    shots::Tick();      // P5.3：分镜模块每帧（执行延迟命令 + 取队列快照）
    ::shine::gallery::Tick(); // G-S5：图库每帧（**必须有**：G-S7 的缩略图落地/淘汰都挂在这里）

    // 全局快捷键
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        State().sideOpen = !State().sideOpen;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        graph::SaveGraph();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !io.WantTextInput) {
        graph::RunCurrentGraph(); // P3.5：运行当前图（控件聚焦时不触发）
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false) && !io.WantTextInput) {
        gallery::PickAndScanLocalFolder(); // G-S5：打开图片文件夹（app::gallery，含弹框+RequestScan）
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;
    ImGui::Begin("ShineTVHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    // ① 顶栏菜单
    DrawMenuBar();

    // ⑦ 状态栏高度从工作区扣掉
    constexpr float kStatusBarH = 24.f;
    const ImVec2 content = ImGui::GetContentRegionAvail();
    const float workH = std::max(80.f, content.y - kStatusBarH);

    // ② 活动栏 + ③–⑥ Dock
    DrawActivityBar(workH);
    ImGui::SameLine(0, 0);
    const ImVec2 dockSize(std::max(1.f, content.x - kActivityBarWidth), workH);
    DrawDockedPanels(dockSize);

    // ⑦ 状态栏（整行）
    ImGui::SetCursorPosX(0.f);
    DrawStatusBar(content.x, kStatusBarH);

    ImGui::End();
}

} // namespace shine::app
