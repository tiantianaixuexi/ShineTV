#pragma once
// shine::pages —— 六个工作区（P5）
//
// 每页的骨架统一是 design-spec §7 的 .vw：padding 20px 24px 26px，竖排 gap16。
// 头 = 20px accent 图标 + .vw-title(18/800) + .vw-sub(12.5 muted) + 弹性空隙 + 控件。
#pragma once

#include "ui/imgui/kit/Views.h"
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/theme/Theme.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
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

// 阶段执行体是否已接入。未接入时全流程**真的跑不了**（业务层没有「替前端跑一个
// T 阶段」的服务接口；真执行体在 novel/，签名与 pipeline::StageExecutor 不同形状，
// 没有适配层）。顶栏的「运行」与总控页的「一键全流程」都靠这一个读数决定给不给按 ——
// 两边各写一份判断的话，迟早只改得动一边。
[[nodiscard]] bool OverviewPipelineWired();

// 「执行体未接入」这句话的**唯一一份**措辞。页面、toast、按钮提示说的必须是同一句。
[[nodiscard]] const char* StageExecutorMissingReason();

// 页面层 → 外壳的 toast 通道。页面不该认识 Shell（那是外壳的活），所以由 Shell
// 在初始化时把 `Notify` 注入进来。没注入时调用是安全的空操作。
//
// 为什么需要它：出图页工具条上那个「批量出图」按钮的业务**没有接**（业务层没有替前端
// 提交 V4 的接口）。画一个点了没反应的主按钮是最差的一种 —— 而把原因写死在页面某处
// 又会因为面板可折叠而看不见。点了弹一句 toast 是唯一能同时做到「诚实」和「原因可达」的。
void SetWorkspaceToast(std::function<void(std::string)> sink);
void WorkspaceToast(std::string message);

// ---- 工作区的**可视高度**（每帧由 Shell 写入，页面只读）----
//
// 为什么页面需要知道它：`Shell::DrawWorkspace` 给页面的布局区是**撑高**的（2400px，
// 满幅画布页则是视口高），这样页面内容超出视口时仍能被外壳滚到。但页面拿着这个
// 2400 当「我的可视区」就会把侧栏一起撑高 —— 总控页右栏因此高达 1978px，
// 于是右栏里的 ScrollRegion 变成一个 1978px 高的**布局容器**而不是视口：
// 卡片在里面从头排到尾，裁切交给外层，于是「右栏自己滚」这件事根本不存在。
//
// 页面要建**自己的视口**（侧栏、面板、列）时用这个高度收口；正文流该多长还多长。
void SetWorkspaceViewportHeight(float height);
// 尚未被 Shell 写入时返回 0 —— 页面据此退回「布局区就是视口」的旧行为。
float WorkspaceViewportHeight();

// ---- 页面**真实内容高度**（页面写、Shell 每帧读一次）----
//
// `Shell::DrawWorkspace` 给页面的布局区是写死的 2400px（满幅画布页除外），好让
// 「内容超出视口仍能被滚到」。但 2400 只是**上限**，不是页面的真实高度：页面若按
// 它铺内容，卡片就会被撑成巨型空盒子（实测总控页「账本」卡 1750px 高、只有 6 行，
// 顺带多出约 1450px 的无效滚动）。
//
// 页面画完把自己**实际用到的高度**写进来，Shell 就用它替代 2400 作为滚动区高度。
// 没写的页面（返回 0）退回 2400，行为不变 —— 这样可以逐页迁移，不是一次性大改。
void SetPageContentHeight(float height);
// 上一帧某页面自报的内容高度；0 = 没报。
float PageContentHeight();
// Shell 每帧在分发页面前清一次，避免上一页的残留值被这一页继承。
void ResetPageContentHeight();

// ---- 小说 / 资产 / 分镜：绑定"当前工程根"（照 BindOverviewProject 的模式）----
//
// 三个页面共用同一份 **novel.db 快照**：开库 + 查询全在 worker 线程
// （`async::RunOnWorker`），结果经 `async::PostToUi` 回投 UI 线程；UI 线程只读
// 那份快照，不做任何 IO。见 BookData.h 的 `Book()` / `BookState`。
//
// 绑上以后这三页的每一个数字 / 每一条列表项都来自业务层
// （novelcore::NovelGraph / NovelVisual / RunContinuityChecks）；
// 没绑、或 db 打不开、或表是空的 —— 页面显示**诚实的空态**，不补占位数字。
//
// ⚠️ 这三个函数必须定义在匿名命名空间**之外**（在 BookData.cpp），
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
    // 卷归属。`chapters.volume_id` + `volumes.title` 都是**已有的**库字段 ——
    // 侧栏树缺「卷」这一层从来不是数据层的问题，是 UI 侧没读。
    // volumeTitle 为空 = 这一章没归到任何卷（或老工程的 volumes 表是空的），
    // 侧栏按「未归卷」如实显示，不编卷名。
    int volumeId = 0;
    std::string volumeTitle;
};

