// shine::app::paint —— 画布 UI（P6.2）
#include "app/paint/PaintCanvasView.h"

#include "app/UiState.h"
#include "app/ui/Widgets.h"
#include "core/Log.h"
#include "gpu/GpuTextureManager.h"
#include "paint/PaintCanvas.h"
#include "theme/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace shine::app::paint {
namespace {

using ::shine::paint::PaintCanvas;
using ::shine::paint::PaintSettings;
using ::shine::paint::Rgba8;
using ::shine::paint::Tool;
using ::shine::paint::ToolLabel;
using ::shine::paint::Vec2;

PaintCanvas& Canvas() {
    static PaintCanvas c;
    return c;
}

PaintSettings& SettingsRef() {
    static PaintSettings s;
    return s;
}

gpu::GpuTextureHandle g_texBase;
gpu::GpuTextureHandle g_texMask;
std::uint64_t g_uploadedRev = 0;
float g_zoom = 1.f;
float g_panX = 0.f; // 像素（内容坐标）
float g_panY = 0.f;
Tool g_tool = Tool::PaintBase;

void EnsureCanvas() {
    PaintCanvas& c = Canvas();
    if (!c.valid()) {
        c.Resize(512, 512, {.r = 40, .g = 40, .b = 48, .a = 255});
        g_uploadedRev = 0;
    }
}

void UploadTexturesIfDirty() {
    PaintCanvas& c = Canvas();
    if (!c.valid() || c.Revision() == g_uploadedRev) {
        return;
    }
    const auto base = c.BasePixels();
    const auto mask = c.MaskPixels();
    if (base.empty()) {
        return;
    }
    // 底图
    if (g_texBase) {
        gpu::Textures().Release(g_texBase);
        g_texBase = {};
    }
    auto up = gpu::Textures().Upload(c.Width(), c.Height(), base);
    if (up) {
        g_texBase = *up;
    }
    // 遮罩叠层：RGBA，白=255，透明=0
    if (g_texMask) {
        gpu::Textures().Release(g_texMask);
        g_texMask = {};
    }
    std::vector<std::byte> maskRgba(mask.size() * 4u);
    const theme::ThemeColors& th = theme::Current();
    const auto accent = static_cast<std::uint8_t>(std::clamp(th.accent[0], 0.f, 1.f) * 255.f);
    const auto accentG = static_cast<std::uint8_t>(std::clamp(th.accent[1], 0.f, 1.f) * 255.f);
    const auto accentB = static_cast<std::uint8_t>(std::clamp(th.accent[2], 0.f, 1.f) * 255.f);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        std::byte* p = maskRgba.data() + i * 4u;
        if (mask[i] >= 128) {
            p[0] = static_cast<std::byte>(accent);
            p[1] = static_cast<std::byte>(accentG);
            p[2] = static_cast<std::byte>(accentB);
            p[3] = static_cast<std::byte>(140);
        } else {
            p[3] = std::byte{0};
        }
    }
    auto upM = gpu::Textures().Upload(c.Width(), c.Height(), maskRgba);
    if (upM) {
        g_texMask = *upM;
    }
    g_uploadedRev = c.Revision();
}

[[nodiscard]] Vec2 ScreenToUv(ImVec2 mouse, ImVec2 imageTl, ImVec2 imageSize, float zoom) {
    if (imageSize.x <= 0.f || imageSize.y <= 0.f) {
        return {0.f, 0.f};
    }
    const float lx = (mouse.x - imageTl.x) / zoom;
    const float ly = (mouse.y - imageTl.y) / zoom;
    return {lx / imageSize.x, ly / imageSize.y};
}

