// ShineTV Studio — Win32 + DX11 + ImGui Docking shell
#include "app/App.h"
#include "app/Fonts.h"
#include "app/novel/NovelCli.h"
#include "core/Log.h"
#include "gpu/GpuDevice.h"
#include "mcp/MCPServer.h"

#include <mimalloc.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <d3d11.h>
#include <tchar.h>

#include <cstdio>
#include <cstdlib> // std::_Exit（退出时跳过 CRT atexit，见文件末尾说明）

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = LOWORD(lParam);
        g_ResizeHeight = HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // mimalloc: take over allocations as early as possible.
    mi_process_init();

    // P10.1：stdio MCP（Claude Desktop 等）—— 不建窗口/D3D，stdout 只走 JSON-RPC
    if (lpCmdLine != nullptr &&
        (wcsstr(lpCmdLine, L"--mcp-stdio") != nullptr || wcsstr(lpCmdLine, L"/mcp-stdio") != nullptr)) {
        return static_cast<int>(shine::mcp::RunStdioServerMain());
    }

    // S17：headless 命令行（脚本 / 无人值守）—— 同样不建窗口。
    //   `--novel-generate <chapter_id>` 生成一章（0 = 第一张未完成的章）
    //   `--novel-run <manual|semi|auto>` 连跑（`09` §2.1）
    // 与 UI 走**同一份** `NovelPipeline`（同一份 LLM 回调与前置判定）。
    if (lpCmdLine != nullptr &&
        (wcsstr(lpCmdLine, L"--novel-generate") != nullptr ||
         wcsstr(lpCmdLine, L"--novel-run") != nullptr)) {
        return shine::app::novel::RunNovelCli(lpCmdLine);
    }

    // Alloc console for spdlog stdout sink
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr,
                      nullptr, nullptr, L"ShineTVStudio", nullptr};
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ShineTV Studio", WS_OVERLAPPEDWINDOW,
                                100, 100, 1600, 900, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.f;
        style.Colors[ImGuiCol_WindowBg].w = 1.f;
    }

    // Chinese UI font (rule: msyh.ttc + Chinese Simplified Common ranges)
    shine::app::LoadUIFonts(18.f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    // P4.1 S1：把 DX11 设备交给共享 GPU 层（必须在 app::Init() 之前、DX11 初始化之后）
    shine::gpu::AttachDevice(g_pd3dDevice, g_pd3dDeviceContext);

    shine::app::Init();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done || shine::app::ShouldExit()) break; // 自检模式：SHINE_EXIT_AFTER_SEC 到时正常退出

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        shine::app::DrawFrame();

        ImGui::Render();
        const float clear[4] = {0.04f, 0.05f, 0.06f, 1.f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        const HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    shine::app::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    shine::gpu::DetachDevice(); // 纹理必须先于设备销毁（P4.1：退出无 D3D11 Live Object 报错）
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    // 上面的清理**都已显式做完**，这里用 `std::_Exit` 直接结束进程 —— **故意跳过 CRT 的 atexit /
    // 静态析构**。原因（2026-09-17 实测，见 `async::Shutdown()` 与 `comfy/ComfyHttp.h` 注释）：
    // 后台 worker 可能还停在 libhv 的 HTTP 里（连接超时 10s 级），而 libhv 的默认 logger 通过
    // `atexit(hv_destroy_default_logger)`（`third/libhv/base/hlog.c:537`）注册了清理；worker 若在
    // 进程退出期间首次/再次走到那里，就会和 msvcrt `_onexit` 的锁互相等待，**进程永远不消失**
    // （就是用户看到的"窗口关了但进程还在 / 关不掉"）。
    std::_Exit(0);
}
