#pragma once
// shine::imguiapp::Host —— Win32 窗口 + WGL/GL3 + ImGui 帧循环
//
// 这是 refactor/phases.md P0.4 与 P1.1–P1.3、P1.6 的落点：Qt 版 AppEntry.cpp 的
// QApplication + QTimer(15ms) 整条链，在此平移成 Win32 消息循环 + 每帧泵。
//
// 渲染后端选 **OpenGL 3.3 core（WGL）**，不用 D3D11：
//   * ImGui 的 gl3 后端自带 imgl3w 加载器与着色器编译，宿主不用碰任何图形 API；
//   * 抓图直接 glReadPixels(GL_RGBA)，没有交换链、没有 staging 纹理，
//     也不存在「后备缓冲到底是 BGRA 还是 RGBA」这种坑；
//   * 顶点色按 GL_UNSIGNED_BYTE 归一化直出，ImGui 侧不涉及通道字节序。
//
// 关键动作（P1.2）：gpu::AttachDevice(GlApi)。那是 shine_core 里空了很久的
// 纹理注入点 —— src/media 的上传链（Textures().Upload → TextureCache().Insert）
// 自此有宿主，页面才能显示图片。
// GetDpiForSystem / GetDpiForWindow 需要 _WIN32_WINNT >= 0x0A00。默认的
// SDK 宏停在 0x0601，必须在 windows.h 之前抬上去。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

    // 建窗口 + 像素格式 + GL 上下文 + ImGui 上下文。任何一步失败都返回 false 并填 error。
    [[nodiscard]] bool Initialize(const Options& options, std::string& error);

    // 跑消息循环，直到 WM_QUIT。onFrame 每帧调用一次。
    void RunLoop(const DrawFrameFn& onFrame);

    void Shutdown();

    // ---- 抓图（P6.1）：glReadPixels 直接读后备缓冲 ----
    // 返回 **RGBA8** 像素（宽 * 高 * 4），调用方直接交给 SavePng。
    // GL 的 glReadPixels 就是 RGBA，没有 BGRA 那一层歧义。
    [[nodiscard]] std::vector<std::uint8_t> CaptureBackBuffer();

    // 推进 N 帧而不进入消息循环（取证用：等异步解码回调投递到 UI 队列）
    void PumpFrames(int frames, const DrawFrameFn& onFrame);

    [[nodiscard]] HWND window() const noexcept { return hwnd_; }
    [[nodiscard]] HGLRC glContext() const noexcept { return glContext_; }

    // 显示器缩放系数（96 DPI = 1.0）。面板宽高按它换算，1:1 指的是 100% 缩放下。
    [[nodiscard]] static float DisplayScaledFactor();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool CreateGlContext(HWND window, std::string& error);
    void CleanupGlContext();
    void SetupViewport();

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;          // 窗口的设备上下文，WGL 绑定在它上面
    HGLRC glContext_ = nullptr;  // GL 上下文
    bool imguiReady_ = false;
    bool quit_ = false;
};

} // namespace shine::imguiapp