void HandlePaintInput(ImVec2 imageTl, ImVec2 contentSize, ImVec2 windowPos) {
    PaintCanvas& c = Canvas();
    PaintSettings& ps = SettingsRef();
    ImGuiIO& io = ImGui::GetIO();

    // 快捷键（输入框聚焦时不触发由调用侧保证：画布窗口无文本框）
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) {
        ps.brushRadiusUv = std::max(0.002f, ps.brushRadiusUv * 0.8f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) {
        ps.brushRadiusUv = std::min(0.5f, ps.brushRadiusUv * 1.25f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && c.StrokeActive()) {
        c.EndStroke();
    }

    // 中键 / 空格+左键 平移
    const bool panMode = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                         (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (panMode) {
        g_panX += io.MouseDelta.x;
        g_panY += io.MouseDelta.y;
        return;
    }

    // 滚轮：以光标为锚点缩放
    if (ImGui::IsWindowHovered() && std::fabs(io.MouseWheel) > 0.01f) {
        const float oldZoom = g_zoom;
        g_zoom = std::clamp(g_zoom * (io.MouseWheel > 0.f ? 1.1f : 1.f / 1.1f), 0.05f, 16.f);
        const ImVec2 mouse = io.MousePos;
        // content point under cursor stays
        const float cx = (mouse.x - imageTl.x) / oldZoom;
        const float cy = (mouse.y - imageTl.y) / oldZoom;
        // new imageTl' such that mouse - tl' = c * newZoom
        // imageTl is computed from pan; we adjust pan so tl shifts correctly
        const ImVec2 origin = windowPos; // approximate: use current imageTl formula
        (void)origin;
        const float wantTlX = mouse.x - cx * g_zoom;
        const float wantTlY = mouse.y - cy * g_zoom;
        // imageTl = windowPos + padding + pan + centering; invert delta:
        g_panX += wantTlX - imageTl.x;
        g_panY += wantTlY - imageTl.y;
    }

    if (!ImGui::IsWindowHovered()) {
        return;
    }
    const ImVec2 mouse = io.MousePos;
    const Vec2 uv = ScreenToUv(mouse, imageTl, contentSize, g_zoom);
    const Rgba8 color = ps.paintColor;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !panMode) {
        c.BeginStroke();
        c.PaintStroke(uv, uv, ps.brushRadiusUv, color, g_tool);
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && c.StrokeActive() && !panMode) {
        static Vec2 last = uv;
        c.PaintStroke(last, uv, ps.brushRadiusUv, color, g_tool);
        last = uv;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && c.StrokeActive()) {
        c.EndStroke();
    }
}

} // namespace

void DrawPaintCanvasWindow() {
    EnsureCanvas();
    UploadTexturesIfDirty();
    PaintCanvas& c = Canvas();
    const PaintSettings& ps = SettingsRef();

    if (ImGui::Button("新建 512×512")) {
        c.Resize(512, 512);
        g_uploadedRev = 0;
        g_zoom = 1.f;
        g_panX = g_panY = 0.f;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("滚轮缩放 · 中键/空格平移 · [ ] 笔刷 · Esc 结束笔画");
    ImGui::SameLine();
    ImGui::TextDisabled("缩放 %.0f%%  笔刷 %.1f%%", g_zoom * 100.f, ps.brushRadiusUv * 100.f);

    ImGui::BeginChild("##paint_canvas", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float cw = static_cast<float>(c.Width());
    const float ch = static_cast<float>(c.Height());
    const float dispW = cw * g_zoom;
    const float dispH = ch * g_zoom;
    const float ox = std::max(0.f, (avail.x - dispW) * 0.5f) + g_panX;
    const float oy = std::max(0.f, (avail.y - dispH) * 0.5f) + g_panY;
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 content0 = ImGui::GetCursorScreenPos();
    const ImVec2 imageTl(content0.x + ox, content0.y + oy);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const theme::ThemeColors& th = theme::Current();
    draw->AddRectFilled(imageTl, ImVec2(imageTl.x + dispW, imageTl.y + dispH),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(th.childBg[0], th.childBg[1], th.childBg[2], 1.f)));

    gpu::GpuTexture* base = g_texBase ? gpu::Textures().Get(g_texBase) : nullptr;
    if (base != nullptr && base->valid()) {
        ImGui::SetCursorScreenPos(imageTl);
        ImGui::Image(ImTextureRef(base->imgui_id()), ImVec2(dispW, dispH));
    } else {
        ImGui::SetCursorScreenPos(imageTl);
        ImGui::Dummy(ImVec2(dispW, dispH));
    }
    if (ps.showMask) {
        gpu::GpuTexture* mask = g_texMask ? gpu::Textures().Get(g_texMask) : nullptr;
        if (mask != nullptr && mask->valid()) {
            ImGui::SetCursorScreenPos(imageTl);
            ImGui::Image(ImTextureRef(mask->imgui_id()), ImVec2(dispW, dispH));
        }
    }

    // 笔刷圆环
    if (ImGui::IsWindowHovered()) {
        const float r = ps.brushRadiusUv * std::max(cw, ch) * g_zoom;
        draw->AddCircle(ImGui::GetIO().MousePos, r,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(th.accent[0], th.accent[1], th.accent[2], 0.9f)), 32,
                        1.5f);
    }

    HandlePaintInput(imageTl, ImVec2(cw, ch), winPos);
    ImGui::EndChild();
}

