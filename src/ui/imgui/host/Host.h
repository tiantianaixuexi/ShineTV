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

    // 取证用：把鼠标位置**在 `NewFrame` 之前**钉住，跨 frames 帧有效。
    //
    // ⚠️ 为什么不能靠在 onFrame 里写 `ImGui::GetIO().MousePos`：那样只是改了
    //    `IsMouseHoveringRect` 用的那个值，而 `g.HoveredId` 早在此前的 `NewFrame`
    //    里按**真实光标**算好了 —— 于是恰好有 1 个 item（真实光标所在那个）在每一帧
    //    都报 hovered。实测后果：静息帧的「命中数」恒为 1，悬停探针的诊断信号全是噪声。
    //    注入必须发生在 `ImGui_ImplWin32_NewFrame` 之前，让 `NewFrame` 自己算出
    //    `g.HoveredId`。传 (-FLT_MAX, -FLT_MAX) 之外的位置即模拟真实悬停。
    //
    // 用两个 float 而不是 ImVec2：Host.h 刻意不引 imgui 头（宿主不该依赖绘制层），
    // 那个头会把 ImVec2 拖进这个 TU 的每个翻译单元。
    void SetFrameMouseOverride(float x, float y) {
        mouseOverrideX_ = x;
        mouseOverrideY_ = y;
        mouseOverrideSet_ = true;
    }
    void ClearFrameMouseOverride() { mouseOverrideSet_ = false; }

    // 取证用：把一次按键**在 `NewFrame` 之前**注入，跨 frames 帧有效。
    //
    // 为什么必须提前：`ImGui::IsKeyPressed` 读的是 `ImGui::NewFrame()` 里推进的按下沿
    // 队列。在帧回调里注入（NewFrame 之后）就永远晚一帧，判定等于没接线。
    // 跟 SetFrameMouseOverride 同一个道理。
    //
    // 只支持无修饰键与 LeftCtrl：快捷键判据要验的是「按了会发生什么」，
    // 而 Ctrl+X 能不能触发取决于「Ctrl 有没有被算进 KeyCtrl」，所以 Ctrl 要能注入。
    // key 用 ImGuiKey 的**整数**值而不是 ImGuiKey —— 同样是别把 imgui 头拖进这个 TU。
    void SetFrameKeyOverride(int key, bool down) {
        keyOverride_ = key;
        keyOverrideDown_ = down;
        keyOverrideSet_ = true;
    }
    void ClearFrameKeyOverride() { keyOverrideSet_ = false; }
    // 辅助：把 ImGuiKey_LeftCtrl 之类换成本 TU 能用的整数（调用方自己 include imgui.h）。
    void SetFrameCtrlOverride(bool down) { ctrlOverride_ = down; }
    void ClearFrameCtrlOverride() { ctrlOverride_ = false; }

    // 鼠标左键的按下 / 抬起覆盖。**光有位置注入是测不出「点不动」的** ——
    // 悬停探针读的是 hovered，而 `IsItemClicked()` 还要 MouseDown 的按下沿。
    // 判据里的用法：位置设好之后先注入 down 一帧、再注入 up 一帧。
    void SetFrameMouseButtonOverride(bool down) {
        mouseButtonOverrideDown_ = down;
        mouseButtonOverrideSet_ = true;
    }
    void ClearFrameMouseButtonOverride() { mouseButtonOverrideSet_ = false; }

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
    float mouseOverrideX_ = 0.0f;
    float mouseOverrideY_ = 0.0f;
    bool mouseOverrideSet_ = false;
    int keyOverride_ = 0;
    bool keyOverrideDown_ = false;
    bool keyOverrideSet_ = false;
    bool ctrlOverride_ = false;    HGLRC glContext_ = nullptr;  // GL 上下文
    // 鼠标左键覆盖（见 SetFrameMouseButtonOverride 的说明）
    bool mouseButtonOverrideDown_ = false;
    bool mouseButtonOverrideSet_ = false;
    bool imguiReady_ = false;
    bool quit_ = false;
};

} // namespace shine::imguiapp
