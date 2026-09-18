#pragma once
// shine::gpu::GpuDevice —— DX11 设备注入点（P4.1 S1）
//
// 谁先需要谁建：P4.1（媒体预览/纹理）与图片库 G 线（G-S3/G-S8）**共用这一份**。
// 注入时机：`main.cpp` 里 `ImGui_ImplDX11_Init(...)` 之后、`app::Init()` 之前。
#include <d3d11.h>

namespace shine::gpu {

void AttachDevice(ID3D11Device* device, ID3D11DeviceContext* context);
void DetachDevice(); // 必须在 CleanupDeviceD3D() 之前调用（先把纹理放掉）

[[nodiscard]] ID3D11Device* Device();
[[nodiscard]] ID3D11DeviceContext* Context();
[[nodiscard]] bool Ready();

} // namespace shine::gpu
