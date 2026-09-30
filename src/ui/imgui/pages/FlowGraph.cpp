#include "ui/imgui/pages/FlowGraph.h"

#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/PageCommon.h"

#include "comfy/ComfySession.h"

#include <cstdint>
#include <string>

namespace shine::pages {

using namespace shine::kit;

namespace {

// 出图/出片的节点图。
//
// ⚠️ 这里原来照抄 webui mock（ImageFlow.jsx / VideoFlow.jsx 的 IMAGE_NODES /
//    VIDEO_NODES），**连副行带状态一起抄了**：
//      出图 8 节点：「第 3 章 雨夜」「prompt_v3」「沈砚 · 3 视图」「openpose_v2」
//                 「seed 42 · 28 step」「latent → rgb」「CLIP 0.918」「out/S012_v3.png」
//      出片 5 节点：「S011 尾帧」「S012 首帧」「24fps · 112 帧」「2× → 48fps」「h264 · 1080p」
//    每一个字面量都与本工程无关，而「Done / Running / Todo」也是写死的 —— 于是这一屏
//    永远显示「KSampler 正在跑 seed 42」，跟你打开的是哪本书毫无关系。
//
// 现在：**节点的 title 是流程阶段的真名**（那是结构，不是数据），但**副行与状态一律
// 从真数据算**：
//   * 章 / 镜号 / 镜数 / 资产数 → BookSide() 快照
//   * 采样、生成是否在跑       → comfy::ComfySession 的真实队列
//   * 没有只读投影的步骤（VAE / 一致性校验 / 补帧 / 编码）→ kDash，**不编参数**
//     （「latent → rgb」是流程的定性描述，保留；「seed 42 · 28 step」「CLIP 0.918」
//       「2× → 48fps」「h264 · 1080p」「24fps · 112 帧」都是**看起来像实测值的编造**）
// 当前选中章里**已有成品视觉资产**的实体数（出图链路第 3 步「角色参考」的口径）。
int ReadyAssetCount(const BookSideView& book) {
    int n = 0;
    for (const BookAssetView& a : book.assets) {
        if (a.hasAsset && a.assetStatus == "READY") {
            ++n;
        }
    }
    return n;
}

// Comfy 队列里有没有正在跑的任务 —— 这是**唯一**一个真的会自己动起来的信号，
// 拿它当「运行中」的判据，比写死 `FlowState::Running` 诚实得多。
[[nodiscard]] bool ComfyHasRunning() {
    for (const comfy::QueueModel::Row& row :
         comfy::ComfySession::Instance().Queue().Snapshot()) {
        if (row.state == comfy::TaskState::Running) {
            return true;
        }
    }
    return false;
}

// 面板头状态词用的读数 —— 语义与 ComfyHasRunning() **完全一致**（有没有 Running 行），
// 只是走 `CountsSnapshot()`：上面那个要 `Snapshot()` 整表 vector 拷贝（每行带 label /
// error / traceback 几个 std::string，还要进出两把锁），而面板头是**每帧**画的，
// 为一个 bool 付整表拷贝不值当。`running > 0` 就是「有 Running 行」。
//
// ⚠️ 口径**必须**与 ComfyHasRunning() 一致：这两个头一个字说「运行中」、另一个说
//    「空闲」，界面就在自相矛盾。所以改口径时要同时改两个。
//    （实现叫 ComfyRunningForBadgeImpl：导出的同名函数在文件末尾，是它的转发。）
[[nodiscard]] bool ComfyRunningForBadgeImpl() {
    return comfy::ComfySession::Instance().Queue().CountsSnapshot().running > 0;
}

std::vector<FlowNode> MakeImageFlowNodes() {
    const BookSideView& book = BookSide();
    struct Def {
        int id;
        const char* icon;
        const char* title;
        std::string sub;
        FlowState state;
    };
    // 当前选中章：没有就显示「未选章」，不拿第一章顶替。
    std::string chapterSub = kDash;
    FlowState chapterState = FlowState::Todo;
    if (book.selectedChapter >= 0 &&
        book.selectedChapter < static_cast<int>(book.chapters.size())) {
        const BookChapterView& ch = book.chapters[static_cast<std::size_t>(book.selectedChapter)];
        chapterSub = "第 " + std::to_string(ch.ord) + " 章" +
                     (ch.title.empty() ? "" : " · " + ch.title);
        chapterState = ch.words > 0 ? FlowState::Done : FlowState::Todo;
    }
    const bool comfying = ComfyHasRunning();
    const int ready = ReadyAssetCount(book);
    const std::vector<Def> defs = {
        {1, "book", "章节", chapterSub, chapterState},
        {2, "text", "提示词", "本章 " + std::to_string(book.shots.size()) + " 镜", chapterState},
        {3, "masks", "角色参考", ready > 0 ? (std::to_string(ready) + " 个实体已出图")
                                          : std::string("还没有实体出图"),
         ready > 0 ? FlowState::Done : FlowState::Todo},
        {4, "image", "姿势骨架",
         book.shots.empty() ? kDash : (ShotCode(book.shots.front().ord) + " 起 " +
                                       std::to_string(book.shots.size()) + " 镜"),
         comfying ? FlowState::Running : FlowState::Todo},
        {5, "wand", "KSampler",
         comfying ? "队列里有任务在跑" : std::string("队列里没有任务"),
         comfying ? FlowState::Running : FlowState::Todo},
        {6, "aperture", "VAE 解码", "latent → rgb", FlowState::Todo},
        {7, "eye", "一致性校验", kDash, FlowState::Todo},
        {8, "download", "落盘", kDash, FlowState::Todo},
    };
    std::vector<FlowNode> nodes;
    nodes.reserve(defs.size());
    for (const Def& def : defs) {
        nodes.push_back(FlowNode{def.id, def.icon, def.title, def.sub, def.state, 0.0f, 0.0f});
    }
    return nodes;
}

std::vector<FlowLink> MakeImageFlowLinks() {
    // 线性主干 1→2→3→4→5→6→7→8。节点数跟着 MakeImageFlowNodes 走，不写死 8。
    std::vector<FlowLink> links;
    const int n = static_cast<int>(MakeImageFlowNodes().size());
    for (int i = 1; i < n; ++i) {
        links.push_back(FlowLink{i, i + 1});
    }
    return links;
}

std::vector<FlowNode> MakeVideoFlowNodes() {
    const BookSideView& book = BookSide();
    struct Def {
        int id;
        const char* icon;
        const char* title;
        std::string sub;
        FlowState state;
    };
    const bool comfying = ComfyHasRunning();
    const std::string head =
        book.shots.empty() ? kDash : ShotCode(book.shots.front().ord);
    const std::string tail =
        book.shots.empty() ? kDash : ShotCode(book.shots.back().ord);
    int totalSec = 0;
    for (const BookShotView& shot : book.shots) {
        totalSec += shot.durationSec;
    }
    const std::vector<Def> defs = {
        {1, "film", "首帧", head, book.shots.empty() ? FlowState::Todo : FlowState::Done},
        {2, "film", "尾帧", tail, book.shots.empty() ? FlowState::Todo : FlowState::Done},
        // ⚠️ 早先这里是「24fps · 112 帧」—— 编的。帧数**真有一个可算的来源**
        //    （本章各镜 durationSec 之和），所以报「N 镜 / 共 Xs」；采样参数本身
        //    没有只读投影，就不编。
        {3, "wand", "H3 生成",
         book.shots.empty() ? kDash
                            : (std::to_string(book.shots.size()) + " 镜 / 共 " +
                               std::to_string(totalSec) + "s"),
         comfying ? FlowState::Running : FlowState::Todo},
        {4, "zap", "RIFE 补帧", kDash, FlowState::Todo},
        {5, "encode", "编码", kDash, FlowState::Todo},
    };
    std::vector<FlowNode> nodes;
    nodes.reserve(defs.size());
    for (const Def& def : defs) {
        nodes.push_back(FlowNode{def.id, def.icon, def.title, def.sub, def.state, 0.0f, 0.0f});
    }
    return nodes;
}

std::vector<FlowLink> MakeVideoFlowLinks() {
    // 1、2 都汇进 3，之后 3→4→5。节点数跟着 MakeVideoFlowNodes 走。
    std::vector<FlowLink> links{FlowLink{1, 3}, FlowLink{2, 3}, FlowLink{3, 4}, FlowLink{4, 5}};
    return links;
}

// 建图时的**数据指纹**：只要这几个量有一个变了，节点副行 / 状态就可能不一样，图就该重建。
//
// ⚠️ 不重建的后果不是「图不刷新」这么轻：novel.db 快照是 `async::RunOnWorker` +
//    `PostToUi` **异步**回投的，而 `BuildGraph()` 原来只在 `flowNodes_.empty()` 时跑一次
//    —— 也就是工程刚打开、快照还在 worker 上跑的那一刻。于是节点图被**永久**冻结在
//    「— / 本章 0 镜 / 还没有实体出图」，哪怕数据两秒后就到了。
//    这正是「假数据」的另一种形态：不是编的，是**永远停在最空的那一刻**。
//
// ⚠️ 重建会重跑 FlowLayoutNodes ⇒ 布局归位、用户拖过的节点位置丢失。所以指纹里
//    **不放**缩放 / 选中 / 画布尺寸 —— 只有真正影响节点内容的那几项。
//    （实现叫 FlowDataKeyImpl：导出的同名函数在文件末尾，是它的转发。）
std::uint64_t FlowDataKeyImpl() {
    const BookSideView& book = BookSide();
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    const auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFFull;
            h *= 1099511628211ull;
        }
    };
    mix(book.bound ? 1u : 0u);
    mix(book.loading ? 1u : 0u);
    mix(static_cast<std::uint64_t>(book.selectedChapter));
    mix(static_cast<std::uint64_t>(book.chapters.size()));
    mix(static_cast<std::uint64_t>(book.shots.size()));
    mix(static_cast<std::uint64_t>(ReadyAssetCount(book)));
    mix(ComfyHasRunning() ? 1u : 0u);
    return h;
}

} // namespace

