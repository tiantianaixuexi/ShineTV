#include "ui/imgui/kit/View_FlowCanvas.h"

#include <algorithm>
#include <cmath>

namespace shine::kit {

// ================================================================ 23 FlowCanvas
// 出图/出片两页共用的节点画布。规格全部照 FlowCanvas.jsx + views.css:1005-1113。
//
// 交互模型与仓库其余自绘控件一致：画布不是 ImGui item，所以命中与拖拽
// 在 C++ 里手算。⚠️ 缩放锚点必须以**光标下的世界坐标**反推；
// 直接改 z 会让内容朝画布中心漂。

namespace {

// 端口：左右两侧、垂直居中（views.css:1093-1094）。
ImVec2 PortPos(const FlowNode& node, bool out) {
    return ImVec2(node.x + (out ? kFlowNodeW : 0.0f), node.y + kFlowNodeH * 0.5f);
}

// 三次贝塞尔取点。dx = max(36, |Δx| * 0.55)（FlowCanvas.jsx:16-19）。
ImVec2 BezierAt(ImVec2 a, ImVec2 b, float dx, float t) {
    const float u = 1.0f - t;
    const float w0 = u * u * u;
    const float w1 = 3.0f * u * u * t;
    const float w2 = 3.0f * u * t * t;
    const float w3 = t * t * t;
    return ImVec2(w0 * a.x + w1 * (a.x + dx) + w2 * (b.x - dx) + w3 * b.x,
                  w0 * a.y + w1 * a.y + w2 * b.y + w3 * b.y);
}

// ImDrawList 没有三次贝塞尔，24 段折线逼近（视觉上与曲线无法区分）。
// dashOffset 是 6 6 虚线的相位（flow-dash 0.7s linear infinite）。
void DrawLink(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, float thickness, bool dashed,
              float dashOffset) {
    const float dx = std::max(36.0f, std::fabs(b.x - a.x) * 0.55f);
    constexpr int kSegments = 24;
    constexpr float kDash = 6.0f;
    const float chain = std::fabs(b.x - a.x) + std::fabs(b.y - a.y);
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = static_cast<float>(i) / kSegments;
        const float t1 = static_cast<float>(i + 1) / kSegments;
        if (dashed) {
            const float s0 = chain * t0 + dashOffset;
            const float s1 = chain * t1 + dashOffset;
            const bool gap = std::fmod(s0, kDash * 2.0f) > kDash;
            if (gap) {
                continue; // 落在 6px 空档里
            }
        }
        draw->PathLineTo(BezierAt(a, b, dx, t0));
        draw->PathLineTo(BezierAt(a, b, dx, t1));
    }
    draw->PathStroke(color, 0, thickness);
}

ImU32 FlowStateColor(FlowState state) {
    switch (state) {
    case FlowState::Running: return theme::ToImU32(theme::Current().statusBusy);
    case FlowState::Done: return theme::ToImU32(theme::Current().statusOk);
    case FlowState::Failed: return theme::ToImU32(theme::Current().statusDanger);
    case FlowState::Todo:
    case FlowState::Skipped: break;
    }
    return ColorLineNormal();
}

} // namespace

void FlowLayoutNodes(std::vector<FlowNode>& nodes, FlowView& view) {
    // 首帧铺网格：4 列、间距 40px。设计稿的初始位置来自 mock 数据，
    // 这里给一个稳定可复现的布局（fit() 之后两者视觉等价）。
    constexpr int kColumns = 4;
    constexpr float kGap = 40.0f;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const int col = static_cast<int>(i) % kColumns;
        const int row = static_cast<int>(i) / kColumns;
        nodes[i].x = 8.0f + (kFlowNodeW + kGap) * col;
        nodes[i].y = 8.0f + (kFlowNodeH + kGap) * row;
    }
    view.x = 40.0f;
    view.y = 30.0f;
    view.z = 1.0f;
}