// 资产工作区侧栏的一行。kind 是**实体 kind 的英文原文**（entities.kind，
// `namespace kind` 里有 30 个取值，库里无 CHECK 约束）——不要在这里翻成中文，
// 分类由消费者按 AssetKindLabel() 走，侧栏与资产页共用那一份映射。
struct BookAssetView {
    int index = 0;  // 在 BookSideView::assets 里的下标；**筛选后的树按下标回指这里**
    int entityId = 0;  // entities.id（别和 index 混：index 是位置，entityId 才是库主键）
    std::string kind;
    std::string name;
    std::string summary;
    bool hasAsset = false;       // 查得到主视觉资产（entities 有行但 visual_assets 没有）
    std::string assetStatus;     // visual_assets.status 八值原文
    int layers = 0;              // visual_artifacts 行数
    int layersDone = 0;          // 其中 status == "DONE"
    bool degraded = false;       // 任一产物走过降级
    // 色调与中文标签都由 AssetTone() 从 assetStatus 映射，**在重建时算好存进来**。
    // 不让消费者各自映射一遍：侧栏的状态点、检查器的状态行、资产页的 Tag 三处要同一份，
    // 而 AssetTone() 定义在 BookData.cpp 的匿名命名空间里，Shell 够不着。
    theme::Tone tone = theme::Tone::Idle;
    std::string statusLabel;
};

// 检查器「关联」段的数据。设计稿那三个 tag（伏笔 #3 / 场景 12 / 镜 S05）是
// Shell.jsx:173-179 的**内联字面量**，没有数据结构、不可点。换成真数据后取不到的组
// 就不出 tag —— 空数组 / 0 序 = 这一组没有，不编一个占位 tag。
struct BookRelationView {
    std::string shotCode;                 // 选中镜的镜码；没选镜则空
    int sceneOrd = 0;                     // 场序（0 = 这一项不落在任何场上）
    std::string sceneTitle;               // 场标题，可空
    std::vector<std::string> foreshadows; // 伏笔标题，来自 scene_foreshadows
};

struct BookSideView {
    bool bound = false;    // 绑没绑工程
    bool loading = false;  // worker 正在取
    std::string error;     // 业务层返回的中文原因；空 = 没出错
    int selectedChapter = 0;  // chapters 下标
    int selectedShot = 0;     // shots 下标
    int selectedAsset = 0;    // assets 下标
    std::vector<BookChapterView> chapters;
    // ⚠️ 场 / 镜只对**当前选中章**取过（与 Qt 版「按章选镜」口径一致），
    //    所以这是那一章的镜，不是全书。别的章的镜要等选中它之后才会有。
    std::vector<BookShotView> shots;
    // 资产是**全量**（与 ListEntities 的取法一致），**不按 kind 预筛**：
    // 筛选是工作区 UI 态（见 BookKindFilter），筛完的树按下标回指这里。
    std::vector<BookAssetView> assets;
    BookRelationView relation;  // 随选中项重建，给检查器「关联」段
    // 每章的 V1–V7 阶段产物数，**下标与 chapters 一一对应**（全 0 = 这一章没跑过视觉链）。
    //
    // 真值源：`novel::ListStageArtifacts(chapterId, "V<n>")` 的返回条数 ——
    // 这个只读接口一直存在（`src/novel/NovelVisual.h:242-243`），表 `stage_artifacts`
    // 也带 `chapter_id`。所以「章节 × 阶段」这个维度在**视觉链**上是数据层就有的，
    // 并不是缺口；早先的判断（「章节维度在数据层根本不存在」）只对 T 链成立 ——
    // T 链的 `LedgerEntry` / `work/T1.json` 确实不带章节。
    std::vector<std::array<int, 7>> chapterVStages;
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

// 资产工作区的 kind 筛选（设计稿的 entityKind，Shell.jsx:522）。
//
// ⚠️ 它**不是**快照的一部分：设计稿里这是侧栏与主区网格**共享**的筛选态
//    （Assets.jsx:130 和 Shell.jsx:523 读同一个 entityKind），属于工作区 UI 态。
//    放进 BookSideView 会被 RebuildBookSide 整块重建冲掉 —— 就是上一轮那个
//    「loading 翻真但视图没跟上」的同一类坑。kind 空串 = 全部。
void SetBookKindFilter(std::string kind);
[[nodiscard]] const std::string& BookKindFilter();
void SelectBookAsset(int index);

// entities.kind / visual_assets.kind → 中文分类名。侧栏树的分组标签、chip 文案、
// 资产页的「类别」行**共用这一份**。库表里没有的中文映射按表外原样回显，不编名字。
// 实现见 BookData.cpp（与该文件里已有的 KindLabel 是同一份，不是第二份）。
[[nodiscard]] std::string AssetKindLabel(const std::string& kind);

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
    // 侧栏 kind 树点叶子后驱动主区：selected_ 原来是私有的，外壳点不到 ——
    // 结果侧栏高亮与主区选中永远是两回事（同一类「只显示不联动」）。
    // index 是 **BookSideView::assets 的下标**（与 s.assets 同序），不是筛后树的序号。
    void setSelected(int index) { selected_ = index; }

private:
    bool overview_ = false;
    int selected_ = 0;
};