void DrawPaintSidePanel() {
    EnsureCanvas();
    PaintCanvas& c = Canvas();
    PaintSettings& ps = SettingsRef();
    app::ui::SectionText("工具");
    static const char* const kTools[] = {"画底", "擦底", "画遮罩", "擦遮罩"};
    int tool = static_cast<int>(g_tool);
    if (ImGui::Combo("##paint_tool", &tool, kTools, 4)) {
        g_tool = static_cast<Tool>(tool);
        if (c.StrokeActive()) {
            c.EndStroke();
        }
    }
    ImGui::TextDisabled("当前：%s", ToolLabel(g_tool));

    ImGui::SliderFloat("笔刷半径", &ps.brushRadiusUv, 0.002f, 0.25f, "%.3f");
    {
        float col[4] = {ps.paintColor.r / 255.f, ps.paintColor.g / 255.f, ps.paintColor.b / 255.f,
                        ps.paintColor.a / 255.f};
        if (ImGui::ColorEdit4("颜色", col, ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            ps.paintColor.r = static_cast<std::uint8_t>(std::clamp(col[0], 0.f, 1.f) * 255.f + 0.5f);
            ps.paintColor.g = static_cast<std::uint8_t>(std::clamp(col[1], 0.f, 1.f) * 255.f + 0.5f);
            ps.paintColor.b = static_cast<std::uint8_t>(std::clamp(col[2], 0.f, 1.f) * 255.f + 0.5f);
            ps.paintColor.a = static_cast<std::uint8_t>(std::clamp(col[3], 0.f, 1.f) * 255.f + 0.5f);
        }
    }
    ImGui::Checkbox("显示遮罩", &ps.showMask);

    app::ui::SectionText("操作");
    if (ImGui::Button("填充颜色", ImVec2(-1.f, 0.f))) {
        c.FillAll(Tool::PaintBase, ps.paintColor);
    }
    if (ImGui::Button("遮罩全选", ImVec2(-1.f, 0.f))) {
        c.FillAll(Tool::PaintMask);
    }
    if (ImGui::Button("清空遮罩", ImVec2(-1.f, 0.f))) {
        c.ClearMask();
    }
    if (ImGui::Button("重置为初始", ImVec2(-1.f, 0.f))) {
        c.ResetToInitial();
        g_uploadedRev = 0;
    }

    app::ui::SectionText("画布");
    app::ui::KvRow("尺寸", std::to_string(c.Width()) + " × " + std::to_string(c.Height()));
    app::ui::KvRow("Revision", std::to_string(c.Revision()));
    app::ui::KvRow("缩放", std::to_string(static_cast<int>(g_zoom * 100)) + "%");
    ImGui::TextDisabled("inpaint 提交在 P6.3 接入");
}

} // namespace shine::app::paint
