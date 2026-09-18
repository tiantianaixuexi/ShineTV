// R-S0 搬家示范：从 `App.cpp` 原样搬出（行为一字未改），只补 include。
#include "app/views/temp/TextureSelfCheck.h"

#include "app/ui/Widgets.h" // R-S1：实时复用 ThumbTile（图库网格将来用同一个）
#include "core/Log.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTexture.h"
#include "gpu/GpuTextureManager.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace shine::app {
namespace {

// ⚠️ **交给 ImGui 画过的纹理必须跨帧保活**（2026-09-17 修崩溃，实测证据见下）：
// ImGui 在 `ImGui::Image()` 时只是把 **SRV 指针**记进 draw list，真正使用是本帧末尾的
// `ImGui_ImplDX11_RenderDrawData()` → `device->PSSetShaderResources(0, 1, &texture_srv)`。
// 所以"**同一帧** Upload → 画 → `Release`"是**用已释放的 SRV 去问驱动**：
//   实测崩溃栈 `wWinMain (main.cpp:190) → ImGui_ImplDX11_RenderDrawData (imgui_impl_dx11.cpp:328) → d3d11.dll`，
//   WER 报的出错模块是显卡驱动 `nvwgf2umx.dll`，异常码 `0xC0000005`，**随机在启动后 15–25 秒崩**
//   （释放后的内存有时还没被复用，所以不是每帧都触发；一旦被复用就 AV，极易误判成"没连 ComfyUI 就崩"）。
// 规则：要释放就**等本帧命令列表提交之后**（下一帧开头 / `DrainUiQueue` 阶段），或像这里一样全程保活。
constexpr std::uint32_t kSides[4] = {256, 128, 64, 32};
const std::byte kColors[4][4] = {{std::byte{0xE0}, std::byte{0x50}, std::byte{0x50}, std::byte{0xFF}},
                                 {std::byte{0x50}, std::byte{0xD0}, std::byte{0x60}, std::byte{0xFF}},
                                 {std::byte{0x50}, std::byte{0x90}, std::byte{0xE0}, std::byte{0xFF}},
                                 {std::byte{0xE0}, std::byte{0xC0}, std::byte{0x40}, std::byte{0xFF}}};

std::array<gpu::GpuTextureHandle, 4> g_handles{}; // 全程保活（退出时由 `app::Shutdown()` 的 ReleaseAll 统一回收）
size_t g_bytes256 = 0;
bool g_created = false;

// 只在第一帧创建一次；此后每帧复用同一批纹理
void EnsureTextures() {
    if (g_created) {
        return;
    }
    g_created = true;
    for (int i = 0; i < 4; ++i) {
        const std::uint32_t side = kSides[i];
        std::vector<std::byte> pixels(static_cast<std::size_t>(side) * side * 4);
        for (std::size_t p = 0; p < pixels.size(); p += 4) {
            std::memcpy(pixels.data() + p, kColors[i], 4);
        }
        auto handle = gpu::Textures().Upload(side, side, pixels);
        if (!handle.has_value()) {
            log::Warn("TEMP-G3: {}x{} 纹理上传失败：{}", side, side, gpu::GpuErrorText(handle.error()));
            continue;
        }
        g_handles[i] = *handle;
        if (i == 0) {
            if (gpu::GpuTexture* texture = gpu::Textures().Get(*handle)) {
                g_bytes256 = texture->bytes();
            }
        }
    }
    log::Info("TEMP-G3 textures: 4 张已创建并保活（UsedBytes={} B，TextureCount={}）", gpu::Textures().UsedBytes(),
              gpu::Textures().TextureCount());
}

} // namespace

void DrawTextureSelfCheck() {
    if (!gpu::Ready()) {
        return;
    }
    static bool s_ranLoop = false;
    static bool s_logged = false;

    if (!s_ranLoop) {
        s_ranLoop = true;
        // 这 500 张**只创建/释放、从不绘制** → 同帧释放是安全的（它们从没进过 draw list）
        std::vector<std::byte> pixels(static_cast<std::size_t>(64) * 64 * 4, std::byte{0x80});
        int created = 0;
        for (int i = 0; i < 500; ++i) {
            auto handle = gpu::Textures().Upload(64, 64, pixels);
            if (handle.has_value()) {
                ++created;
                gpu::Textures().Release(*handle);
            }
        }
        log::Info("TEMP-G3 loop: 创建/释放 {} 次 → UsedBytes={} B，TextureCount={}", created,
                  gpu::Textures().UsedBytes(), gpu::Textures().TextureCount());
    }

    EnsureTextures();

    ImGui::Begin("纹理自检");
    for (int i = 0; i < 4; ++i) {
        if (gpu::GpuTexture* texture = gpu::Textures().Get(g_handles[i])) {
            app::ui::ThumbTile(texture->imgui_id(), ImVec2(96, 96), {}); // R-S1：改用组件
            if (i < 3) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::Text("4 张测试纹理（256/128/64/32）· 256 尺寸 bytes()=%zu · UsedBytes=%zu B（跨帧保活）", g_bytes256,
                gpu::Textures().UsedBytes());
    ImGui::End();

    if (!s_logged) {
        s_logged = true;
        log::Info("TEMP-G3 draw: 4 张纹理已绘制（256 尺寸 bytes()={}，期望 262144）；UsedBytes={} B", g_bytes256,
                  gpu::Textures().UsedBytes());
    }
}

} // namespace shine::app