// ---- P5.4 分镜（storyboard）----
class StoryboardPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);
    // ⚠️ 这里原来有个 `selectedShot_` 成员 + `setSelected`，**零读点** —— 真正被读的是
    //    `BookState::selectedShot`。两个「选中态」并存，取证的公开入口写进了没人读的那个，
    //    拍出来的图与默认态逐字节相同却仍记 saved。已删；选中态只有 `SelectBookShot` 一条路。

private:
};

// ---- P5.5 出图（imageflow）— 满幅画布 + FlowCanvas ----
class ImageFlowPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);
    // 取证用：右侧面板页签（0 参数 / 1 产物）与画布折叠态。这两个都只能靠点 UI 切换，
    // 以前取证进不去 —— 于是第二个页签里的内容从来没被拍过。
    void setPanelTab(int tab) { panelTab_ = tab; }
    void setFolded(bool on) { folded_ = on; }

private:
    // 建图 + 重新布局。**不是**只在首帧调一次 —— 节点副行与状态现在读真数据
    // （当前章、镜数、已出图实体数、Comfy 队列），而 novel.db 快照是**异步**回投的：
    // 首帧建图时它多半还是空的，于是节点会永久停在「— / 本章 0 镜」。
    // 触发条件见 Draw 里的数据指纹比较。
    void BuildGraph();

    int panelTab_ = 0;
    bool folded_ = false;
    std::vector<kit::FlowNode> flowNodes_;
    std::vector<kit::FlowLink> flowLinks_;
    kit::FlowView flowView_;
    int flowSelected_ = -1;
    // 建图时的数据指纹。变了就重建图 —— 否则真数据永远上不了屏。
    std::uint64_t flowDataKey_ = 0;
};

// ---- P5.6 出片（videoflow）----
class VideoFlowPage {
public:
    void Draw(kit::Rect area, ImDrawList* draw);
    // 取证用：右侧面板页签（同上）。
    void setPanelTab(int tab) { panelTab_ = tab; }

private:
    void BuildGraph();

    int panelTab_ = 0;
    std::vector<kit::FlowNode> flowNodes_;
    std::vector<kit::FlowLink> flowLinks_;
    kit::FlowView flowView_;
    int flowSelected_ = -1;
    std::uint64_t flowDataKey_ = 0;
};

// ---- P4.11 项目中心（全屏，不套外壳）----
void DrawProjectHub(kit::Rect area, ImDrawList* draw);
// 项目中心里三个对话框（新建向导 / 打开 / 确认）**有没有开着**。
//
// 给浮层按钮判据当**前置条件**用：判据要验「对话框里的按钮收得到鼠标」，
// 就必须先确认对话框真的开着。少了这一步，判据会在「压根没开」的状态下去扫
// 一片没有遮罩的界面，照常扫到按钮 id，于是报通过 —— 而它本该抓的缺陷
// （遮罩抢走 HoveredId）正是在「开着」时才发生的。
[[nodiscard]] bool HubDialogOpen();

} // namespace shine::pages
