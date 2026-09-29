#include "ui/imgui/host/Host.h"

#include "core/Async.h"
#include "core/Log.h"
#include "gpu/GpuDevice.h"
#include "media/Gallery.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/theme/Theme.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <dxgi.h>
#include <tchar.h>

#include <algorithm>
#include <cmath>

// imgui_impl_win32.h 把这一行放在 #if 0 里（它不想拖 <windows.h> 进来），
// 官方要求调用方自己抄一份到 .cpp。照做，别改成包含头里的声明。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace shine::imguiapp {
namespace {

constexpr wchar_t kWindowClass[] = L"ShineTVStudioImguiHost";

// 帧循环的 UI 队列泵。Qt 版是 QTimer(15ms)，这里每帧调一次 ——
// 延迟从「最坏 15ms」降到「一帧」，只更好，不会更差。
void PumpUi() {
    shine::async::DrainUiQueue();
    shine::gallery::Tick();
}

} // namespace

Host::~Host() { Shutdown(); }

float Host::DisplayScaledFactor() {
    // 优先用主显示器 DPI；拿不到退回 1.0（96 DPI）。
    UINT dpi = GetDpiForSystem();
    if (dpi == 0) {
        dpi = 96;
    }
    return static_cast<float>(dpi) / 96.0f;
}

LRESULT CALLBACK Host::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) {
        return 1;
    }

    auto* host = reinterpret_cast<Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (host == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    switch (message) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            host->resizeRequested_ = true;
            host->resizeWidth_ = static_cast<UINT>(LOWORD(lparam));
            host->resizeHeight_ = static_cast<UINT>(HIWORD(lparam));
        }
        return 0;
    case WM_SYSCOMMAND:
        // 拦最小化：最小化后 D3D11 交换链拿不到后备缓冲，ImGui 会在恢复时黑一帧。
        if ((wparam & 0xFFF0) == SC_MINIMIZE) {
            return 0;
        }
        break;
    case WM_DESTROY:
        host->quit_ = true;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool Host::Initialize(const Options& options, std::string& error) {
    // DPI 感知必须在建窗口之前设，否则窗口先按系统 DPI 建出来再放大会有一次闪动。
    // 1:1 对齐指的是 100% 缩放下与 webui 一致，>100% 按 DisplayScaledFactor 换算。
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = &Host::WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;
    if (RegisterClassExW(&wc) == 0) {
        error = "RegisterClassExW failed";
        return false;
    }

    RECT rect{0, 0, options.width, options.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, kWindowClass, options.title, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                            rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, this);
    if (hwnd_ == nullptr) {
        error = "CreateWindowExW failed";
        return false;
    }

    if (!CreateDeviceD3D(hwnd_, error)) {
        return false;
    }

    // ---- ImGui 上下文 ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // 布局持久化走自己的 layout.dat（P1.5），不用 imgui.ini
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    RECT client{};
    GetClientRect(hwnd_, &client);
    io.DisplaySize = ImVec2(static_cast<float>(client.right),
                            static_cast<float>(client.bottom));
    // 高 DPI 下 ImGui 用逻辑像素画、由后端按比例放大，避免每个控件手写缩放。
    const float scale = DisplayScaledFactor();
    io.DisplayFramebufferScale = ImVec2(scale, scale);

    // ---- P2.5：字体图集 ----
    // ⚠️ 顺序硬约束：必须在 ImGui_ImplDX11_Init **之后**。1.92+ 的渲染后端
    // 通过 ImGuiBackendFlags_RendererHasTextures 声明自己会按需烘焙并上传纹理，
    // 在那之前调 atlas->Build() 会每帧刷 "Called ImFontAtlas::Build() before
    // ImGuiBackendFlags_RendererHasTextures got set!"。
    // 不建图集就是满屏豆腐块且不报错，所以失败必须走日志。
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    if (!kit::BuildFontAtlas(/*serif=*/theme::ThemeUsesSerif(theme::CurrentThemeId()))) {
        shine::log::Error("font atlas build failed — text may render as tofu");
    }

    theme::ApplyCurrentTheme();

    imguiReady_ = true;

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    // ---- P1.2：补上这一行。shine::gpu 自此有宿主 ----
    shine::gpu::AttachDevice(device_, context_);

    shine::log::Info("imgui host up: {}x{} client, dpi scale {:.2f}, gpu={}", client.right,
                     client.bottom, static_cast<double>(scale),
                     shine::gpu::Ready() ? "attached" : "MISSING");
    return true;
}

