#pragma once
// shine::paint —— 画布数据模型类型（P6.1 S1）
#include <cstdint>

namespace shine::paint {

// 笔刷作用对象
enum class Tool : int {
    PaintBase = 0, // 在底图上画颜色
    EraseBase,     // 底图擦回初始快照
    PaintMask,     // 遮罩涂 255
    EraseMask,     // 遮罩涂 0
};

[[nodiscard]] constexpr const char* ToolLabel(Tool tool) noexcept {
    switch (tool) {
    case Tool::PaintBase:
        return "画底";
    case Tool::EraseBase:
        return "擦底";
    case Tool::PaintMask:
        return "画遮罩";
    case Tool::EraseMask:
        return "擦遮罩";
    }
    return "未知";
}

// 导出遮罩时的语义（P6.3 S4）：白 = 可重绘区域
enum class MaskMode : int {
    Editable = 0, // 白 = 可编辑（默认：涂过的地方会重绘）
    Protect,      // 白 = 保护（涂过的地方不重绘）→ 导出前反相
};

struct Rgba8 {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

// 画布侧参数（完整 inpaint 参数在 P6.3 进 Settings / 工程）
struct PaintSettings {
    float brushRadiusUv = 0.02f; // 半径 / max(w,h)
    Rgba8 paintColor{.r = 220, .g = 60, .b = 60, .a = 255};
    MaskMode maskMode = MaskMode::Editable;
    bool showMask = true;
};

} // namespace shine::paint
