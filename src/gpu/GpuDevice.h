#pragma once
// shine::gpu::GpuDevice —— OpenGL 纹理后端注入点
//
// 谁先需要谁建：媒体预览/纹理与图片库共用这一份。注入时机：GL 上下文
// make-current 之后、任何 GPU 资源使用之前。
//
// ⚠️ src/gpu 是 shine_core 里**唯一**知道图形后端的地方，且有两条硬约束：
//   1. 不能 include ImGui 头 —— tools/check-layers.ps1 规则 2 的门禁。
//      所以这里**不**用 ImGui 的 imgl3w 加载器，也**不**用 <GL/gl.h>
//      （会和 ImGui 后端的加载器在同一 TU 里打架）。
//   2. 只需要 6 个函数，全是 **GL 1.1**，opengl32.dll 直接导出 → 自己声明、
//      直接链接即可，不需要 GetProcAddress，也不需要当前上下文。
//   真正的 GL 3.3 能力（着色器、VAO、UBO）只有 ImGui 后端用得到，由它自己的
//   加载器负责。两套入口互不干扰：这里是 opengl32 的导出符号，后端是函数指针。
//
// 纹理创建/删除要求**调用线程持有当前 GL 上下文**（UI 线程，宿主保证）。
#pragma once

namespace shine::gpu {

// 建立/销毁 GL 纹理所需的函数指针。默认全空 = 未就绪。
// 由宿主在 GL 上下文就绪后填进来（见 AppEntry / Host）。
struct GlApi {
    void (*genTextures)(int count, unsigned int* textures) = nullptr;
    void (*deleteTextures)(int count, const unsigned int* textures) = nullptr;
    void (*bindTexture)(unsigned int target, unsigned int texture) = nullptr;
    void (*texImage2D)(unsigned int target, int level, int internalFormat, int width, int height,
                       int border, unsigned int format, unsigned int type, const void* pixels) = nullptr;
    void (*texParameteri)(unsigned int target, unsigned int pname, int param) = nullptr;
    void (*pixelStorei)(unsigned int pname, int param) = nullptr;

    [[nodiscard]] bool complete() const noexcept {
        return genTextures != nullptr && deleteTextures != nullptr && bindTexture != nullptr &&
               texImage2D != nullptr && texParameteri != nullptr && pixelStorei != nullptr;
    }
};

void AttachDevice(const GlApi& api);
void DetachDevice(); // 必须在 GL 上下文销毁之前调用（先把纹理 glDeleteTextures 放掉）

[[nodiscard]] const GlApi& Gl();
[[nodiscard]] bool Ready();

} // namespace shine::gpu