bool Host::CreateDeviceD3D(HWND window, std::string& error) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    // DXGI_SWAP_EFFECT_DISCARD：与 ImGui 的 dx11 后端配套（全屏才用 FLIP_SEQUENTIAL）。
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                               static_cast<UINT>(std::size(levels)),
                                               D3D11_SDK_VERSION, &desc, &swapChain_, &device_,
                                               &got, &context_);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        // 远程桌面 / 无 GPU 的机器上硬件驱动不可用，退回 WARP 软件渲染。
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
                                           static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                           &desc, &swapChain_, &device_, &got, &context_);
    }
    if (FAILED(hr)) {
        char buffer[64]{};
        snprintf(buffer, sizeof(buffer), "D3D11CreateDeviceAndSwapChain failed 0x%08lX",
                 static_cast<unsigned long>(hr));
        error = buffer;
        return false;
    }

    auto* chain = swapChain_;
    chain->GetBuffer(0, IID_PPV_ARGS(&renderTarget_));
    CreateRenderTarget();
    return true;
}

void Host::CreateRenderTarget() {
    if (renderTarget_ != nullptr) {
        return;
    }
    auto* chain = swapChain_;

    ID3D11Texture2D* backBuffer = nullptr;
    chain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer != nullptr) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
        backBuffer->Release();
    }

    // 深度模板缓冲：ImGui 的 dx11 后端会启用深度测试自己画，不提供时
    // CreateDepthStencilView 拿不到可绑定的 D3D11_DEPTH_STENCIL。
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 0;
    depthDesc.Height = 0;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(device_->CreateTexture2D(&depthDesc, nullptr, &depthBuffer_)) &&
        depthBuffer_ != nullptr) {
        device_->CreateDepthStencilView(depthBuffer_, nullptr, &depthStencil_);
    }
}

void Host::CleanupRenderTarget() {
    if (depthStencil_ != nullptr) {
        depthStencil_->Release();
        depthStencil_ = nullptr;
    }
    if (depthBuffer_ != nullptr) {
        depthBuffer_->Release();
        depthBuffer_ = nullptr;
    }
    if (renderTarget_ != nullptr) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
}

void Host::PumpFrames(int frames, const DrawFrameFn& onFrame) {
    if (!imguiReady_) {
        return;
    }
    for (int i = 0; i < frames; ++i) {
        PumpUi();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (onFrame) {
            onFrame(1.0f / 60.0f);
        }
        ImGui::Render();

        const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->OMSetRenderTargets(1, &renderTarget_, depthStencil_);
        context_->ClearRenderTargetView(renderTarget_, clear);
        context_->ClearDepthStencilView(depthStencil_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                        1.0f, 0);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    // 后端此刻已把字体纹理烘焙上传，打一次显存日志（64 MB 硬判据）。
    kit::LogFontAtlasIfNeeded();
}

void Host::RunLoop(const DrawFrameFn& onFrame) {
    bool done = false;
    while (!done) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) {
                done = true;
                break;
            }
        }
        if (done) {
            break;
        }

        if (resizeRequested_) {
            CleanupRenderTarget();
            if (swapChain_ != nullptr) {
                swapChain_->ResizeBuffers(0, resizeWidth_, resizeHeight_, DXGI_FORMAT_UNKNOWN, 0);
            }
            CreateRenderTarget();
            resizeRequested_ = false;
        }

        if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
            break;
        }

        PumpFrames(1, onFrame);
        swapChain_->Present(1, 0); // 垂直同步
    }
}

std::vector<std::uint8_t> Host::CaptureBackBuffer() {
    std::vector<std::uint8_t> pixels;
    if (device_ == nullptr || context_ == nullptr || renderTarget_ == nullptr) {
        return pixels;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return pixels;
    }

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    // 抓图走自己的 staging 纹理：D3D11 默认后端缓冲不可 MAP，且可能是 MSAA。
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, &staging)) || staging == nullptr) {
        backBuffer->Release();
        return pixels;
    }
    context_->CopyResource(staging, backBuffer);
    backBuffer->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return pixels;
    }

    // 逐行拷贝：后备缓冲行距（D3D11_TEXTURE2D_DESC.Width * 4）常大于图库要的紧密行距。
    const int width = static_cast<int>(desc.Width);
    const int height = static_cast<int>(desc.Height);
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y) {
        std::copy_n(source + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * 4u,
                    pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u);
    }
    context_->Unmap(staging, 0);
    staging->Release();
    return pixels;
}

void Host::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (swapChain_ != nullptr) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    // device_/context_ 由交换链持有，交换链放掉即可；这里只清我们的裸指针。
    device_ = nullptr;
    context_ = nullptr;
}

void Host::Shutdown() {
    if (imguiReady_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    // ⚠️ 顺序：先把 gpu 的纹理放掉，再放设备（GpuDevice.h 的契约）。
    if (shine::gpu::Ready()) {
        shine::gpu::DetachDevice();
    }
    CleanupDeviceD3D();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

} // namespace shine::imguiapp
