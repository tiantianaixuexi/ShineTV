#pragma once
// shine::pages —— 六个工作区（P5）
//
// 每页的骨架统一是 design-spec §7 的 .vw：padding 20px 24px 26px，竖排 gap16。
// 头 = 20px accent 图标 + .vw-title(18/800) + .vw-sub(12.5 muted) + 弹性空隙 + 控件。
#pragma once

#include "ui/imgui/kit/Views.h"
#include "ui/imgui/kit/Widgets.h"

#include <filesystem>
#include <string>
#include <vector>

namespace shine::pages {

// 头：图标 + 标题 + 副行 + 右侧控件区。返回内容区起点。
kit::Rect ViewHeader(kit::Rect area, ImDrawList* draw, const char* icon, const char* title,
                     const char* subtitle, kit::Rect* rightOut = nullptr);

// ---- P5.1 总控（pipeline）—— 纯数据，先做它验证数据通路 ----
void DrawOverview(kit::Rect area, ImDrawList* draw);

// 总控页绑定"当前工程根"的唯一入口（对应 Qt 版 PipelineWorkspace::SetContext）。
// 绑上以后页面的进度 / 预算 / 账本 / 停止判定才是 Runner 的真实返回值；没绑就是空态。
void BindOverviewProject(std::filesystem::path root);

// ---- P5.2 小说（novel）—— 最大的一页：8 模式标签 + 8 套视图 ----
class NovelPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);
    void setMode(int mode) { mode_ = mode; }

private:
    int mode_ = 0;
};

// ---- P5.3 资产（assets）—— 第一处真正用 src/gpu 的页面 ----
class AssetsPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);
    void setOverview(bool on) { overview_ = on; }

private:
    bool overview_ = false;
    int selected_ = 0;
};

// ---- P5.4 分镜（storyboard）----
class StoryboardPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);

private:
    int selectedShot_ = 0;
};

// ---- P5.5 出图（imageflow）— 满幅画布 + FlowCanvas ----
class ImageFlowPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);

private:
    // 首帧建图（节点定义照 webui mock）；之后坐标/缩放由 FlowCanvas 维护。
    void BuildGraph();

    int panelTab_ = 0;
    bool folded_ = false;
    std::vector<kit::FlowNode> flowNodes_;
    std::vector<kit::FlowLink> flowLinks_;
    kit::FlowView flowView_;
    int flowSelected_ = -1;
};

// ---- P5.6 出片（videoflow）----
class VideoFlowPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);

private:
    void BuildGraph();

    int panelTab_ = 0;
    std::vector<kit::FlowNode> flowNodes_;
    std::vector<kit::FlowLink> flowLinks_;
    kit::FlowView flowView_;
    int flowSelected_ = -1;
};

// ---- P4.11 项目中心（全屏，不套外壳）----
void DrawProjectHub(kit::Rect area, ImDrawList* draw);

} // namespace shine::pages
