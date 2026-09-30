#include "ui/imgui/pages/PageCommon.h"

#include "core/Log.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

namespace shine::pages {

using namespace shine::kit;

float LabelWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, 1e9f, 0.0f, text, text + std::strlen(text)).x;
}

// 三页共用的**可视底**（绝对屏幕 y）。
//
// Shell 每帧把真实可视高度写进 WorkspaceViewportHeight()，而它给页面的布局区是
// **写死的 2400px** —— 那是「内容超出视口仍能被滚到」的上限，不是可视高度。页面
// 拿 2400 当视口，就会把「常驻可见」的底部元素钉到 y≈2280（时间轴、阶段表脚注），
// 要滚到最底才看得着，而上面全是空白。
//
// 返回 area.max.y = Shell 尚未写入这一帧（单跑页面 / 早于第一帧），调用方据此
// 退回旧行为 —— 逐页迁移，不要求外壳先到齐。
float ViewportBottom(Rect area) {
    const float viewportH = pages::WorkspaceViewportHeight();
    return viewportH > 0.0f ? std::min(area.max.y, area.min.y + viewportH) : area.max.y;
}

// ---- 页面层 → 外壳的运行时通道 ----
//
// 页面不该认识 Shell（那是外壳的活），所以这几条都是**外壳写、页面读**的单向通道。
// 实现放在页面层只是为了和调用点住在一起；声明在 WorkspacePages.h。
// 从 WorkspaceB.cpp 原样搬来：那里它们和 novel.db 快照挤在同一个 .cpp 里，
// 拆开之后「谁在写、谁在读」才看得出来。
namespace {
std::function<void(std::string)>& ToastSink() {
    static std::function<void(std::string)> sink;
    return sink;
}
float& WorkspaceViewportHeightSlot() {
    static float height = 0.0f;
    return height;
}
float& PageContentHeightSlot() {
    static float height = 0.0f;
    return height;
}
} // namespace

// 没注入时（单跑页面、或注入前的那几帧）调用是空操作，不会崩，也不会静默吞掉 ——
// 调用点是「用户点了个按钮」，静默吞掉等于按钮又变成空操作。
void SetWorkspaceToast(std::function<void(std::string)> sink) { ToastSink() = std::move(sink); }

void SetWorkspaceViewportHeight(float height) { WorkspaceViewportHeightSlot() = height; }

float WorkspaceViewportHeight() { return WorkspaceViewportHeightSlot(); }

void SetPageContentHeight(float height) { PageContentHeightSlot() = std::max(0.0f, height); }
float PageContentHeight() { return PageContentHeightSlot(); }
void ResetPageContentHeight() { PageContentHeightSlot() = 0.0f; }

void WorkspaceToast(std::string message) {
    if (auto& sink = ToastSink(); sink) {
        sink(std::move(message));
    } else {
        shine::log::Warn("pages: 提示无处可送（外壳未注入 toast 通道）· {}", message);
    }
}

} // namespace shine::pages