// 首次进入时建图并适应视图；之后由 FlowCanvas 维护坐标与缩放。
// fit 需要画布尺寸，而首帧 Draw 时才有 —— 所以用一个「待适配」标志，
// 在第一次拿到尺寸后 fit 一次（webui FlowCanvas.jsx:56-62 的 setTimeout(fit) 同理）。
namespace {
bool g_imageFlowNeedsFit = true;
bool g_videoFlowNeedsFit = true;
} // namespace

// ---- FlowGraph.h 的窄接口：只转出两页 Draw 真的要用的三个量 ----
//
// 上面那些 Make*FlowNodes / FlowDataKey / Comfy* 都留在匿名命名空间里 —— 节点表
// 是实现细节，导出就等于允许第二处定义它。页面要「重建判据」「面板头状态」「首帧
// fit」这三件事，所以只转出这三样。
std::uint64_t FlowDataKey() { return FlowDataKeyImpl(); }

bool ComfyRunningForBadge() { return ComfyRunningForBadgeImpl(); }

bool& ImageFlowNeedsFit() { return g_imageFlowNeedsFit; }
bool& VideoFlowNeedsFit() { return g_videoFlowNeedsFit; }

void ImageFlowPage::BuildGraph() {
    flowNodes_ = MakeImageFlowNodes();
    flowLinks_ = MakeImageFlowLinks();
    FlowLayoutNodes(flowNodes_, flowView_);
    flowSelected_ = 5; // 默认选中运行中的节点，对齐 webui 的初始 sel
    g_imageFlowNeedsFit = true;
    flowDataKey_ = FlowDataKey();
}

void VideoFlowPage::BuildGraph() {
    flowNodes_ = MakeVideoFlowNodes();
    flowLinks_ = MakeVideoFlowLinks();
    FlowLayoutNodes(flowNodes_, flowView_);
    flowSelected_ = 3;
    g_videoFlowNeedsFit = true;
    flowDataKey_ = FlowDataKey();
}

} // namespace shine::pages
