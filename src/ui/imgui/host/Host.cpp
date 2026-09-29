#include "ui/imgui/host/Host.h"

#include "core/Async.h"
#include "core/Log.h"
#include "gpu/GpuDevice.h"
#include "media/Gallery.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/theme/Theme.h"

#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

// 系统 GL 头在这里是安全的：imgui_impl_opengl3.h 只包 imgui.h，不拖 imgl3w 加载器
// （加载器只在后端 .cpp 里）。imgl3w 全是 static 函数指针，链接期不会和我们抢符号。
// 有了真头就不用手抄 GL 1.1 原型、也不用把位掩码常量硬写成十六进制 ——
// 后者会被 tools/check-layers.ps1 的硬编码颜色规则当色值拦下。
// 只有 imgui_impl_opengl3.cpp 自己那份编译单元不许碰 <GL/gl.h>。
#include <GL/gl.h>
#include <GL/glext.h> // GL_MULTISAMPLE 是 GL 1.3 的枚举，MinGW 的 gl.h 里没有

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

// ImGui 后端用的是自己的 imgl3w 加载器，拿不到它的函数指针表；
// src/gpu 需要的 6 个 GL 1.1 函数在这里按同样的「wglGetProcAddress 优先、
// opengl32 导出兜底」解析一次交给它。两套入口互不干扰。
using ProcGen = void (*)(int, unsigned int*);
using ProcDelete = void (*)(int, const unsigned int*);
using ProcBind = void (*)(unsigned int, unsigned int);
using ProcTexImage = void (*)(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
using ProcTexParam = void (*)(unsigned int, unsigned int, int);
using ProcPixelStore = void (*)(unsigned int, int);

template <typename Fn>
Fn ResolveGl(const char* name) {
    // wglGetProcAddress 只保证 GL 扩展 / GL 2.0+ 的函数；GL 1.1 的可能返回 NULL，
    // 所以必须再问一次 opengl32 的导出表。
    if (auto proc = reinterpret_cast<Fn>(wglGetProcAddress(name)); proc != nullptr) {
        return proc;
    }
    static HMODULE opengl32 = LoadLibraryW(L"opengl32.dll");
    if (opengl32 == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(GetProcAddress(opengl32, name));
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
            // WGL 上下文不随窗口尺寸变化，viewport 每帧按客户区重设即可，
            // 不用重建任何缓冲 —— 这是选 GL 相对 D3D11 少一整套 ResizeBuffers 的地方。
        }
        return 0;
    case WM_SYSCOMMAND:
        // 拦最小化：最小化后客户区为 0，glViewport 会拿到 0×0 并触发 GL 错误刷屏。
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

    if (!CreateGlContext(hwnd_, error)) {
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
    // framebuffer scale 固定 1:1，**不**按显示器 DPI 放大。
    //
    // ImGui 的契约：渲染后端把 ImGui 坐标乘以 DisplayFramebufferScale 落到像素上。
    // 若这里填 1.25，则 DisplaySize 也必须除以 1.25 变回逻辑单位，整个界面被放大 1.25 倍，
    // 顶栏变成 57.5px，右侧和底部各丢掉 20% 内容 —— 那不是 1:1。
    //
    // design-spec §1 的几何表全部是「100% 缩放下的 px」，1:1 验收就是拿 1600x960 的抓图
    // 去和 webui 100% 的截图比。所以按物理像素 1:1 画：46px 顶栏就是 46 个物理像素。
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // ---- P2.5：字体图集 ----
    // ⚠️ 顺序硬约束：必须在 ImGui_ImplOpenGL3_Init **之后**。1.92+ 的渲染后端
    // 通过 ImGuiBackendFlags_RendererHasTextures 声明自己会按需烘焙并上传纹理，
    // 在那之前调 atlas->Build() 会每帧刷 "Called ImFontAtlas::Build() before
    // ImGuiBackendFlags_RendererHasTextures got set!"。
    // 不建图集就是满屏豆腐块且不报错，所以失败必须走日志。
    ImGui_ImplWin32_Init(hwnd_);
    // GLSL 150 = GL 3.2 core，Win10 的默认 GDI 通用实现稳定支持到这一档。
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
        error = "ImGui_ImplOpenGL3_Init failed";
        return false;
    }
    if (!kit::BuildFontAtlas(/*serif=*/theme::ThemeUsesSerif(theme::CurrentThemeId()))) {
        shine::log::Error("font atlas build failed — text may render as tofu");
    }

    theme::ApplyCurrentTheme();

    imguiReady_ = true;

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    // ---- P1.2：补上这一行。shine::gpu 自此有宿主 ----
    {
        shine::gpu::GlApi api;
        api.genTextures = ResolveGl<ProcGen>("glGenTextures");
        api.deleteTextures = ResolveGl<ProcDelete>("glDeleteTextures");
        api.bindTexture = ResolveGl<ProcBind>("glBindTexture");
        api.texImage2D = ResolveGl<ProcTexImage>("glTexImage2D");
        api.texParameteri = ResolveGl<ProcTexParam>("glTexParameteri");
        api.pixelStorei = ResolveGl<ProcPixelStore>("glPixelStorei");
        shine::gpu::AttachDevice(api);
        if (!shine::gpu::Ready()) {
            shine::log::Error("gpu::AttachDevice 缺函数 —— 图库/媒体预览会不出图");
        }
    }

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    shine::log::Info("imgui host up: {}x{} client, framebuffer 1:1 (system dpi {:.2f}), gpu={}",
                     client.right, client.bottom, static_cast<double>(DisplayScaledFactor()),
                     shine::gpu::Ready() ? "attached" : "MISSING");
    shine::log::Info("GL vendor={} renderer={} version={}", vendor ? vendor : "?",
                     renderer ? renderer : "?", version ? version : "?");
    return true;
}

bool Host::CreateGlContext(HWND window, std::string& error) {
    hdc_ = GetDC(window);
    if (hdc_ == nullptr) {
        error = "GetDC failed";
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(hdc_, &pfd);
    if (format == 0) {
        error = "ChoosePixelFormat failed（系统可能没有可用的 OpenGL 像素格式）";
        return false;
    }
    if (SetPixelFormat(hdc_, format, &pfd) == FALSE) {
        error = "SetPixelFormat failed";
        return false;
    }

    glContext_ = wglCreateContext(hdc_);
    if (glContext_ == nullptr) {
        error = "wglCreateContext failed";
        return false;
    }
    if (wglMakeCurrent(hdc_, glContext_) == FALSE) {
        error = "wglMakeCurrent failed";
        return false;
    }

    // 关掉 MSAA：默认帧缓冲多重采样会让 glReadPixels 拿到的图边缘发虚，
    // 逐像素比对时是纯噪声。ImGui 是 2D 三角形，本来也用不上。
    glDisable(GL_MULTISAMPLE);
    SetupViewport();
    return true;
}

void Host::SetupViewport() {
    if (hdc_ == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    glViewport(0, 0, client.right, client.bottom);
}

void Host::CleanupGlContext() {
    if (glContext_ != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(glContext_);
        glContext_ = nullptr;
    }
    if (hdc_ != nullptr && hwnd_ != nullptr) {
        ReleaseDC(hwnd_, hdc_);
    }
    hdc_ = nullptr;
}

void Host::PumpFrames(int frames, const DrawFrameFn& onFrame) {
    if (!imguiReady_ || glContext_ == nullptr) {
        return;
    }
    for (int i = 0; i < frames; ++i) {
        PumpUi();

        // 客户区可能变（拖边框、还原最小化）。WGL 上下文不用重建，viewport 跟一下即可。
        RECT client{};
        GetClientRect(hwnd_, &client);
        ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(client.right),
                                           static_cast<float>(client.bottom));
        glViewport(0, 0, client.right, client.bottom);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (onFrame) {
            onFrame(1.0f / 60.0f);
        }
        ImGui::Render();

        // ⚠️ 必须先关掉剪裁测试再清屏。
        // ImGui 的 GL3 后端每个 draw call 都会 glEnable(GL_SCISSOR_TEST) + glScissor(该
        // 元素的 clip rect)，帧结束时它保持开启。直接 glClear 只会清掉**最后一个 clip
        // 矩形**内的像素，其余全是上一帧的残留 —— 表现为布局切换后上一屏的碎片
        // 混在新内容里（实测在组件画廊的进度条上表现为彩色细条纹）。
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
        if (done || quit_) {
            break;
        }

        if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
            break;
        }

        PumpFrames(1, onFrame);
        SwapBuffers(hdc_); // 垂直同步
    }
}

std::vector<std::uint8_t> Host::CaptureBackBuffer() {
    std::vector<std::uint8_t> pixels;
    if (glContext_ == nullptr || hdc_ == nullptr) {
        return pixels;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        return pixels;
    }

    // glReadPixels 的原点在左下角，PNG 从左上角起 —— 逐行倒着写。
    // 顺带把剪裁测试也限制住，避免遗留的 scissor 矩形把读取范围裁掉。
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width, height);
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glDisable(GL_SCISSOR_TEST);

    if (const GLenum err = glGetError(); err != GL_NO_ERROR) {
        shine::log::Error("capture: glReadPixels GL error 0x{:X}", static_cast<unsigned>(err));
    }

    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    std::vector<std::uint8_t> flipped(pixels.size());
    for (int y = 0; y < height; ++y) {
        const std::size_t src = static_cast<std::size_t>(height - 1 - y) * stride;
        std::copy_n(pixels.data() + src, stride, flipped.data() + static_cast<std::size_t>(y) * stride);
    }
    return flipped;
}

void Host::Shutdown() {
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    // ⚠️ 顺序：先把 gpu 的纹理 glDeleteTextures 放掉，再销毁 GL 上下文
    // （GpuDevice.h 的契约）。反过来就是往已死上下文里发命令。
    if (shine::gpu::Ready()) {
        shine::gpu::DetachDevice();
    }
    CleanupGlContext();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

} // namespace shine::imguiapp
