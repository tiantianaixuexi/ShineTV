#pragma once
// shine::pages —— 项目中心（P4.11）的**私有契约**
//
// 项目中心原先是一个 899 行的 .cpp：卡片模型 + 全部 IO 操作（打开 / 移除 /
// 建目录 / 换主题）+ 三段弹窗私有实现 + 三块绘制（项目卡 / 向导 / 打开列表）
// + 页面骨架。拆分前它能拆不动的原因只有一个：三个对话框和页面骨架**共享同一份
// HubState**，而 HubState 住在匿名命名空间里。
//
// 收录标准（与 PageCommon.h / PageKpi.h 同一把尺）：**换一页也照样成立**的才放这。
// HubCard / HubState 是**这一页的模型**，别的地方不读它们 —— 所以它们留在这个私有
// 头里，而不是提到 WorkspacePages.h。
//
// ⚠️ 本页**零 BookSide / Book() 调用**（不依赖 novel.db 快照），只共用 PageCommon 的
//    版式常量与 LabelWidth。所以不 include BookData.h。
#include "project/Project.h"

#include "ui/imgui/pages/WorkspacePages.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::pages {
namespace hub {

// 卡片上要用到、但 RecentEntry 里没有的字段（都来自各自的 project.json）。
struct HubCard {
    project::RecentEntry entry;
    std::string premise;   // project.json.premise（读不到就留空，不拿模板名顶替）
    std::string tplLabel;  // 封面短标签：小说 / 影视 / 空白
    std::string tplName;   // meta 行用的模板全名
    std::string when;      // lastOpened 的相对时间
    bool readable = false; // project.json 是否读得到
    int artSeed = 0;       // 封面种子：由项目 id 稳定派生（不是循环下标）
};

// DrawProjectHub 是自由函数，没有实例可挂交互态，状态放函数内 static
// （与本文件 g_imageFlowNeedsFit 同一手法）。
struct HubState {
    project::ProjectService service;
    std::vector<HubCard> cards; // 排序后的全量
    std::vector<int> shown;     // 搜索过滤后的下标
    bool loaded = false;
    std::string query;
    std::string sort = "r"; // Segmented 的 value：r=最近打开 / n=名称
    bool wizard = false;
    int step = 0;
    std::string tpl = "novel";
    std::string name;
    std::string dir;
    std::string idea;
    bool openDlg = false;
    bool confirm = false;
    // 「点面板外关闭」这一下点击**是否算数**。只在本帧之前对话框就已经开着时
    // 才为真 —— 见 ProjectHub.cpp 里的说明（不加这道闸门，弹窗会被打开它的那
    // 一下点击立刻关掉）。
    bool dismissArmed = false;
    std::string confirmTitle;
    std::string confirmBody;
    std::string confirmOk = "确定";
    std::string pendingId;
    std::string status;
    bool statusError = false;
};

HubState& S();

// ---- IO / 索引操作（每一步都真落到 project/ 层，不编状态）----

// 重新拉最近列表（Recent() 每次都重读索引，所以只在需要时调）。
void Refresh(HubState& hub);
// 搜索过滤：项目名或一句话创意命中即可（webui 匹配 name + desc）。
void Refilter(HubState& hub);
// 打开项目：真 Open（读 project.json + 置顶索引），失败把业务层的中文 message 摆出来。
void Open(HubState& hub, const HubCard& card);
// 从最近列表移除：ProjectService 没这个接口，走 ProjectIndex（只摘登记，不删文件）。
void Remove(HubState& hub, const std::string& id);
// 主题轮转（pfoot 的 palette 按钮）。动作与 Shell::SetTheme 一致 ——
// 水墨换衬线族必须重建字体图集，否则中文缺字。
void CycleTheme(HubState& hub);
// 向导第 2 步的「浏览…」：系统选目录（Win32 边界调用，按钮触发一次即返回）。
std::string PickFolder();

// ---- 纯函数小工具（卡片与向导都要用）----

// 项目将创建到的目录：<位置>\<项目名>（名字空就还没有目标）。
std::filesystem::path TargetDir(const HubState& hub);
// 模板 id → 图标（向导第 1 步每行一个）。
const char* TemplateIcon(std::string_view templateId);
// UTF-8 字符数：设计稿的「200 字」上限按字符算，不是字节。
std::size_t CharCount(std::string_view text);

// ---- 子区域绘制 ----
//
// 三块各自成文件：卡片 / 向导 / 打开列表 + 确认。它们共享 HubState，但**没有任何
// 一块的几何依赖另一块** —— 依赖的只有状态字段，所以拆开是纯机械的。

// 项目卡：整卡可点 + pfoot 的四个入口（打开 / 资源管理器 / 更多 / 主题）。
// 返回 true = 本帧通过整卡或「打开」按钮触发了打开。
bool DrawCard(ImDrawList* draw, HubState& hub, const HubCard& card, kit::Rect bounds,
              float bodyWidth);

// 新建项目向导：模板 / 命名 / 创意 / 确认。数据源 = AllTemplates / PreviewTree / Create。
void DrawWizard(HubState& hub, kit::Rect area, ImDrawList* draw);

// 「打开…」对话框：列最近项目，点了就 Open。
void DrawOpenDialog(HubState& hub, kit::Rect area, ImDrawList* draw);

// 「从列表移除」确认框。body 是一句话 + 取消/确定两个按钮。
void DrawConfirmDialog(HubState& hub, kit::Rect area, ImDrawList* draw);

} // namespace hub
} // namespace shine::pages
