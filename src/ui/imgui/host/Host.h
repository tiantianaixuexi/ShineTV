#pragma once
// shine::imguiapp::Host —— Win32 窗口 + D3D11 设备/交换链 + ImGui 帧循环
//
// 这是 refactor/phases.md P0.4 与 P1.1–P1.3、P1.6 的落点：Qt 版 AppEntry.cpp 的
// QApplication + QTimer(15ms) 整条链，在此平移成 Win32 消息循环 + 每帧泵。
//
// 关键动作（P1.2）：gpu::AttachDevice(device, context)。
// 那是 shine_core 里空了很久的注入点 —— src/media 的纹理上传链
// （Textures().Upload → TextureCache().Insert）自此有宿主，页面才能显示图片。
// GetDpiForSystem / GetDpiForWindow 需要 _WIN32_WINNT >= 0x0A00。默认的
// SDK 宏停在 0x0601，必须在 windows.h 之前抬上去。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Texture2D;

namespace shine::imguiapp {

// 一帧的业务绘制回调。dt 为秒。
using DrawFrameFn = std::function<void(float dt)>;

class Host {
public:
    struct Options {
        int width = 1600;
        int height = 960;
        const wchar_t* title = L"ShineTV Studio";
    };

    Host() = default;
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // 建窗口 + 设备 + 交换链 + ImGui 上下文。任何一步失败都返回 false 并填 error。
    [[nodiscard]] bool Initialize(const Options& options, std::string& error);

    // 跑消息循环，直到 WM_QUIT。onFrame 每帧调用一次。
    void RunLoop(const DrawFrameFn& onFrame);

    void Shutdown();

    // ---- 抓图（P6.1）：从 D3D11 后备缓冲拷像素 ----
    // 返回 BGRA8 像素（宽 * 高 * 4），调用方负责编码 PNG。设备不可用时返回空。
    [[nodiscard]] std::vector<std::uint8_t> CaptureBackBuffer();

    // 推进 N 帧而不进入消息循环（取证用：等异步解码回调投递到 UI 队列）
    void PumpFrames(int frames, const DrawFrameFn& onFrame);

    [[nodiscard]] HWND window() const noexcept { return hwnd_; }
    [[nodiscard]] ID3D11Device* device() const noexcept { return device_; }
    [[nodiscard]] ID3D11DeviceContext* context() const noexcept { return context_; }

    // 显示器缩放系数（96 DPI = 1.0）。面板宽高按它换算，1:1 指的是 100% 缩放下。
    [[nodiscard]] static float DisplayScaledFactor();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool CreateDeviceD3D(HWND window, std::string& error);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;               // 借用，非 COM 引用
    ID3D11DeviceContext* context_ = nullptr;        // 借用
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
    ID3D11DepthStencilView* depthStencil_ = nullptr;
    ID3D11Texture2D* depthBuffer_ = nullptr;
    bool resizeRequested_ = false;
    bool imguiReady_ = false;
    UINT resizeWidth_ = 0;
    UINT resizeHeight_ = 0;
    bool quit_ = false;
};

} // namespace shine::imguiapp
