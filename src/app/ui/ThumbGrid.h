#pragma once
// shine::app::ui::ThumbGrid —— 通用缩略图网格（G-S6 抽出，可复用）
//
// 只依赖 ImGui + theme（经 Current() 取色），**不认识** gallery/gpu/media。
// 适用：图库、输出预览、素材选择、角色参考图……任何「一格图 + 两行字」的墙。
//
// 适配要点（修「文字穿格 / 图压标签 / 列数错」）：
//   * 每格 `PushClipRect`，标签超宽自动 `…`（UTF-8 安全）
//   * 图严格 fit 进缩略图盒，不侵占标签区；透明底用棋盘格
//   * 列数按可用宽度计算并 clamp，永不横向溢出
//   * 工具条/状态条由调用方画；本组件只画网格体
#include <imgui.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shine::app::ui {

struct ThumbGridItem {
    std::uint64_t id = 0;
    std::string label;    // 主文案（文件名等），超宽省略
    std::string sublabel; // 副文案（尺寸等），超宽省略
    std::string tooltip;  // 悬停完整说明（可多行）
    std::string dragPayload; // 非空则格子可拖拽（UTF-8，如绝对路径）
    ImTextureID texture = 0;
    std::uint32_t imageW = 0;
    std::uint32_t imageH = 0;
    bool selected = false;
    bool failed = false;      // 画 `!` 角标
    bool placeholder = false; // 加载中占位
};

struct ThumbGridStyle {
    float thumbSide = 256.f; // 缩略图盒边长（目标长边）
    float thumbSideMin = 64.f; // Ctrl+滚轮缩放下限
    float thumbSideMax = 512.f; // Ctrl+滚轮缩放上限
    float gap = 6.f;         // 单元格间距
    float pad = 4.f;         // 格内边距
    float labelLineH = 16.f;
    int labelLines = 2; // 标签行数（主+副）
    float round = 4.f;
    float borderSelected = 2.f;
    float borderHover = 1.5f;
    bool enableCtrlWheelZoom = true; // 悬停网格时 Ctrl+滚轮改 thumbSide（clamp 到 min/max）
    float zoomStep = 1.12f;          // 每格滚轮的倍率（>1）
};

struct ThumbGridOutcome {
    int columns = 0;
    int rows = 0;
    std::uint64_t clickedId = 0;       // 0 = 本帧无单击（左键）
    std::uint64_t rightClickedId = 0;  // G-S11：右键
    std::uint64_t doubleClickedId = 0; // 0 = 本帧无双击
    std::size_t visibleBegin = 0;      // 绘制用可见区间（含上下预取行）
    std::size_t visibleEnd = 0;        // 不含
    float contentHeight = 0.f;
    float scrollY = 0.f;     // G-S9：供调用方算请求带与滚动方向
    float viewH = 0.f;       // 可视高度
    bool zoomChanged = false; // 本帧 Ctrl+滚轮是否改了 style.thumbSide
    float thumbSide = 0.f;    // 缩放后的边长（= style.thumbSide）
};

// 在当前光标处画可滚动网格。height < 0 时用剩余 ContentRegion.y。
// `style` 非 const：Ctrl+滚轮会就地改 `thumbSide`（并 clamp）；调用方据 `zoomChanged` 落盘/重建缓存。
// 结果里的列数以**本帧实际布局**为准（调用方拿去显示「N 列」）。
[[nodiscard]] ThumbGridOutcome DrawThumbGrid(std::string_view strId, std::span<const ThumbGridItem> items,
                                             ThumbGridStyle& style, float height = -1.f);

// UTF-8 省略号截断，保证 CalcTextSize ≤ maxWidth
[[nodiscard]] std::string Ellipsize(std::string_view text, float maxWidth);

// 透明棋盘格（主题 token，不硬编码色）
void DrawCheckerboard(ImVec2 min, ImVec2 max, float cell = 8.f);

// 保持宽高比放进 box，居中用的尺寸
[[nodiscard]] ImVec2 FitInBox(std::uint32_t imageW, std::uint32_t imageH, ImVec2 box);

} // namespace shine::app::ui
