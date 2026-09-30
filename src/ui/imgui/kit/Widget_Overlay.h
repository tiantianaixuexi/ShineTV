#pragma once
// shine::kit::Widget_Overlay —— 浮层系（Tooltip / Menu）
//
// 从 kit/Widgets.cpp 拆出。两者都是「贴锚点弹出的浮层」，但绘制目标不同：
// Tooltip 走 ImGui 的**前景** draw list（必须压在同一帧已画好的内容之上），
// Menu 走调用方传入的 draw list。

#include "ui/imgui/kit/Widget_Core.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 19. Tooltip（ui.css:472-507，[data-tip]）----
// bg-overlay + 1px line-normal + 11.5/500 pad 4/9 r6 + shadow-1；
// 默认贴锚点**右侧 10px 垂直居中**（left: calc(100% + 10px)），below=true 则贴下方 8px 居中。
// 只画不命中（CSS 的 pointer-events: none），且自带 200ms 出现延迟（--dur-2）。
// 走 ImGui 的前景 draw list：浮层必须压在同一帧里已经画好的内容之上。
void Tooltip(Rect anchor, std::string_view text, bool below = false);

// ---- 23. Menu（shell.css:98-145 .menu-pop）----
// 面板 min-w180 + bg-overlay + 1px line-normal + r-md10 + shadow-2 + pad5；
// 项 pad 7/10 r6 gap9 12.5；hover = fill-hover + primary；选中 = accent + accent-dim 底；
// .msep = 1px line-subtle 上下各 5px；.mlabel = 10.5/700 muted + 字距 .06em。
enum class MenuRowKind { Item, Separator, Label };
struct MenuRow {
    MenuRowKind kind = MenuRowKind::Item;
    std::string label;
    std::string icon;      // 可空
    bool selected = false; // .mi.on
    bool disabled = false;
};
// 贴 anchor 右下展开（.menu-pop 的 top: calc(100% + 6px); right: 0）。
// 返回本帧点中的**行**下标（-1 = 没点中 / 点了禁用项）。关闭与否由调用方按返回值决定。
int Menu(ImDrawList* draw, Rect anchor, const std::vector<MenuRow>& rows, std::string_view id);
[[nodiscard]] float MenuWidth(const std::vector<MenuRow>& rows);
[[nodiscard]] float MenuHeight(const std::vector<MenuRow>& rows);

} // namespace shine::kit