void FlowFit(const std::vector<FlowNode>& nodes, Rect bounds, float fitInset, FlowView& view) {
    if (nodes.empty()) {
        return;
    }
    // ⚠️ fitInset 是「画布右侧被浮动面板占掉的宽度」，只减一次。
    //    页面已经把面板从 canvas 里排除掉了（canvas 宽度 = area.width - 348 - 32），
    //    这里再减一次会把可用宽压到接近 0，z 退化成一团看不清的东西。
    float minX = nodes[0].x;
    float minY = nodes[0].y;
    float maxX = nodes[0].x + kFlowNodeW;
    float maxY = nodes[0].y + kFlowNodeH;
    for (const FlowNode& node : nodes) {
        minX = std::min(minX, node.x);
        minY = std::min(minY, node.y);
        maxX = std::max(maxX, node.x + kFlowNodeW);
        maxY = std::max(maxY, node.y + kFlowNodeH);
    }
    minX -= 24.0f;
    minY -= 24.0f;
    maxX += 24.0f;
    maxY += 24.0f;
    const float spanX = std::max(1.0f, maxX - minX);
    const float spanY = std::max(1.0f, maxY - minY);
    // 两边都留 8px 内边距再算可用区：留了内边距却用**未扣**的宽度去居中，
    // 右侧就会少 8px，连线正好被浮动面板切掉（FlowCanvas.jsx:49 那个 +8 是
    // 给它的 availW 减 48 配套的，两边必须成对）。
    constexpr float kPad = 8.0f;
    const float fitW = std::max(64.0f, bounds.width() - fitInset - kPad * 2.0f);
    const float fitH = std::max(64.0f, bounds.height() - kPad * 2.0f);
    const float z = std::clamp(std::min(fitW / spanX, fitH / spanY), 0.35f, 1.15f);
    view.z = z;
    // 水平居中（fitInset 让出右侧），垂直居中 —— 对应 FlowCanvas.jsx:47-51
    view.x = kPad + (fitW - spanX * z) * 0.5f - minX * z;
    view.y = kPad + (fitH - spanY * z) * 0.5f - minY * z;
}

