#pragma once
// shine::kit::Widget_Core —— 绝对矩形 + 命中测试（所有控件族的地基）
//
// 从 kit/Widgets.cpp 拆出。Widgets.h 现在只是**门面**，转 include 本文件与
// 其余 Widget_*.h —— 下游不需要改 include。
//
// 统一约定（整个 kit 族共用，写在这里因为每族都要遵守）：
//   * 所有控件吃 **绝对矩形**（design-spec 的布局是 CSS 绝对定位式），
//     不走 ImGui 的线性布局 —— 否则外壳的自绘栅格无法控制。
//   * 命中测试统一用 InvisibleButton 占位（保持 ImGui 的 ID 栈与焦点语义）。
//   * 颜色一律从 theme 取，**不许**在调用点写 ImVec4 字面量（check-layers 规则 3）。

#include <imgui.h>

#include <string>
#include <string_view>

namespace shine::kit {

struct Rect {
    ImVec2 min;
    ImVec2 max;
    // 显式两套构造：ImVec2 不是聚合类型，四个裸 float 的字面量要能直接初始化。
    Rect() = default;
    Rect(ImVec2 a, ImVec2 b) : min(a), max(b) {}
    // ⚠️ 四参是 **(minX, minY, maxX, maxY)**，不是 (x, y, w, h)。写宽高请用 RectAt()。
    //    写错的**不报编译错**，只是 max < min，DrawRoundRect 的 `max.x <= min.x` 会直接
    //    return —— 控件整个不画、也点不到，界面上是一片空白，看不出是哪儿错了。
    //    已确认踩过的实例：Page_Assets.cpp 资产总览网格的 card / thumb、Shell.cpp 底栏
    //    页签条（2026-09-30 修）。全树扫描见 tools\find-rect-wh-misuse.ps1。
    Rect(float x0, float y0, float x1, float y1) : min(x0, y0), max(x1, y1) {}
    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
    [[nodiscard]] ImVec2 center() const { return ImVec2(0.5f * (min.x + max.x), 0.5f * (min.y + max.y)); }
    [[nodiscard]] bool contains(ImVec2 p) const { return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y; }
};

[[nodiscard]] inline Rect RectAt(float x, float y, float w, float h) {
    return Rect{ImVec2(x, y), ImVec2(x + w, y + h)};
}

// ---- 命中测试 ----
// 所有自绘控件都靠它在绝对位置占一个不可见 item，ImGui 才能维护 ID 栈与焦点。
struct Hit {
    bool hovered = false;
    bool held = false;
    bool clicked = false;
    bool doubleClicked = false;
};
[[nodiscard]] Hit HitTest(Rect bounds, std::string_view id);
// 外壳/页面层最常用的一条：本帧是否点了这块矩形。
[[nodiscard]] bool Clicked(Rect bounds, std::string_view id);
[[nodiscard]] bool Hovered(Rect bounds, std::string_view id);

// ---- 同一帧重复命中的自检（2026-09-30 加）----
//
// ImGui 同窗口内是「**先注册者独占** HoveredId」（imgui.cpp:5161
// `ItemHoverable` 里的 `if (g.HoveredId != 0 && g.HoveredId != id && !AllowOverlap) return false;`）。
// 于是**同一帧里在重叠矩形上注册第二个 item**，第二个永远
// hovered=false / clicked=false —— 而它上面已经画好了一整套控件外观：
// 编译过、不崩、截图正常、manifest 记 saved，只是那个控件按不动。
//
// 这个失败模式极难靠读代码发现（两条调用都「看起来对」），所以这里做成
// 运行时兜底：本帧内登记过的矩形，第二次重叠命中时报一次。取证跑完全部
// 工作区后若计数 > 0 即判 FAIL。
//
// 只报**同一帧**的重叠；跨帧相同 id 是正常的（ImGui 按 id 记状态）。
//
// ⚠️ 实现不在本文件，而在 Draw.cpp（与 NoteInvertedRect / NoteHoveredItem 同一组
//    取证自检计数）。声明留在这里是因为它属于命中语义。
void NoteDuplicateHit(float minX, float minY, float maxX, float maxY, const char* id);
[[nodiscard]] int DuplicateHitCount();
void ResetDuplicateHitCount();
[[nodiscard]] const char* LastDuplicateHit();

// ---- 内部原语（**不是公开 API**，页面层不要用）----
//
// 拆分前这两个是 Widgets.cpp 匿名命名空间里的 static 函数，Button / Segmented /
// Tabs / ListRow / DataTable / Tree / Menu 全都在用。拆成多文件后必须跨 TU 可见，
// 所以提到这里并放进 `detail` 命名空间 —— 不与任何公开符号同名，也不出现在
// Widgets.h 的导出面上。
namespace detail {

// 命中测试本体：返回 hovered / active / clicked / doubleClicked。
// 公开的 HitTest() 只是它的转发（那层转发是拆分前就有的，保留）。
[[nodiscard]] Hit HitTestItem(Rect bounds, std::string_view id);

// 一族控件里的第 index 项的 item id。Segmented / Tabs / DataTable / Tree /
// Menu 都靠它给同一族里的兄弟项生成互不相同的 ImGui id。
[[nodiscard]] std::string UniqueId(std::string_view id, int index);

} // namespace detail

} // namespace shine::kit
