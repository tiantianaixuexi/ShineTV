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

// ---- 小说 / 资产 / 分镜：绑定"当前工程根"（照 BindOverviewProject 的模式）----
//
// 三个页面共用同一份 **novel.db 快照**：开库 + 查询全在 worker 线程
// （`async::RunOnWorker`），结果经 `async::PostToUi` 回投 UI 线程；UI 线程只读
// 那份快照，不做任何 IO。见 WorkspaceB.cpp 的 `Book()` / `BookState`。
//
// 绑上以后这三页的每一个数字 / 每一条列表项都来自业务层
// （novelcore::NovelGraph / NovelVisual / RunContinuityChecks）；
// 没绑、或 db 打不开、或表是空的 —— 页面显示**诚实的空态**，不补占位数字。
//
// ⚠️ 这三个函数必须定义在匿名命名空间**之外**（在 WorkspaceB.cpp），
//    否则 Shell 链接不到。Shell 在 SetProjectRoot 里与 BindOverviewProject 一起调。
void BindNovelProject(std::filesystem::path root);
void BindAssetsProject(std::filesystem::path root);
void BindStoryboardProject(std::filesystem::path root);

// ---- P4 侧栏树 / 检查器：外层外壳要读的那份 novel.db 快照 ----
//
// 小说的内容区已经持有真实数据（章节 / 场 / 镜 / 阶段产物 / 连续性），但它住在一
// 个 .cpp 的匿名命名空间里，外面够不着 —— 于是侧栏树与检查器只能给诚实空态。
//
// 这里开一个**只读视图**而不是把 BookState 暴露出去：外壳不持有数据、也不做 IO，
// 拿到的应该是一份纯数据拷贝。视图在 ApplyBook（worker 回投、跑在 UI 线程）里
// 重建，getter 只返回引用，所以每帧读它没有拷贝成本。
struct BookShotView {
    int ord = 0;        // 章内镜序（1 起）
    int sceneOrd = 0;   // 所属场的序号
    std::string action;
    std::string expression;
    std::string mood;
    std::string dialogue;
    std::string narration;
    std::string durationNote;
    std::string canonStatus;
    double durationSec = 0.0;
};

struct BookChapterView {
    int ord = 0;         // 章序（1 起）
    std::string title;
    std::string status;
    int words = 0;
};

struct BookSideView {
    bool bound = false;    // 绑没绑工程
    bool loading = false;  // worker 正在取
    std::string error;     // 业务层返回的中文原因；空 = 没出错
    int selectedChapter = 0;  // chapters 下标
    int selectedShot = 0;     // shots 下标
    std::vector<BookChapterView> chapters;
    // ⚠️ 场 / 镜只对**当前选中章**取过（与 Qt 版「按章选镜」口径一致），
    //    所以这是那一章的镜，不是全书。别的章的镜要等选中它之后才会有。
    std::vector<BookShotView> shots;
};

// 只读视图。UI 线程读，不做任何 IO。
[[nodiscard]] const BookSideView& BookSide();

// 镜码 S001 —— ShotRow 库里**没有** code 字段（对照 NovelVisual.h 的 ShotRow 定义），
// 这是按章内镜序拼出来的显示码。侧栏树 / 故事板卡片 / 检查器标题三处必须用同一份实现：
// 写成 "S1" / "镜 #1" / "S001" 三种，同一个镜在不同面板上是三个名字，比缺一个码更糟。
inline std::string ShotCode(int ord) {
    std::string digits = std::to_string(ord);
    while (digits.size() < 3) {
        digits.insert(digits.begin(), '0');
    }
    return "S" + digits;
}

// 侧栏点击 → 改选中项。
// 选章会按该章重新取镜（走 worker，generation 丢弃过期结果）；选镜只改下标，不重取。
// index 越界会被夹回合法范围，越界时不做任何事。
void SelectBookChapter(int index);
void SelectBookShot(int index);

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