void FlowCanvas(ImDrawList* draw, Rect bounds, std::vector<FlowNode>& nodes,
                const std::vector<FlowLink>& links, FlowView& view, int& selected, float fitInset) {
    if (nodes.empty()) {
        return;
    }

    // ---- 滚轮以光标为锚缩放 ----
    // ⚠️ 必须先确认指针在本区域内，否则滚轮会同时把外层页面滚下去。
    //    手算而不是 ImGui::IsMouseHoveringRect —— 后者在**本工程**会 0xC0000005
    //    （fault offset 0x8b8597），refactor/PROGRESS.md 有那次崩溃的记录。
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool hovered = bounds.contains(mouse);
    if (hovered) {
        if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f) {
            const float z2 = std::clamp(view.z * std::exp(-wheel * 0.0014f), 0.35f, 2.0f);
            const float wx = (mouse.x - bounds.min.x - view.x) / view.z;
            const float wy = (mouse.y - bounds.min.y - view.y) / view.z;
            view.z = z2;
            view.x = mouse.x - bounds.min.x - wx * z2;
            view.y = mouse.y - bounds.min.y - wy * z2;
        }
    }

    const auto ToScreen = [&](float wx, float wy) {
        return ImVec2(bounds.min.x + view.x + wx * view.z, bounds.min.y + view.y + wy * view.z);
    };
    const auto ToWorld = [&](float sx, float sy) {
        return ImVec2((sx - bounds.min.x - view.x) / view.z, (sy - bounds.min.y - view.y) / view.z);
    };

    // ---- 命中：先测节点，再落回背景（后画的在上层，倒序测）----
    int hitId = -1;
    for (std::size_t i = nodes.size(); i-- > 0;) {
        const FlowNode& node = nodes[i];
        const ImVec2 topLeft = ToScreen(node.x, node.y);
        if (mouse.x >= topLeft.x && mouse.x <= topLeft.x + kFlowNodeW * view.z &&
            mouse.y >= topLeft.y && mouse.y <= topLeft.y + kFlowNodeH * view.z) {
            hitId = node.id;
            break;
        }
    }

    // ---- 拖拽状态（跨帧）----
    static int draggingId = -1;
    static bool panning = false;
    static ImVec2 dragOffset{};
    static ImVec2 panStartMouse{};
    static ImVec2 panStartView{};

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered) {
        if (hitId >= 0) {
            selected = hitId;
            draggingId = hitId;
            const ImVec2 world = ToWorld(mouse.x, mouse.y);
            for (const FlowNode& node : nodes) {
                if (node.id == hitId) {
                    dragOffset = ImVec2(world.x - node.x, world.y - node.y);
                }
            }
        } else {
            selected = -1;
            panning = true;
            panStartMouse = mouse;
            panStartView = ImVec2(view.x, view.y);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        draggingId = -1;
        panning = false;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (draggingId >= 0) {
            const ImVec2 world = ToWorld(mouse.x, mouse.y);
            for (FlowNode& node : nodes) {
                if (node.id == draggingId) {
                    node.x = std::max(8.0f, world.x - dragOffset.x);
                    node.y = std::max(8.0f, world.y - dragOffset.y);
                }
            }
        } else if (panning) {
            view.x = panStartView.x + (mouse.x - panStartMouse.x);
            view.y = panStartView.y + (mouse.y - panStartMouse.y);
        }
    }

    // ---- 连线（画在节点之下）----
    // 两层描边：底层 5px line-normal 40%，上层 1.8px（active = accent 虚线）
    const float dashOffset = Now() * 0.7f * 12.0f;
    for (const FlowLink& link : links) {
        const FlowNode* a = nullptr;
        const FlowNode* b = nullptr;
        for (const FlowNode& n : nodes) {
            if (n.id == link.from) { a = &n; }
            if (n.id == link.to) { b = &n; }
        }
        if (a == nullptr || b == nullptr) {
            continue;
        }
        const bool active = a->state != FlowState::Todo && b->state != FlowState::Todo;
        const ImVec2 pa = ToScreen(PortPos(*a, true).x, PortPos(*a, true).y);
        const ImVec2 pb = ToScreen(PortPos(*b, false).x, PortPos(*b, false).y);
        DrawLink(draw, pa, pb, WithAlpha(ColorLineNormal(), 0.4f), 5.0f * view.z, false, 0.0f);
        DrawLink(draw, pa, pb, active ? ColorAccent() : ColorLineStrong(), 1.8f * view.z, active,
                 dashOffset);
    }

    // ---- 节点 ----
    for (const FlowNode& node : nodes) {
        const ImVec2 topLeft = ToScreen(node.x, node.y);
        const float w = kFlowNodeW * view.z;
        const float h = kFlowNodeH * view.z;
        const Rect box{topLeft, ImVec2(topLeft.x + w, topLeft.y + h)};
        const bool isSel = node.id == selected;

        // views.css:1035 `.fnode { box-shadow: var(--shadow-1) }` —— **常驻**，不是
        // hover 才出现。早先这里只画本体，投影整个缺失，画布上的节点是平的。
        // 投影随缩放一起缩放（CSS 里节点是固定 150px，ImGui 侧 view.z 缩放的是
        // 整个坐标系，所以偏移与模糊半径乘 view.z 才与本体保持同一比例）。
        DrawShadow(draw, box.min, box.max, 10.0f * view.z, theme::ShadowTier::Card, 1.0f, view.z);
        DrawRoundRect(draw, box.min, box.max, 10.0f * view.z, ColorPanel(),
                      isSel ? ColorAccent() : FlowStateColor(node.state), 1.5f * view.z);
        // .fnode.sel / .fnode.run 的 `0 0 0 3px <淡色>` 外圈（box-shadow 近似，R5）
        if (isSel) {
            DrawRoundRect(draw, box.min - ImVec2(3, 3), box.max + ImVec2(3, 3), 12.0f * view.z, 0,
                          ColorOf(theme::CurrentDerived().accentDim), 2.0f * view.z);
        } else if (node.state == FlowState::Running) {
            DrawRoundRect(draw, box.min - ImVec2(3, 3), box.max + ImVec2(3, 3), 12.0f * view.z, 0,
                          WithAlpha(theme::ToImU32(theme::Current().statusBusy), 0.18f), 2.0f * view.z);
        }

        // fhead：13px accent 图标 + 标题 12px/700，pad 7/10，下边框 line-subtle
        const float padX = 10.0f * view.z;
        const float headH = 28.0f * view.z;
        DrawIcon(draw, node.icon.c_str(), ImVec2(box.min.x + padX, box.min.y + 7.0f * view.z),
                 13.0f * view.z, ColorAccent());
        DrawTextClipped(draw, FontBoldAt(12.0f), 12.0f * view.z,
                        ImVec2(box.min.x + padX + 20.0f * view.z, box.min.y + 7.5f * view.z),
                        w - padX * 2.0f - 40.0f * view.z, ColorText(), node.title);
        draw->AddLine(ImVec2(box.min.x, box.min.y + headH), ImVec2(box.max.x, box.min.y + headH),
                      ColorLineSubtle(), 1.0f);
        // fbody：11px muted 等宽
        DrawTextClipped(draw, MonoAt(11.0f), 11.0f * view.z,
                        ImVec2(box.min.x + padX, box.min.y + headH + 7.0f * view.z),
                        w - padX * 2.0f, ColorTextMuted(), node.sub);

        // 端口 9px 圆、垂直居中、左右各外移 5.5px（views.css:1086-1099）
        const float portR = 4.5f * view.z;
        const float portY = box.center().y;
        draw->AddCircleFilled(ImVec2(box.min.x, portY), portR, ColorPanel(), 16);
        draw->AddCircle(ImVec2(box.min.x, portY), portR, FlowStateColor(node.state), 16,
                        2.0f * view.z);
        draw->AddCircleFilled(ImVec2(box.max.x, portY), portR, ColorPanel(), 16);
        draw->AddCircle(ImVec2(box.max.x, portY), portR,
                        node.state == FlowState::Running ? ColorAccent() : ColorLineStrong(), 16,
                        2.0f * view.z);
    }

    // ---- 工具条（右上，玻璃 → --glass 实色，r10，pad 4，gap 4）----
    // views.css:1101-1113 是 right:12 top:12；FlowCanvas.jsx:133 的
    // `.canvas-tools.bl`（fill 模式）挪到左下，这里只实现右上定位。
    const Rect tools{bounds.max.x - 12.0f - 92.0f, bounds.min.y + 12.0f, bounds.max.x - 12.0f,
                     bounds.min.y + 12.0f + 32.0f};
    DrawShadowed(draw, tools.min, tools.max, 10.0f, GlassColor(), ColorLineSubtle(), 1.0f, theme::ShadowTier::Card);
    if (IconButton(draw, RectAt(tools.min.x + 4.0f, tools.min.y + 4.0f, 24.0f, 24.0f), "plus", false,
                   false, "flow-zoom-in")) {
        view.z = std::min(2.0f, view.z * 1.2f);
    }
    if (IconButton(draw, RectAt(tools.min.x + 32.0f, tools.min.y + 4.0f, 24.0f, 24.0f), "x", false,
                   false, "flow-zoom-out")) {
        view.z = std::max(0.35f, view.z / 1.2f);
    }
    if (IconButton(draw, RectAt(tools.min.x + 60.0f, tools.min.y + 4.0f, 24.0f, 24.0f), "target",
                   false, false, "flow-fit")) {
        FlowFit(nodes, bounds, fitInset, view);
    }
    // 有节点在跑时工具条下方转圈（FlowCanvas.jsx:137）
    const bool running =
        std::any_of(nodes.begin(), nodes.end(), [](const FlowNode& n) { return n.state == FlowState::Running; });
    if (running) {
        DrawRoundRect(draw, ImVec2(tools.center().x - 6.0f, tools.max.y + 4.0f),
                      ImVec2(tools.center().x + 6.0f, tools.max.y + 16.0f), 6.0f, ColorAccent());
    }
}

} // namespace shine::kit
