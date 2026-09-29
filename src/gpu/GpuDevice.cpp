#include "gpu/GpuDevice.h"

#include "core/Log.h"

namespace shine::gpu {
namespace {

// GL 1.1 函数原型。刻意**不** include <GL/gl.h>：shine_core 不能拖 ImGui 头进来
// （层门禁），而 <GL/gl.h> 会和 ImGui 的 imgl3w 加载器在同一编译单元里抢符号。
// 这 6 个函数全是 GL 1.1，opengl32.dll 直接导出，链接 opengl32 即可。
// 枚举常量里只用到的也一并自己定义，避免为了两个常量去拉整个 gl.h。
using GLenum_ = unsigned int;
using GLuint_ = unsigned int;
using GLint_ = int;
using GLsizei_ = int;

constexpr GLenum_ GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum_ GL_TEXTURE_WRAP_S_ = 0x2802;
constexpr GLenum_ GL_TEXTURE_WRAP_T_ = 0x2803;
constexpr GLenum_ GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum_ GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum_ GL_RGBA_ = 0x1908;
constexpr GLenum_ GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum_ GL_CLAMP_TO_EDGE_ = 0x812F;
constexpr GLint_ GL_LINEAR_ = 0x2601;
constexpr GLenum_ GL_UNPACK_ALIGNMENT_ = 0x0CF5;

extern "C" {
void glGenTextures(GLsizei_, GLuint_*);
void glDeleteTextures(GLsizei_, const GLuint_*);
void glBindTexture(GLenum_, GLuint_);
void glTexImage2D(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void*);
void glTexParameteri(GLenum_, GLenum_, GLint_);
void glPixelStorei(GLenum_, GLint_);
}

GlApi MakeSystemGlApi() {
    GlApi api;
    api.genTextures = &glGenTextures;
    api.deleteTextures = &glDeleteTextures;
    api.bindTexture = &glBindTexture;
    api.texImage2D = &glTexImage2D;
    api.texParameteri = &glTexParameteri;
    api.pixelStorei = &glPixelStorei;
    return api;
}

GlApi g_api;

} // namespace

void AttachDevice(const GlApi& api) {
    if (!api.complete()) {
        // 没给全就别装：半套函数表比没有更危险 —— Ready() 会说真话，
        // 但第一次 Upload 才会炸在栈上。
        g_api = GlApi{};
        return;
    }
    g_api = api;
}

void DetachDevice() { g_api = GlApi{}; }

const GlApi& Gl() {
    if (!g_api.complete()) {
        g_api = MakeSystemGlApi();
    }
    return g_api;
}

bool Ready() { return g_api.complete(); }

} // namespace shine::gpu
