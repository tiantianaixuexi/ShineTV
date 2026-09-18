#include "gpu/GpuDevice.h"

#include "core/Log.h"

namespace shine::gpu {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;

} // namespace

void AttachDevice(ID3D11Device* device, ID3D11DeviceContext* context) {
    // 注意：此时 log::Init() 还没跑（它在 app::Init() 里）→ 这一行由 App::Init() 补打
    g_device = device;
    g_context = context;
}

void DetachDevice() {
    g_device = nullptr;
    g_context = nullptr;
}

ID3D11Device* Device() { return g_device; }
ID3D11DeviceContext* Context() { return g_context; }
bool Ready() { return g_device != nullptr; }

} // namespace shine::gpu
