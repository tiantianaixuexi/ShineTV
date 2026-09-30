#include "ui/imgui/pages/WorkspacePages.h"

#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelContinuity.h"
#include "novel/NovelGraph.h"
#include "novel/NovelTypes.h"
#include "novel/NovelVisual.h"
#include "pipeline/StageMachine.h"
#include "project/Project.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "ui/imgui/kit/Scroll.h"
#include "util/Encoding.h"
#include "util/Shell.h"
#include "util/Strings.h"
#include "visual/CharacterAsset.h"

#include <objbase.h>  // 必须在 windows.h（util/Encoding.h 已经带进来）之后
#include <shlobj.h>   // SHBrowseForFolderW：向导第 2 步的「浏览…」

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::pages {
namespace {

using namespace shine::kit;

constexpr float kGap = 16.0f;

// 业务层没有这一项时的**诚实**占位（区别于编一个 0 / 写死一个哈希）。
//
// ⚠️ 定义**提前**到匿名命名空间开头：出图 / 出片的节点图（下面那两个 Make*FlowNodes）
//    要用它 —— 「这一步没有只读投影」就写 kDash，而不是编一个看起来像实测值的参数
//    （早先的「seed 42 · 28 step」「CLIP 0.918」「2× → 48fps」就是这么来的）。
constexpr const char* kDash = "—";

float LabelWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, 1e9f, 0.0f, text, text + std::strlen(text)).x;
}

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

// visual_assets.status 八值 → 流程节点的三态。只在**有行**时才算得出 Done/Running。
FlowState StageStateFromStatus(const std::string& status) {
    if (status == "READY") {
        return FlowState::Done;
    }
    if (status == "PROMPTING" || status == "REF_READY" || status == "SHEET_READY" ||
        status == "WARDROBE_READY" || status == "GENERATING") {
        return FlowState::Running;
    }
    return FlowState::Todo;  // PENDING / FAILED / STALE / 空
}

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
std::uint64_t FlowDataKey() {
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

// ================================================================ 三页共用的 novel.db 真实数据源
//
// 小说 / 资产 / 分镜三页读的是同一本 novel.db。线程模型照 WorkspaceA.cpp 的总控页：
//   * worker 线程开库 + 查询（`async::RunOnWorker`），UI 线程只读下面那份**快照**；
//   * worker 用 `async::PostToUi` 回投，UI 线程全程不碰 sqlite 句柄、不做文件 IO；
//   * 取不到就是取不到 —— 页面显示空态 + 业务层返回的中文原因，不补占位数字。
//
// ⚠️ 已知的业务层缺口（如实标注，不在 UI 侧绕过）：
//   · shots 表**没有** `spatial` / `transition` / `performance` 专列 —— V9 只把它们
//     存盘到 `work/ch<NNN>/storyboard.json`（`11` §2.2）。所以分镜页「空间」一行
//     只能显示磁盘位置 + 空态，不能从库里取到一个假的值。
//   · `Ledger` 没有章节维度 —— 总控页的账本是全书一本，分镜页不假装能按章切。
//   · 某些 service 要先 `Configure(root)` / 开库才有数据：这里等价于**先打开
//     novel.db**（`root/db/novel.db`，对应 project::ProjectRef::dbPath）。
//   · `visual_assets` 没有 `ListVisualAssets` API —— 资产页照 Qt 版 AssetPageModel
//     的同一口径，逐实体 `FindAssetByEntity` 取主视觉资产；查不到 = 该实体还没有资产。
namespace {
using novelcore::RowId;

struct BookChapter {  // 小说页「章节」模式 + 分镜页的当前章
    RowId id = 0;
    // 卷归属。⚠️ `ChapterRow` 早就有 `volume_id`（`src/novel/NovelTypes.h:115`），
    //    `ListChapters` 的 SELECT 也带了它（`src/novel/NovelGraph.cpp:410,421`）——
    //    只是早先往这个结构体搬字段时**漏了**，于是「卷」这一层在 UI 侧凭空消失。
    //    顺带纠正一个归因：卷层级**并不缺 shine_core 接口**，`chapters.volume_id` 与
    //    独立的 `volumes` 表都在；缺的只是 UI 侧没读。设计稿的「书 / 卷 / 章」三层
    //    一直只画得出两层，根因就在这两行，不是「没有 ListVolumes」。
    RowId volumeId = 0;
    std::string volumeTitle;  // 来自 volumes 表；查不到 = 这一行是「未归卷」
    int ord = 0;
    std::string title;
    std::string status;
    std::string summary;
    std::string body;
    int words = 0;
    std::int64_t updated = 0;
};

struct BookShot {  // 分镜页的镜头（shots 表一行）
    RowId id = 0;
    RowId sceneId = 0;
    int sceneOrd = 0;
    int ord = 0;
    std::string action;
    std::string expression;
    std::string mood;
    std::string dialogue;
    std::string narration;
    std::string durationNote;
    double durationSec = 0.0;  // timeline_json.duration_s；<=0 = 没有
    RowId cameraId = 0;
    RowId lightingId = 0;
    std::string timelineJson;
    std::string canonStatus;
};

// 检查器「关联」段的**场 → 伏笔**链路（scenes + scene_foreshadows + foreshadows）。
// ⚠️ 住在这个匿名命名空间里，Shell 看不到：它只需要 RebuildBookSide 产出的
//    BookRelationView。这三张表在 shots 之外，镜行本身没有场标题与伏笔，
//    所以在 worker 侧先按场聚好，回投时只带纯数据。
struct BookSceneLink {
    RowId id = 0;
    int ord = 0;  // scenes.ord；0 = 取不到场序
    std::string title;
    std::vector<std::string> foreshadows;  // 伏笔标题（不是 id —— id 不是可读文案）
};

struct BookAsset {  // 资产页一行（实体 + 它的主视觉资产 + 形象层进度）
    RowId entityId = 0;
    std::string kind;
    std::string name;
    std::string summary;
    std::string entityStatus;
    bool hasAsset = false;
    std::string assetName;
    std::string canonStatus;
    std::string assetStatus;  // PENDING/PROMPTING/…/READY/FAILED/STALE
    std::string sheetRelPath;
    int layers = 0;      // visual_artifacts 行数
    int layersDone = 0;  // 其中 status == DONE
    bool degraded = false;
};

struct BookContinuity {  // V8 的真实三态结论（pass / fail / unverified）
    bool ran = false;
    int shotsSeen = 0;
    int pairsChecked = 0;
    int failed = 0;
    int unverified = 0;
    std::vector<novelcore::ContinuityIssue> issues;
    std::vector<std::string> notes;
};

struct BookState {
    std::filesystem::path root;
    std::string dbPath;  // UTF-8，给界面显示用
    bool bound = false;
    bool loading = false;
    std::string error;  // 业务层返回的中文原因；空 = 没出错

    std::vector<BookChapter> chapters;
    // 章节 × V1–V7 的产物计数，与 chapters 同下标（全 0 = 没跑过视觉链）。
    // 取自 ListStageArtifacts 的返回条数 —— 这个维度在**视觉链**上数据层就有。
    std::vector<std::array<int, 7>> chapterVStages;
    std::vector<BookAsset> assets;
    std::vector<BookShot> shots;  // 当前章
    std::vector<BookSceneLink> sceneLinks;  // 当前章：场序 + 场标题 + 该场伏笔标题
    BookContinuity continuity;    // 当前章
    // V1–V7 有阶段产物 / V8 跑过连续性 —— 逐项来自真实表，不整条一起亮
    bool vStage[8] = {false, false, false, false, false, false, false, false};

    int selectedChapter = 0;  // chapters 下标
    int selectedShot = 0;     // shots 下标
    int selectedAsset = 0;    // assets 下标（与只读视图的 selectedAsset 同口径）

    [[nodiscard]] RowId chapterId() const {
        return selectedChapter >= 0 && selectedChapter < static_cast<int>(chapters.size())
                   ? chapters[static_cast<std::size_t>(selectedChapter)].id
                   : 0;
    }
    [[nodiscard]] const BookChapter* chapter() const {
        return selectedChapter >= 0 && selectedChapter < static_cast<int>(chapters.size())
                   ? &chapters[static_cast<std::size_t>(selectedChapter)]
                   : nullptr;
    }
    [[nodiscard]] const BookShot* shot() const {
        return selectedShot >= 0 && selectedShot < static_cast<int>(shots.size())
                   ? &shots[static_cast<std::size_t>(selectedShot)]
                   : nullptr;
    }
};

BookState& Book() {
    static BookState state;
    return state;
}

// 只在 UI 线程读写的重载代号：worker 回投时用它丢弃过期结果。
std::uint64_t g_bookGen = 0;

// timeline_json = {"duration_s":N,…}。只取这一个数，取不到返回 <=0（不猜、不写死 6.0）。
double DurationFromTimeline(const std::string& json) {
    const std::size_t key = json.find("\"duration_s\"");
    if (key == std::string::npos) {
        return 0.0;
    }
    std::size_t i = json.find(':', key);
    if (i == std::string::npos) {
        return 0.0;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
        ++i;
    }
    double value = 0.0;
    bool any = false;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10.0 + static_cast<double>(json[i] - '0');
        ++i;
        any = true;
    }
    if (i < json.size() && json[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
            value += static_cast<double>(json[i] - '0') * scale;
            scale *= 0.1;
            ++i;
            any = true;
        }
    }
    return any ? value : 0.0;
}

std::string TimeText(std::int64_t epochSeconds) {
    if (epochSeconds <= 0) {
        return kDash;
    }
    const std::time_t raw = static_cast<std::time_t>(epochSeconds);
    std::tm parts{};
    if (localtime_s(&parts, &raw) != 0) {
        return kDash;
    }
    char buf[32];
    return std::strftime(buf, sizeof(buf), "%m-%d %H:%M", &parts) == 0 ? kDash : std::string(buf);
}

// 章状态（chapters.status 规范四值：draft|writing|review|done）→ 标签 + 色调。
void ChapterTone(const std::string& status, std::string& label, theme::Tone& tone) {
    if (status == "writing") {
        label = "写作中";
        tone = theme::Tone::Busy;
    } else if (status == "review") {
        label = "待评审";
        tone = theme::Tone::Warn;
    } else if (status == "done") {
        label = "已完成";
        tone = theme::Tone::Ok;
    } else {
        label = status.empty() ? "未知" : status;
        tone = theme::Tone::Idle;
    }
}

// VisualAssetRow::status 的八值生产态（`11` §2.6）→ 标签 + 色调。
void AssetTone(const std::string& status, std::string& label, theme::Tone& tone) {
    if (status == "READY") {
        label = "就绪";
        tone = theme::Tone::Ok;
    } else if (status == "FAILED") {
        label = "失败";
        tone = theme::Tone::Danger;
    } else if (status == "STALE") {
        label = "已过期";
        tone = theme::Tone::Warn;
    } else if (status == "PROMPTING") {
        label = "生成提示词";
        tone = theme::Tone::Busy;
    } else if (status == "REF_READY") {
        label = "参考图就绪";
        tone = theme::Tone::Info;
    } else if (status == "SHEET_READY") {
        label = "设定图就绪";
        tone = theme::Tone::Info;
    } else if (status == "WARDROBE_READY") {
        label = "服装就绪";
        tone = theme::Tone::Info;
    } else {
        label = "待处理";
        tone = theme::Tone::Idle;
    }
}

// ⚠️ entities.kind → 中文分类名的映射**不在这里**：它必须导出给外壳
//    （WorkspacePages.h 的 AssetKindLabel），所以定义在匿名命名空间**之外**、
//    本文件靠后那一节。资产页卡片与详情里的调用点也走同一份，不写第二张表。

// ---- worker 侧：开库 + 查询，产出一份**纯数据**快照（无句柄）----
// wantChapter = 用户选中的章下标（-1 = 取第一章）。镜头 / 阶段产物 / 连续性
// 都只对这一章取 —— 与 Qt 版 StoryboardWorkspace「按章选镜」的口径一致。
void LoadBook(const std::filesystem::path& root, int wantChapter, BookState& out) {
    out = BookState{};
    out.root = root;
    out.bound = true;
    const auto dbPath = root / "db" / "novel.db";
    out.dbPath = util::PathToUtf8(dbPath);

    db::sqlite::Database db;
    if (auto opened = db.Open({.path = dbPath, .readOnly = true, .create = false}); !opened) {
        out.error = "打不开小说库：" + opened.error().message;
        return;
    }
    novelcore::NovelGraph graph(db);
    novelcore::NovelVisual visual(db);

    // —— 卷（设计稿侧栏树的中间层）——
    //
    // ⚠️ 这里**没有**用 NovelGraph —— 因为它没有 ListVolumes。
    //    卷表只有两列（id/title/ord/summary）且就在同一个 db 上，而 UI 层本来就已经
    //    持有一个只读句柄（上面 `db.Open({.readOnly=true})`），一条 SELECT 就够。
    //    不为此去动 shine_core，也不在 UI 侧另发明一套查询封装 —— 只用 db 层
    //    已经在用的 Prepare/Step/ColumnText。
    //
    // 查不到**不算错误**：老工程的 volumes 表可能是空的。那时每章的 volumeTitle 为空，
    // 侧栏按「未归卷」分组如实显示，而不是伪造卷名。
    std::map<RowId, std::string> volumeTitles;
    if (auto st = db.Prepare("SELECT id,title FROM volumes ORDER BY ord")) {
        while (st->Step() == db::sqlite::StepResult::Row) {
            volumeTitles.emplace(static_cast<RowId>(st->ColumnInt(0)), st->ColumnText(1));
        }
    }

    // —— 章（小说页 + 分镜页的当前章）——
    auto chapters = graph.ListChapters(2000);
    if (!chapters) {
        out.error = "读取章节失败：" + chapters.error().message;
        return;
    }
    for (const novelcore::ChapterRow& c : *chapters) {
        BookChapter row;
        row.id = c.id;
        row.volumeId = c.volume_id;
        if (const auto it = volumeTitles.find(c.volume_id); it != volumeTitles.end()) {
            row.volumeTitle = it->second;
        }
        row.ord = c.ord;
        row.title = c.title;
        row.status = c.status;
        row.summary = c.summary;
        row.body = c.body;
        row.words = c.words;
        row.updated = c.updated;
        out.chapters.push_back(std::move(row));
    }

    // 章节 × V1–V7 的产物计数（全书）。真值源是 `ListStageArtifacts` 的返回条数。
    //
    // 逐章 × 逐阶段各查一次：N 章就是 7N 次预编译查询，跑在 worker 上，每次微秒级，
    // 且语义直接来自业务层接口 —— 不另写一条 GROUP BY 的 SQL，避免「界面算的」和
    // 「业务层算的」两套口径（上一轮刚吃过一次：一个空执行体让真通路算出了假结果）。
    out.chapterVStages.resize(out.chapters.size());
    for (std::size_t ci = 0; ci < out.chapters.size(); ++ci) {
        for (int v = 1; v <= 7; ++v) {
            const std::string code = "V" + std::to_string(v);
            if (auto arts = visual.ListStageArtifacts(out.chapters[ci].id, code)) {
                out.chapterVStages[ci][static_cast<std::size_t>(v - 1)] =
                    static_cast<int>(arts->size());
            }
        }
    }

    // —— 资产页：实体 + 主视觉资产 + 形象层进度 ——
    // 同 Qt 版 AssetPageModel::RefreshAssets 的口径；查不到资产 = 该实体还没有。
    if (auto entities = graph.ListEntities({}, {}, 2000)) {
        for (const novelcore::EntityRow& e : *entities) {
            BookAsset row;
            row.entityId = e.id;
            row.kind = e.kind;
            row.name = e.name;
            row.summary = e.summary;
            row.entityStatus = e.status;
            if (auto found = visual.FindAssetByEntity(e.id)) {
                row.hasAsset = true;
                row.assetName = found->name;
                row.canonStatus = found->canon_status;
                row.assetStatus = found->status;
                row.sheetRelPath = found->sheet_rel_path;
                if (auto arts = visual.ListArtifacts(found->id)) {
                    row.layers = static_cast<int>(arts->size());
                    for (const novelcore::VisualArtifactRow& art : *arts) {
                        if (art.status == "DONE") {
                            ++row.layersDone;
                        }
                        if (art.degraded) {
                            row.degraded = true;
                        }
                    }
                }
            }
            out.assets.push_back(std::move(row));
        }
    } else {
        out.error = "读取实体失败：" + entities.error().message;
        return;
    }

    // —— 分镜页：当前章的场 / 镜头 / 阶段产物 / 连续性 ——
    if (out.chapters.empty()) {
        return;  // 没有章 = 没有场与镜头；空态由页面显示
    }
    out.selectedChapter = std::clamp(wantChapter, 0, static_cast<int>(out.chapters.size()) - 1);
    const RowId chapterId = out.chapters[static_cast<std::size_t>(out.selectedChapter)].id;

    std::vector<novelcore::SceneRow> scenes;
    if (auto listed = graph.ListScenes(chapterId)) {
        scenes = std::move(*listed);
    }
    auto sceneOrd = [&scenes](RowId id) {
        for (const novelcore::SceneRow& s : scenes) {
            if (s.id == id) {
                return s.ord;
            }
        }
        return 0;
    };

    // —— 检查器「关联」段的伏笔：场 → scene_foreshadows → foreshadows.title ——
    // ⚠️ ListOpenForeshadows 只给未回收的那一档（PLANNED|PLANTED|DEVELOPING，
    //    见 NovelGraph.h 的注释）：已经 REVEALED / RESOLVED 的伏笔在
    //    foreshadows 表里有行、但这里取不到标题。这是业务层的口径，UI 侧照实
    //    呈现 —— 不把 id 当标题显示，也不绕开它回表再查一次。
    // ⚠️ 伏笔表可能是空的（没跑过 T9 伏笔计划）：那就是空，不造数据。
    std::map<RowId, std::string> foreshadowTitle;
    if (auto opened = graph.ListOpenForeshadows()) {
        for (const novelcore::ForeshadowRow& f : *opened) {
            if (!f.title.empty()) {
                foreshadowTitle[f.id] = f.title;
            }
        }
    }
    for (const novelcore::SceneRow& sc : scenes) {
        BookSceneLink link;
        link.id = sc.id;
        link.ord = sc.ord;
        link.title = sc.title;
        if (auto rows = graph.ListSceneForeshadows(sc.id)) {
            for (const novelcore::SceneForeshadowRow& fr : *rows) {
                // 查不到标题就**跳过这一条**：显示一串 id 不如什么都不显示。
                if (const auto it = foreshadowTitle.find(fr.foreshadowing_id);
                    it != foreshadowTitle.end()) {
                    link.foreshadows.push_back(it->second);
                }
            }
        }
        out.sceneLinks.push_back(std::move(link));
    }

    if (auto listed = visual.ListShotsByChapter(chapterId)) {
        for (const novelcore::ShotRow& s : *listed) {
            BookShot row;
            row.id = s.id;
            row.sceneId = s.scene_id;
            row.sceneOrd = sceneOrd(s.scene_id);
            row.ord = s.ord;
            row.action = s.action;
            row.expression = s.expression;
            row.mood = s.mood;
            row.dialogue = s.dialogue;
            row.narration = s.narration;
            row.durationNote = s.duration_note;
            row.durationSec = DurationFromTimeline(s.timeline_json);
            row.cameraId = s.camera_id;
            row.lightingId = s.lighting_id;
            row.timelineJson = s.timeline_json;
            row.canonStatus = s.canon_status;
            out.shots.push_back(std::move(row));
        }
    }

    // V1–V7：逐阶段查 stage_artifacts，**有产物才算 done**，没有就是 todo。
    for (int v = 1; v <= 7; ++v) {
        const std::string code = "V" + std::to_string(v);
        if (auto arts = visual.ListStageArtifacts(chapterId, code)) {
            out.vStage[static_cast<std::size_t>(v - 1)] = !arts->empty();
        }
    }

    // V8：真实三态结论。⚠️ RunContinuityChecks 会把报告落盘到
    // `work/ch<NNN>/v08_continuity.json`（该函数的既定行为），本调用在 worker 上。
    // 它的 unverified 是「数据不足，不算通过也不算失败」—— 页面照实显示，不粉饰成通过。
    novelcore::ContinuityOutcome outcome =
        novelcore::RunContinuityChecks(db, chapterId, util::PathToUtf8(root));
    out.continuity.ran = true;
    out.continuity.shotsSeen = outcome.shots_seen;
    out.continuity.pairsChecked = outcome.pairs_checked;
    out.continuity.failed = outcome.failed;
    out.continuity.unverified = outcome.unverified;
    out.continuity.issues = std::move(outcome.issues);
    out.continuity.notes = std::move(outcome.notes);
    out.vStage[7] = outcome.error.empty();  // V8 跑出报告才算数；error 非空 = 没跑成
}

// P4 外壳（Shell 侧栏树 / 检查器）读的那份**只读视图**。BookState 住在这个匿名
// 命名空间里，外面够不着 —— 所以不把 BookState 暴露出去，而是每次快照落地时重建
// 一份纯数据拷贝：外壳不持有数据、也不做 IO。函数内 static（照 Book() 的写法）
// 持有，getter 返回引用，所以外壳每帧读它没有拷贝成本。
BookSideView& BookSideCache() {
    static BookSideView view;
    return view;
}

// 资产工作区的 kind 筛选存处。⚠️ 必须是**函数内 static**，不能是 BookSideView 的
// 字段：视图每次 RebuildBookSide 都被整块重建（`v = BookSideView{}`），
// 存在里面必被冲掉 —— 症状是筛完立刻弹回「全部」。
// 值是 entities.kind 的**英文原文**（空串 = 全部），不存中文标签：分组名走
// AssetKindLabel()，筛选值必须与 BookSideView::assets 的 kind 同源。
std::string& BookKindFilterSlot() {
    static std::string kind;
    return kind;
}

// 从刚落地的快照重建视图。
// ⚠️ 只能读**合并后**的 s：ApplyBook 里 `s = std::move(next)` 之后 next 已经被搬空，
//    这时候再遍历 next.chapters 只会拿到空列表（症状：侧栏树永远只有一行空态）。
//    s 这时是最终态：selectedChapter 是 worker 按 wantChapter 定的、selectedShot 已夹回
//    新快照的范围、loading 已置 false。
void RebuildBookSide(const BookState& s) {
    BookSideView& v = BookSideCache();
    v = BookSideView{};  // 先清空：换工程 / 库打不开时不能把上一本的章挂在新工程上
    v.bound = s.bound;
    v.loading = s.loading;
    v.error = s.error;
    v.selectedChapter = s.selectedChapter;
    v.selectedShot = s.selectedShot;
    v.chapters.reserve(s.chapters.size());
    for (const BookChapter& c : s.chapters) {
        BookChapterView row;
        row.ord = c.ord;
        row.title = c.title;
        row.status = c.status;
        row.words = c.words;
        row.volumeId = static_cast<int>(c.volumeId);
        row.volumeTitle = c.volumeTitle;
        v.chapters.push_back(std::move(row));
    }
    // 章节 × V 阶段矩阵原样搬过去（worker 已经查完，这里只做拷贝）。
    v.chapterVStages = s.chapterVStages;
    // ⚠️ 只有**当前选中章**的镜（shots 与 BookState.shots 同口径），不是全书。
    v.shots.reserve(s.shots.size());
    for (const BookShot& shot : s.shots) {
        BookShotView row;
        row.ord = shot.ord;
        row.sceneOrd = shot.sceneOrd;
        row.action = shot.action;
        row.expression = shot.expression;
        row.mood = shot.mood;
        row.dialogue = shot.dialogue;
        row.narration = shot.narration;
        row.durationNote = shot.durationNote;
        row.canonStatus = shot.canonStatus;
        row.durationSec = shot.durationSec;  // <=0 = timeline_json 里没有，别当成 0 秒
        v.shots.push_back(std::move(row));
    }
    // ---- 资产工作区侧栏：全量实体，**不按 kind 预筛** ----
    // ⚠️ 筛选是工作区 UI 态（BookKindFilterSlot），筛完的树拿着这里的
    //    BookAssetView::index 回指，所以 index 必须**等于** BookState::assets
    //    里的下标 —— 一旦这里再排序或再筛，侧栏点谁都会点错。
    v.assets.reserve(s.assets.size());
    for (std::size_t i = 0; i < s.assets.size(); ++i) {
        const BookAsset& asset = s.assets[i];
        BookAssetView row;
        row.index = static_cast<int>(i);
        row.entityId = static_cast<int>(asset.entityId);
        row.kind = asset.kind;
        row.name = asset.name;
        row.summary = asset.summary;
        row.hasAsset = asset.hasAsset;
        row.assetStatus = asset.assetStatus;
        row.layers = asset.layers;
        row.layersDone = asset.layersDone;
        row.degraded = asset.degraded;
        // 色调与中文标签都复用资产页那份 AssetTone（唯一的状态→颜色映射）。
        // label 也存进视图：AssetTone() 在本文件的匿名命名空间里，Shell 的检查器
        // 够不着，让它自己再映射一遍就是第二份状态词表。
        AssetTone(asset.hasAsset ? asset.assetStatus : std::string(), row.statusLabel, row.tone);
        v.assets.push_back(std::move(row));
    }
    // 选中下标必须夹进新快照的范围：换工程 / 库打不开时 assets 是空的。
    v.selectedAsset = s.assets.empty()
                          ? 0
                          : std::clamp(s.selectedAsset, 0, static_cast<int>(s.assets.size()) - 1);

    // ---- 检查器「关联」段：伏笔 / 场 / 镜三组 ----
    // 口径：这一段锚在**当前选中的那一镜**上，三组一起有或一起空。
    // 没选中镜（本章一个镜都没有，s.shot() == nullptr）时三组全留空 ——
    // 不拿「本章第一场」去补一个场 tag：空镜码配一个「场景 12」会被读成
    // 「这一镜属于第 12 场」，而实际上没有选中任何镜，那是误导而不是空态。
    v.relation = BookRelationView{};
    if (const BookShot* selected = s.shot(); selected != nullptr) {
        v.relation.shotCode = ShotCode(selected->ord);  // 侧栏 / 故事板 / 检查器同一份
        for (const BookSceneLink& link : s.sceneLinks) {
            if (link.id != selected->sceneId) {
                continue;
            }
            v.relation.sceneOrd = link.ord;
            v.relation.sceneTitle = link.title;
            v.relation.foreshadows = link.foreshadows;
            break;
        }
        // sceneId 在本章的 sceneLinks 里找不到（scenes 表为空 / shots.scene_id
        // 指向别章的场）时：sceneOrd 留 0、sceneTitle 与 foreshadows 留空 ——
        // 0 序即「不落在任何场上」，不编一个占位场出来。
    }
}

// 把 worker 产出的快照并进状态。selectedChapter 由 worker 按 wantChapter 定好，
// 这里只把选中的镜头 / 资产夹回新快照的范围。
void ApplyBook(BookState& s, BookState&& next) {
    const int keepShot = s.selectedShot;
    // ⚠️ 资产选中同理：LoadBook 开头的 `out = BookState{}` 会把 selectedAsset 抹成 0，
    //    不保留的话切一章或点一次「重新读取」，侧栏高亮就跳回第一个实体 ——
    //    而实体列表本身并没有换。RebuildBookSide 里那处夹范围只防越界，不防丢失。
    const int keepAsset = s.selectedAsset;
    s = std::move(next);
    s.selectedShot = keepShot >= 0 && keepShot < static_cast<int>(s.shots.size()) ? keepShot : 0;
    s.selectedAsset = keepAsset >= 0 && keepAsset < static_cast<int>(s.assets.size()) ? keepAsset : 0;
    s.loading = false;
    // ApplyBook 跑在 UI 线程（worker 结果经 async::PostToUi 回投后才调它），
    // 所以在这里重建外壳视图是安全的 —— worker 不会同时碰这个缓存。
    RebuildBookSide(s);
}

void RequestBookReload() {
    BookState& s = Book();
    s.loading = true;
    // ⚠️ 这里也要重建视图：`loading` 是在**派发之前**就翻成 true 的，而视图只在
    //    ApplyBook（落地时）与 BindBook（换工程时）重建 —— 不补这一下，视图里的
    //    loading 永远是上一轮的 false。症状：调用方「等 !loading」立刻成立、一帧都不等，
    //    然后把上一章的画面当成新章拍下来（实测：side-tree-empty-chapter 与 side-tree
    //    几乎逐字节相同）。此处只更新标志位，不动已经取好的章 / 镜。
    RebuildBookSide(s);
    const std::uint64_t gen = ++g_bookGen;
    const std::filesystem::path root = s.root;
    const int wantChapter = s.selectedChapter;
    async::RunOnWorker([root, wantChapter, gen] {
        BookState next;
        LoadBook(root, wantChapter, next);
        async::PostToUi([gen, next = std::move(next)]() mutable {
            BookState& cur = Book();
            if (gen != g_bookGen) {
                return;  // 已经又发起了一轮，丢掉这次过期结果
            }
            ApplyBook(cur, std::move(next));
        });
    });
}

void BindBook(std::filesystem::path root) {
    BookState& s = Book();
    if (s.root == root) {
        return;  // 同一个工程不重复开库
    }
    s = BookState{};
    RebuildBookSide(s);  // ⚠️ 换工程：必须立刻把外壳视图一起清空，
                         //    否则这一轮还没回投的窗口里，侧栏会把上一本的章
                         //    挂在新工程名下（症状：切了书树还是旧章节）。
    if (root.empty()) {
        return;  // 未打开工程 → 空态
    }
    s.root = std::move(root);
    s.bound = true;
    RequestBookReload();
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

// 把 ContinuityIssue::detail 里的镜对从**库主键**换成镜码：`镜 #128→#131：…` →
// `镜 S004 → S005：…`。
//
// ⚠️ refactor/PROGRESS.md 曾把「镜与镜的关联接不上」记成「ContinuityIssue 没有
//    shot id，要归因必须改 src/novel/」—— **那个归因是错的**。镜对就写在 detail
//    里（NovelContinuity.cpp 的 C1/C3/C8/C10/C12 五处，格式 `镜 #{}→#{}：…`），
//    缺的只是「id → 镜码」这一步显示侧映射，shine_core 一个字都不用改。
//
// 为什么查内存快照而不是 `GetShot(id)`：detail 里的 id 来自**本章**的镜，而本章的
// 镜已经随 `ListShotsByChapter` 一起进了 BookState::shots（id + ord 都在）。
// 为一次纯显示映射重开库查询，是把 IO 搬回 UI 线程 —— 缓存里有的东西不去取。
//
// ⚠️ 解析必须**防御性**：detail 是中文文本约定，不是结构化字段。业务层改措辞
//    （换箭头、去掉 #、改措辞）都该表现为「解析不出来 → 原样显示 detail」，
//    而不是崩、也不是编一个镜对出来。id 在本章镜头表里查不到也一样：那是数据
//    对不上，如实回退比画一个假的 S00x 诚实。
std::string ShotPairToCodes(const std::string& detail, const std::vector<BookShot>& shots) {
    auto readId = [&detail](std::size_t from, std::size_t& end) -> RowId {
        if (from >= detail.size() || detail[from] < '0' || detail[from] > '9') {
            return 0;
        }
        RowId id = 0;
        std::size_t i = from;
        while (i < detail.size() && detail[i] >= '0' && detail[i] <= '9') {
            id = id * 10 + (detail[i] - '0');
            ++i;
        }
        end = i;
        return id;
    };
    // 只认**前两个** `#数字`：C3 的文案里第三个 `#` 是角色 id，不是镜。
    const std::size_t hashA = detail.find('#');
    if (hashA == std::string::npos) {
        return detail;
    }
    std::size_t endA = 0;
    const RowId idA = readId(hashA + 1, endA);
    const std::size_t hashB = idA > 0 ? detail.find('#', endA) : std::string::npos;
    if (hashB == std::string::npos) {
        return detail;
    }
    std::size_t endB = 0;
    const RowId idB = readId(hashB + 1, endB);
    if (idB <= 0) {
        return detail;
    }
    const auto codeOf = [&shots](RowId id) -> const BookShot* {
        for (const BookShot& row : shots) {
            if (row.id == id) {
                return &row;
            }
        }
        return nullptr;
    };
    const BookShot* a = codeOf(idA);
    const BookShot* b = codeOf(idB);
    if (a == nullptr || b == nullptr) {
        return detail;  // 本章镜头表里没有这两个 id → 原样显示，不编镜码
    }
    return detail.substr(0, hashA) + ShotCode(a->ord) + " → " + ShotCode(b->ord) +
           detail.substr(endB);
}

// 三页共用的**空态**：没绑工程 / 在读 / 库打不开 / 库里没数据，分四种说法。
// 不在这里编任何数字 —— 分清「还没加载」「加载失败」「确实没有」三件事。
void BookEmpty(Rect body, ImDrawList* draw, const char* icon, const char* hint) {
    BookState& s = Book();
    if (!s.bound) {
        Empty(draw, body, icon, "未绑定工程", "打开一本小说后这里才有真实数据。");
        return;
    }
    if (!s.error.empty()) {
        Empty(draw, body, icon, "读取失败", s.error);
        return;
    }
    if (s.loading) {
        Empty(draw, body, icon, "正在读取 novel.db", hint);
        return;
    }
    Empty(draw, body, icon, "尚无数据", hint);
}

} // namespace

// 三页绑定"当前工程根"的入口（必须在匿名命名空间之外，否则 Shell 链接不到）。
// 三个入口共用同一份快照：Shell 在 SetProjectRoot 里连同 BindOverviewProject 一起调，
// 调任意一个即可（同一个 root 只会开一次库）。
void BindNovelProject(std::filesystem::path root) { BindBook(std::move(root)); }
void BindAssetsProject(std::filesystem::path root) { BindBook(std::move(root)); }
void BindStoryboardProject(std::filesystem::path root) { BindBook(std::move(root)); }

// 页面层 → 外壳的 toast 通道。没注入时（单跑页面、或注入前的那几帧）调用是空操作，
// 不会崩，也不会静默吞掉 —— 调用点是「用户点了个按钮」，静默吞掉等于按钮又变成空操作。
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

// ---- P4 外壳（Shell 侧栏树 / 检查器）要的那份只读视图 ----
// ⚠️ 这三个也必须在匿名命名空间**之外**（与上面三个 Bind 同一批），
//    否则 Shell 在 SetProjectRoot 里链接不到它们。
// 只读视图。UI 线程读，不做任何 IO；没绑工程时返回的是一份全 0 / 空列表的默认视图。
const BookSideView& BookSide() { return BookSideCache(); }

// entities.kind / visual_assets.kind → 中文分类名。表外原样回显，不编一个名字。
// ⚠️ 必须定义在匿名命名空间**之外**（声明见 WorkspacePages.h），否则 Shell 的
//    kind 筛选树链接不到它。**就是**本文件原先那个内部 KindLabel —— 一份映射，
//    改名导出而已；下面资产页卡片 / 详情那两处调用点也走它。
std::string AssetKindLabel(const std::string& kind) {
    if (kind == "person" || kind == "character") {
        return "角色";
    }
    if (kind == "location") {
        return "地点";
    }
    if (kind == "item" || kind == "prop" || kind == "treasure") {
        return "物品";
    }
    if (kind == "clothing") {
        return "服装";
    }
    if (kind == "faction") {
        return "势力";
    }
    if (kind == "event") {
        return "事件";
    }
    return kind.empty() ? kDash : kind;
}

// 资产工作区的 kind 筛选。存处在匿名命名空间里的 BookKindFilterSlot（函数内
// static）—— 不在 BookSideView 里，理由见那处的注释：视图会被整块重建。
// 空串 = 全部。
const std::string& BookKindFilter() { return BookKindFilterSlot(); }
void SetBookKindFilter(std::string kind) { BookKindFilterSlot() = std::move(kind); }

// 侧栏资产树点叶子 → 改选中实体。index 是 **BookSideView::assets 的下标**
// （与 BookState::assets 同序），不是筛后树的序号。
// 越界只可能是过期点击或旧工程的下标，先判界、后夹范围，和 SelectBookChapter
// 同一套写法；改完立刻重建视图 —— 外壳读的是缓存，不重建就还是上一格高亮。
void SelectBookAsset(int index) {
    BookState& s = Book();
    if (s.assets.empty()) {
        return;  // 没实体 = 没东西可切
    }
    const int last = static_cast<int>(s.assets.size()) - 1;
    if (index < 0 || index > last) {
        return;
    }
    s.selectedAsset = std::clamp(index, 0, last);
    RebuildBookSide(s);
}

// 侧栏点击 → 改选中项。越界直接什么都不做。
// ⚠️ 不能"夹回去接着用"：越界只可能是过期点击或旧工程的下标，拿一个猜出来的下标
//    去 RequestBookReload() 会白发一轮 worker 任务，还会把 selectedChapter 改成
//    另一章的镜 —— 所以先判界、后夹范围，最后写。
void SelectBookChapter(int index) {
    BookState& s = Book();
    if (s.chapters.empty()) {
        return;  // 没章 = 没东西可切，也不发 worker
    }
    const int last = static_cast<int>(s.chapters.size()) - 1;
    if (index < 0 || index > last) {
        return;
    }
    s.selectedChapter = std::clamp(index, 0, last);
    s.selectedShot = 0;  // 换章了：旧镜下标在新章里没意义，归 0 等 worker 回投
    RebuildBookSide(s);  // 选中项立刻生效；RequestBookReload 内部还会再重建一次（带 loading=true）
    RequestBookReload();  // 取的是新选中章的场 / 镜（LoadBook 按 wantChapter 定章）
}

// 选镜只改下标，**不重取** —— 镜列表已经在内存里，重取会白跑一轮 sqlite。
void SelectBookShot(int index) {
    BookState& s = Book();
    if (s.shots.empty()) {
        return;
    }
    const int last = static_cast<int>(s.shots.size()) - 1;
    if (index < 0 || index > last) {
        return;
    }
    s.selectedShot = std::clamp(index, 0, last);
    // ⚠️ 必须重建视图。故事板页直接读 BookState（永远新鲜），侧栏与检查器读的是
    //    缓存视图 —— 不补这一下，点了 S002 之后故事板换面了、侧栏高亮和检查器属性
    //    还钉在 S001 上。而且这种不一致**不会**被"两张图 md5 不同"抓到：
    //    故事板那半边确实变了，两张图必然不同。只能靠人看图发现。
    RebuildBookSide(s);
}

// ================================================================ P5.4 分镜
//
// 全部字段来自 novel.db：章/场/镜（NovelGraph::ListChapters/ListScenes +
// NovelVisual::ListShotsByChapter）、V1–V7 阶段产物（ListStageArtifacts）、
// V8 连续性（RunContinuityChecks 的真实三态）。旧版的 "S012 · 转身"、6.0s、
// 6 条 "C1 服装一致"、8 张 S010–S017 缩略图全是写死的，已全部删除。
void StoryboardPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const BookChapter* chapter = s.chapter();
    const BookShot* shot = s.shot();

    // 页头标题/副行：没有选中镜头时说清是哪种空态，不用占位镜头号。
    // 副行恒为"章 + 已落库镜头数 + 库路径"（全是真实计数），标题随选中镜头变。
    std::string title = "分镜";
    std::string subtitle = "Scene → Sequence → Shot";
    if (chapter != nullptr) {
        title = "第 " + std::to_string(chapter->ord) + " 章" +
                (chapter->title.empty() ? std::string() : (" · " + chapter->title));
        subtitle = "已落库镜头 " + std::to_string(s.shots.size()) + " 个 · " + s.dbPath;
    }
    if (shot != nullptr) {
        title = ShotCode(shot->ord) + " · " +
                (shot->action.empty() ? std::string(kDash) : shot->action);
    }
    Rect right;
    Rect content = ViewHeader(area, draw, "clapper", title.c_str(), subtitle.c_str(), &right);

    // 状态标签：取镜头的 canon_status（shots 表的规范值），映射不到就原样显示。
    std::string canonLabel = shot != nullptr && !shot->canonStatus.empty() ? shot->canonStatus : kDash;
    const float tagW = TagWidth(canonLabel, false, true);
    Tag(draw, RectAt(right.max.x - 320.0f, right.min.y, tagW, 20.0f), canonLabel, theme::Tone::Idle,
        false, true);

    ButtonSpec run;
    run.variant = ButtonVariant::Primary;
    const char* runLabel = "重新读取";
    const float runW = ButtonWidth(ButtonSize::Medium, 0.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, runLabel));
    // ⚠️ 业务层目前**没有**暴露"替前端跑 V1–V8"的服务接口（Qt 版靠 NovelDirector +
    // LlmCallFn 注入，ImGui 侧没有注入点）。所以这里只给"重新读取"，不给一个点了
    // 什么都不做的假"运行 V1–V8"。
    run.disabled = !s.bound || s.loading;
    if (Button(draw, RectAt(right.max.x - runW, right.min.y, runW, 30.0f), runLabel, run, "sb-run")) {
        RequestBookReload();
    }

    // 空态闸门：没绑 / 读失败 / 正在读 / 库里没章 —— BookEmpty 内部按这四种分别措辞。
    if (!s.bound || !s.error.empty() || s.chapters.empty()) {
        BookEmpty(content, draw, "clapper", "这本小说还没有章。跑一次 T1–T17 后这里才有分镜。");
        // 空态也要自报：只画一个居中图标 + 两行字，报 2400 会让工作区多出只能滚到
        // 空白的滚动范围。ViewportBottom 已经是绝对 y，不要再加 area.min.y。
        pages::SetPageContentHeight(ViewportBottom(area) - area.min.y + kGap);
        return;
    }

    // V1–V8 状态：逐项来自真实表（V1–V7 看 stage_artifacts 有无产物，V8 看连续性报告）。
    std::vector<StageNode> vs;
    for (int i = 0; i < 8; ++i) {
        vs.push_back(StageNode{"V" + std::to_string(i + 1), "",
                               s.vStage[i] ? StageState::Done : StageState::Todo});
    }
    StageFlow(draw, Rect{content.min.x, content.min.y, content.max.x, content.min.y + 30.0f}, vs);

    // .shots-wrap = minmax(0,1.2fr) / minmax(0,1fr) gap16
    const float detailTop = content.min.y + 46.0f;
    // 时间轴是**常驻可见**元素（横向镜条 + 时长），所以钉**视口**底，不钉布局区底。
    // 原来 `content.max.y - 118` 取的是 2400 布局区的底（≈2282），于是时间轴要滚到
    // 最底才看得着，上面两张 detail / continuity 卡被撑到约 2200px 高却只画十几行。
    //
    // ⚠️ 视口很矮时（< 约 200px）`timelineTop - kGap` 会落到 detailTop 之上，两张卡
    //    的 max.y < min.y 变成反向矩形 —— DrawShadowed / Card 会整块 return（内容
    //    静默消失）。给一个下限，宁可让时间轴压住卡片，也不产出反向矩形。
    const float contentBottom = ViewportBottom(area);
    const float timelineTop =
        std::max(contentBottom - 118.0f, std::min(content.min.y + 120.0f, contentBottom));
    const float detailW = (content.width() - kGap) * 1.2f / 2.2f;
    const float detailBottom = std::max(timelineTop - kGap, detailTop);
    const Rect detail{content.min.x, detailTop, content.min.x + detailW, detailBottom};
    const Rect continuity{detail.max.x + kGap, detailTop, content.max.x, detailBottom};

    // ---- 镜头详情 ----
    Rect detailBody = Card(draw, detail, "镜头详情", "target", false, false);
    if (shot == nullptr) {
        Empty(draw, detailBody, "clapper", "本章尚无镜头",
              "V9（叙事分镜）落库后，shots 表才会有行。");
    } else {
        // 时长：优先 timeline_json.duration_s（秒），退回 duration_note 原文，都没有 → kDash。
        std::string duration = kDash;
        if (shot->durationSec > 0.0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1fs", shot->durationSec);
            duration = buf;
        } else if (!shot->durationNote.empty()) {
            duration = shot->durationNote;
        }
        auto orDash = [](RowId id) { return id > 0 ? ("#" + std::to_string(id)) : std::string(kDash); };
        KeyValues(draw, detailBody,
                  {{"镜头", "#" + std::to_string(shot->id)},
                   {"场 / 序", "第 " + std::to_string(shot->sceneOrd) + " 场 · 第 " +
                                   std::to_string(shot->ord) + " 镜"},
                   {"动作", shot->action.empty() ? kDash : shot->action},
                   {"表演", shot->expression.empty() ? kDash : shot->expression},
                   {"机位", orDash(shot->cameraId)},
                   {"光线", orDash(shot->lightingId)},
                   {"时长", duration},
                   {"情绪", shot->mood.empty() ? kDash : shot->mood},
                   {"旁白", shot->narration.empty() ? kDash : shot->narration}});
        // Beat 时间轴：直接显示库里的 timeline_json 原文，不编一份示例 JSON。
        const float ty = detailBody.min.y + 190.0f;
        static const char* kBeatLabel = "BEAT 时间轴（shots.timeline_json）";
        draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(detailBody.min.x, ty), ColorTextMuted(),
                      kBeatLabel, kBeatLabel + std::strlen(kBeatLabel));
        DrawRoundRect(draw, ImVec2(detailBody.min.x, ty + 16.0f),
                      ImVec2(detailBody.max.x, ty + 104.0f), 6.0f, ColorFillMuted(), ColorLineNormal(),
                      1.0f);
        if (shot->timelineJson.empty() || shot->timelineJson == "{}") {
            DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                            ImVec2(detailBody.min.x + 10.0f, ty + 40.0f), detailBody.width() - 20.0f,
                            ColorTextMuted(), "这一镜没有 timeline_json。", true);
        } else {
            DrawTextClipped(draw, MonoAt(12.5f), 12.5f, ImVec2(detailBody.min.x + 10.0f, ty + 26.0f),
                            detailBody.width() - 20.0f, ColorTextSecondary(), shot->timelineJson, true);
        }
        // 空间 / 转场 / 走位**无专列**（V9 只存盘到 storyboard.json）—— 如实说明，不编值。
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(detailBody.min.x, ty + 112.0f), detailBody.width(),
                        ColorTextMuted(),
                        "空间 / 转场 / 走位没有库字段，只在 work/ch<NNN>/storyboard.json", true);
    }

    // ---- 连续性 C1–C12：真实三态 ----
    Rect continuityBody = Card(draw, continuity, "连续性 C1-C12", "check", false, false);
    if (!s.continuity.ran) {
        Empty(draw, continuityBody, "check", "连续性未校验", "V8 需要该章的镜头数据。");
    } else {
        // 汇总行用真实计数：看了几镜、比对几对、失败几条、数据不足几条。
        const std::string summary = "镜 " + std::to_string(s.continuity.shotsSeen) + " · 比对 " +
                                    std::to_string(s.continuity.pairsChecked) + " 对 · 失败 " +
                                    std::to_string(s.continuity.failed) + " · 数据不足 " +
                                    std::to_string(s.continuity.unverified);
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, continuityBody.min, continuityBody.width(),
                        ColorTextSecondary(), summary, true);
        float cy = continuityBody.min.y + 24.0f;
        if (s.continuity.issues.empty()) {
            // 三态里"没报 issue"不等于"通过"——数据不足时 RunContinuityChecks 记 unverified。
            const theme::Tone tone = s.continuity.unverified > 0 ? theme::Tone::Warn : theme::Tone::Ok;
            const char* label = s.continuity.unverified > 0 ? "无不一致，但有数据不足项" : "无不一致";
            StatusDot(draw, ImVec2(continuityBody.min.x + 4.0f, cy + 6.0f), tone, false);
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(continuityBody.min.x + 16.0f, cy),
                          ColorTextSecondary(), label, label + std::strlen(label));
            cy += 22.0f;
        }
        // ⚠️ 下面两个循环原来都是 `if (cy + 22.0f > continuityBody.max.y) { break; }`。
        //    卡片高度是按**视口**定的固定槽位，画不下就**静默丢掉**后面的 issue 与 note ——
        //    用户看到的是「就这么多」，界面上没有任何提示说还有第 N+1 条。列表被截断且不
        //    提示 = 界面在骗人（V8 的 issue / notes 都没有条数上限，数据一多必然复现）。
        //
        //    改成：列表本体自己滚（ScrollRegion），画**全部**。卡片标题、汇总行、
        //    「无不一致」结论留在滚动区**外面** —— 滚的只有下面这些行。
        if (!s.continuity.issues.empty() || !s.continuity.notes.empty()) {
            ScrollRegion issueScroll("novel-continuity-issues",
                                     Rect{continuityBody.min.x, cy, continuityBody.max.x,
                                          continuityBody.max.y});
            if (issueScroll) {
                // ⚠️ 内容必须画在 child **自己的** draw list 上：BeginChild 的裁剪矩形
                //    只写进它自己那条 list，画到外层 list 上裁剪**完全无效**（内容会盖住
                //    上层而不是消失）。见 Scroll.h。
                ImDrawList* ldraw = issueScroll.drawList();
                const Rect inner = issueScroll.content();
                float ly = inner.min.y;
                for (const novelcore::ContinuityIssue& issue : s.continuity.issues) {
                    const theme::Tone tone =
                        issue.severity == "high" ? theme::Tone::Danger : theme::Tone::Warn;
                    StatusDot(ldraw, ImVec2(inner.min.x + 4.0f, ly + 6.0f), tone, false);
                    // 镜对换成镜码（`镜 #128→#131：` → `镜 S004 → S005：`）：这条 issue 说的
                    // 就是「这两镜之间」出了什么事，写库主键的话没人对得上号。解析不出来就
                    // 原样显示 detail —— 见 ShotPairToCodes 的注释。
                    const std::string label = issue.code + " " + ShotPairToCodes(issue.detail, s.shots);
                    DrawTextClipped(ldraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 16.0f, ly),
                                    inner.width() - 16.0f, ColorTextSecondary(), label, true);
                    ly += 22.0f;
                }
                // unverified 的原因也照实列（它不是失败，但必须看得见）。
                for (const std::string& note : s.continuity.notes) {
                    DrawTextClipped(ldraw, FontAt(11.5f), 11.5f, ImVec2(inner.min.x, ly),
                                    inner.width(), ColorTextMuted(), note, true);
                    ly += 20.0f;
                }
                // ⚠️ 不调这一行等于没修：自绘内容全程不给 ImGui 提交 item，ContentSize
                //    恒为 0 ⇒ ScrollMaxY 恒为 0 ⇒ 滚轮怎么转都停在原地（本仓已踩过两次）。
                issueScroll.setContentHeight(ly - inner.min.y);
            }
        }
    }

    // ---- 故事板时间线：真实镜头卡 ----
    const Rect timeline{content.min.x, timelineTop, content.max.x, contentBottom};
    DrawShadowed(draw, timeline.min, timeline.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    // 本页最深就是这条时间轴（两张卡的下边界是 timelineTop - kGap，更浅）。
    // 自报它的高度，工作区的滚动范围从此跟着视口走 —— 不再是「滚到 2400 才看得见
    // 时间轴、而那 1400px 里什么都没有」（与总控页同一处治理，注释见那里）。
    pages::SetPageContentHeight(contentBottom - area.min.y + kGap);
    if (s.shots.empty()) {
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(timeline.min.x + 14.0f, timeline.min.y + 14.0f), timeline.width() - 28.0f,
                        ColorTextMuted(), "本章尚无镜头，时间线为空。", true);
        return;
    }
    // 时长条按**本章最长镜头**归一（旧版写死 60% = 假装 6s）。
    double longest = 0.0;
    for (const BookShot& row : s.shots) {
        longest = std::max(longest, row.durationSec);
    }
    float x = timeline.min.x + 10.0f;
    for (int i = 0; i < static_cast<int>(s.shots.size()); ++i) {
        if (x + 96.0f > timeline.max.x) {
            break;  // 横向超出视口就不画（外壳的 ScrollRegion 负责滚动）
        }
        const BookShot& row = s.shots[static_cast<std::size_t>(i)];
        const Rect card{x, timeline.min.y + 10.0f, x + 96.0f, timeline.max.y - 10.0f};
        const bool on = (i == s.selectedShot);
        DrawRoundRect(draw, card.min, card.max, 8.0f, ColorFillMuted(),
                      on ? ColorAccent() : ColorLineNormal(), on ? 1.5f : 1.0f);
        if (on) {
            DrawRoundRect(draw, card.min - ImVec2(2, 2), card.max + ImVec2(2, 2), 10.0f, 0,
                          ColorOf(theme::CurrentDerived().accentDim), 2.0f);
        }
        // ⚠️ 缩略图：真实分镜图要走 gpu 纹理链路（出图页才接），这里**不画假缩略图** ——
        // 显示"待出图"占位，与 webui 自己在没有图时的状态一致。
        const Rect thumb{card.min.x + 6.0f, card.min.y + 6.0f, card.max.x - 6.0f, card.min.y + 66.0f};
        DrawRoundRect(draw, thumb.min, thumb.max, 6.0f, ColorFillMuted(), ColorLineSubtle(), 1.0f);
        DrawIconCentered(draw, "image", thumb.center(), 20.0f, ColorTextMuted());
        // 镜码与侧栏树 / 检查器共用 WorkspacePages.h 的 ShotCode —— 同一个镜三处同名。
        const std::string code = ShotCode(row.ord);
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(card.min.x + 8.0f, card.min.y + 70.0f),
                      ColorAccent(), code.data(), code.data() + code.size());
        // 时长条：按真实 durationSec / 本章最长。没有 duration 就画一条灰槽，不画 60%。
        const float barY = card.max.y - 10.0f;
        const float barMaxW = card.width() - 12.0f;
        const float ratio =
            (longest > 0.0 && row.durationSec > 0.0)
                ? std::clamp(static_cast<float>(row.durationSec / longest), 0.08f, 1.0f)
                : 0.0f;
        DrawRoundRect(draw, ImVec2(card.min.x + 6.0f, barY),
                      ImVec2(card.min.x + 6.0f + barMaxW, barY + 4.0f), 2.0f, ColorFillSelected());
        if (ratio > 0.0f) {
            DrawRoundRect(draw, ImVec2(card.min.x + 6.0f, barY),
                          ImVec2(card.min.x + 6.0f + barMaxW * ratio, barY + 4.0f), 2.0f, ColorAccent());
        }
        if (Clicked(card, "sb-tl-" + std::to_string(i))) {
            // 走 SelectBookShot 而不是就地写 `s.selectedShot` + RebuildBookSide：
            // 那两行本来是 SelectBookShot 的复制品，少了夹取边界检查，再多一份就要
            // 靠「记得同步」维持。选中态只有一条路径。
            SelectBookShot(i);
        }
        x += 104.0f;
    }
}

// ================================================================ P5.2 小说
//
// ⚠️ 这一页的实现从 WorkspaceA.cpp **搬到这里**（连同下面的资产页）：真实数据源
//    novel.db 的读取/快照/worker 全在 WorkspaceB.cpp 的 BookState 里，把实现留在
//    A 会让"数据在哪"分裂成两处。声明仍在 WorkspacePages.h，Shell 不用改。
//
// 旧版的 "第 3 章 · 雨夜"、固定摘要、固定草稿三段、字数 "2180"、状态 "草稿"、
// 8 个模式里 6 个的假流水线节点，全部改成业务层返回值或诚实空态。
void NovelPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const float inspectorW = 280.0f;
    const Rect center{area.min.x, area.min.y, area.max.x - inspectorW, area.max.y};
    const Rect inspector{center.max.x, area.min.y, area.max.x, area.max.y};

    // 8 个模式标签（min-h 44，横向滚动；选中 = accent + 2px accent 下边框）
    const char* modes[] = {"章节", "设定", "初始化", "流水线", "评审", "模型", "状态", "自动"};
    float tx = area.min.x;
    const float tabY = area.min.y;
    ImFont* tabFont = FontBoldAt(12.5f);
    for (int i = 0; i < 8; ++i) {
        const float w = LabelWidth(tabFont, 12.5f, modes[i]) + 32.0f;
        const Rect tab{tx, tabY, tx + w, tabY + 44.0f};
        if (i == mode_) {
            DrawRoundRect(draw, tab.min, tab.max, 6.0f, ColorFillSelected());
            draw->AddLine(ImVec2(tab.min.x, tab.max.y - 1.0f), ImVec2(tab.max.x, tab.max.y - 1.0f),
                          ColorAccent(), 2.0f);
        }
        // 一次 HitTest 取齐 hovered + clicked。
        //
        // ⚠️ 悬停**背景对选中项也生效**，这不是我一开始写的那样。设计稿
        //    `views.css` 的两条规则是：
        //      .novel-modes .ntab:hover { color: text-primary; background: fill-muted; }
        //      .novel-modes .ntab.on   { color: accent; border-bottom-color: accent; }
        //    两条特异度相同、后者胜出，但 `.on` **没有声明 background**，
        //    所以 hover 的 fill-muted 底**照样作用在选中项上**。早先写成
        //    `hit.hovered && i != mode_`，选中页签悬停时一点反应都没有 ——
        //    CSS 不会因为「已经选中」就免掉 hover 底。悬停探针直接判 `broken` 抓到了它。
        const Hit hit = HitTest(tab, "novel-mode-" + std::to_string(i));
        if (hit.hovered) {
            DrawRoundRect(draw, tab.min, tab.max, 6.0f, ColorFillHover());
        }
        draw->AddText(tabFont, 12.5f, ImVec2(tab.min.x + 16.0f, tab.min.y + 14.0f),
                      i == mode_ ? ColorAccent()
                                 : (hit.hovered ? ColorText() : ColorTextSecondary()),
                      modes[i], modes[i] + std::strlen(modes[i]));
        if (hit.clicked) {
            mode_ = i;
        }
        tx += w + 4.0f;
    }

    const Rect body{center.min.x, tabY + 52.0f, center.max.x, center.max.y};

    // 章选择条：切章会重跑 worker（镜头/阶段/连续性都按章取）。
    if (s.bound && s.error.empty() && !s.chapters.empty()) {
        float cx = body.min.x;
        const float chipH = 24.0f;
        for (int i = 0; i < static_cast<int>(s.chapters.size()) && cx + 70.0f < body.max.x; ++i) {
            const std::string label = "第 " + std::to_string(s.chapters[static_cast<std::size_t>(i)].ord) + " 章";
            const float w = LabelWidth(FontAt(12.0f), 12.0f, label.c_str()) + 22.0f;
            const bool on = (i == s.selectedChapter);
            const Rect chip{cx, body.min.y, cx + w, body.min.y + chipH};
            DrawRoundRect(draw, chip.min, chip.max, 12.0f,
                          on ? ColorOf(theme::CurrentDerived().accentDim) : ColorFillMuted(),
                          on ? ColorAccent() : ColorLineSubtle(), 1.0f);
            draw->AddText(FontAt(12.0f), 12.0f, ImVec2(cx + 11.0f, body.min.y + 5.0f),
                          on ? ColorAccent() : ColorTextSecondary(), label.data(), label.data() + label.size());
            if (Clicked(chip, "novel-ch-" + std::to_string(i))) {
                s.selectedChapter = i;
                s.selectedShot = 0;
                RequestBookReload();
            }
            cx += w + 6.0f;
        }
    }

    const Rect modeBody{body.min.x, body.min.y + 34.0f, body.max.x, body.max.y};

    // 本页**实际画到**的最底（绝对屏幕 y），末尾自报给外壳当滚动区高度。
    // 起点取 modeBody 的顶：空态分支只画一个居中的图标 + 两行字，不该把 2400 的
    // 布局区高原样报回去（那会让工作区多出上千 px 只能滚到空白）。
    float usedBottom = modeBody.min.y;

    if (mode_ == 0) {
        // ---- 章节 ----
        const BookChapter* chapter = s.chapter();
        if (!s.bound || !s.error.empty() || chapter == nullptr) {
            BookEmpty(modeBody, draw, "book", "这本小说还没有章。跑一次 T1–T17 后这里才有正文。");
        } else {
            std::string statusLabel;
            theme::Tone statusTone = theme::Tone::Idle;
            ChapterTone(chapter->status, statusLabel, statusTone);
            const std::string head = "第 " + std::to_string(chapter->ord) + " 章 · " +
                                     (chapter->title.empty() ? std::string(kDash) : chapter->title);
            draw->AddText(FontBoldAt(20.0f), 20.0f, ImVec2(modeBody.min.x, modeBody.min.y),
                          ColorText(), head.data(), head.data() + head.size());
            const float stW = TagWidth(statusLabel, false, chapter->status == "review");
            Tag(draw, RectAt(modeBody.min.x, modeBody.min.y + 28.0f, stW, 20.0f), statusLabel,
                statusTone, false, chapter->status == "review");
            // 字数：chapters.words 是业务层记的值；为 0 时说"未统计"，不编一个数字。
            const std::string words = chapter->words > 0 ? std::to_string(chapter->words) : "未统计";
            DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                            ImVec2(modeBody.min.x + stW + 10.0f, modeBody.min.y + 32.0f),
                            modeBody.width(), ColorTextMuted(),
                            words + " 字 · 更新 " + TimeText(chapter->updated), true);

            // .chap-summary：fill-muted + 3px accent 左边框 + pad 10/14
            float y = modeBody.min.y + 60.0f;
            if (!chapter->summary.empty()) {
                const Rect sum{modeBody.min.x, y, modeBody.min.x + 720.0f, y + 48.0f};
                DrawRoundRect(draw, sum.min, sum.max, 0.0f, ColorFillMuted());
                DrawRoundRect(draw, ImVec2(sum.min.x, sum.min.y), ImVec2(sum.min.x + 3.0f, sum.max.y),
                              1.5f, ColorAccent());
                DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                                ImVec2(sum.min.x + 14.0f, sum.min.y + 10.0f), sum.width() - 28.0f,
                                ColorTextSecondary(), "本章目标 · " + chapter->summary, true);
                y += 60.0f;
            }
            // .draft：14px / 行高 1.9 / max-w 720。正文是 chapters.body 原文。
            // 折行高度由 DrawTextClipped 的返回值给出（它自己按 maxWidth 折行并返回
            // 实际占高），不另算一份行数 —— 两份算法迟早对不上。
            if (chapter->body.empty()) {
                Empty(draw, Rect{modeBody.min.x, y, modeBody.min.x + 720.0f, y + 220.0f}, "text",
                      "本章尚无正文", "T11（正文写作）落库后 chapters.body 才有内容。");
                usedBottom = std::max(usedBottom, y + 220.0f);
            } else {
                const float bodyH = DrawTextClipped(draw, FontAt(14.0f), 14.0f, ImVec2(modeBody.min.x, y),
                                                    720.0f, ColorText(), chapter->body, true);
                usedBottom = std::max(usedBottom, y + bodyH);
            }
        }
    } else if (mode_ == 1) {
        // ---- 设定：真实实体（entities 表）----
        if (!s.bound || !s.error.empty() || s.assets.empty()) {
            BookEmpty(modeBody, draw, "masks", "还没有实体。初始化链跑完后这里才有设定。");
        } else {
            static const char* kWorldTitle = "设定集";
            draw->AddText(FontBoldAt(16.0f), 16.0f, ImVec2(modeBody.min.x, modeBody.min.y),
                          ColorText(), kWorldTitle, kWorldTitle + std::strlen(kWorldTitle));
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(modeBody.min.x, modeBody.min.y + 22.0f),
                            modeBody.width(), ColorTextMuted(),
                            "共 " + std::to_string(s.assets.size()) + " 个实体 · 来自 entities 表", true);
            const int columns = AutoGridCols(modeBody.width(), 260.0f, 14.0f);
            const float cardW =
                (modeBody.width() - 14.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
            for (int i = 0; i < static_cast<int>(s.assets.size()); ++i) {
                const BookAsset& a = s.assets[static_cast<std::size_t>(i)];
                const int column = i % columns;
                const int row = i / columns;
                // ⚠️ 这里**曾经**是本仓第三处「宽高写进 kit::Rect 四参」的同型 bug：
                //    四参是 (minX,minY,maxX,maxY)，写 (x,y,w,h) 时 max.x = cardW(~250)
                //    < min.x(~296) ⇒ DrawShadowed 整块 return，卡片一张都没画、也点不到。
                //    那个 bug **已修**（下面就是 RectAt），注释留着是因为前两处同型
                //    （资产总览网格 / 底栏页签条）证明它会复发，而当初的
                //    find-rect-wh-misuse.ps1 只认裸 `Rect{...}`、漏掉了本处这种声明式
                //    `Rect name{...}`。写宽高一律 RectAt。
                const Rect card = RectAt(modeBody.min.x + (cardW + 14.0f) * static_cast<float>(column),
                                         modeBody.min.y + 40.0f + 84.0f * static_cast<float>(row),
                                         cardW, 76.0f);
                // 选中态与资产总览网格 / 侧栏树 / 检查器**共用** BookState::selectedAsset：
                // 写入口只有 SelectBookAsset 一条（它会顺带 RebuildBookSide，侧栏高亮
                // 跟着走）。另存一份 `on` 局部选中态就是第 N 个「点这边亮那边」。
                const bool on = (i == s.selectedAsset);
                // ⚠️ 一次 HitTest 取齐 hovered + clicked，**不要** Hovered(idA) 再
                //    Clicked(idB)：同一矩形上叠两个 InvisibleButton 时后者永远
                //    clicked=false（本仓踩过），症状是「卡画得出来、怎么点都不动」。
                //    描边规则照抄资产总览网格那张卡（.card:hover 把边框提到 accent-glow），
                //    不另发明一套选中样式。
                const Hit hit = HitTest(card, "novel-asset-" + std::to_string(i));
                DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                             on ? ColorAccent()
                                : (hit.hovered ? ColorAccentGlow() : ColorLineSubtle()),
                             1.0f);
                if (hit.clicked) {
                    SelectBookAsset(i);
                }
                usedBottom = std::max(usedBottom, card.max.y);
                const std::string name = a.name.empty() ? std::string(kDash) : a.name;
                DrawTextClipped(draw, FontBoldAt(13.0f), 13.0f,
                                ImVec2(card.min.x + 12.0f, card.min.y + 10.0f), cardW - 24.0f,
                                ColorText(), name, true);
                DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                                ImVec2(card.min.x + 12.0f, card.min.y + 30.0f), cardW - 24.0f,
                                ColorTextMuted(), AssetKindLabel(a.kind), true);
                DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                                ImVec2(card.min.x + 12.0f, card.min.y + 50.0f), cardW - 24.0f,
                                ColorTextMuted(), a.summary, true);
            }
        }
    } else if (mode_ == 3) {
        // ---- 流水线：真实阶段表（pipeline::AllStages 的 text 链）----
        std::vector<StageNode> nodes;
        for (const auto& stage : pipeline::AllStages()) {
            if (stage.chain == "text") {
                nodes.push_back(StageNode{stage.code, stage.name, StageState::Todo});
            }
        }
        if (nodes.empty()) {
            BookEmpty(modeBody, draw, "chip", "没有阶段定义。");
        } else {
            StageFlow(draw, Rect{modeBody.min.x, modeBody.min.y, modeBody.min.x + 1400.0f,
                                 modeBody.min.y + 30.0f},
                      nodes);
            // ⚠️ 阶段**运行态**归总控页的 Runner（它持有 pipeline::Runner 与账本）。
            //    本页不复制一份进度 —— 所以全部 Todo，并在下面说明去哪看真状态。
            //
            //    阶段表是**正文流**：有多长就多长，不再撑满 2400 的布局区。原来写死
            //    `modeBody.max.y - 24`，于是 T1–T17 只有十来行、框却有 2200px 高，
            //    脚注被顶到 y2382 —— 要滚到最底才看得见，而上面全是空白。行高 34
            //    与 StageList 内部一致（Views.cpp 的 rowH），多给 8px 收尾。
            const float listTop = modeBody.min.y + 48.0f;
            const float listH = 34.0f * static_cast<float>(nodes.size()) + 8.0f;
            StageList(draw, Rect{modeBody.min.x, listTop, modeBody.max.x, listTop + listH}, nodes,
                      "artifacts/");
            // 脚注紧跟表底（不再是 `max.y - 18` 的锚点）。
            const float footY = listTop + listH + 8.0f;
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(modeBody.min.x, footY), modeBody.width(),
                            ColorTextMuted(),
                            "这里只列阶段定义；真实运行进度与账本在「总控」页的 Runner 上。", true);
            usedBottom = std::max(usedBottom, footY + 18.0f);
        }
    } else {
        // ---- 其余 5 个模式：业务层还没有对应的**只读投影**接口 ----
        // 如实说明缺什么，而不是拿别的表的数据凑一屏看起来像的东西。
        const char* missing = "";
        switch (mode_) {
        case 2: missing = "初始化链（novel::NovelInit 的门禁与产物表）没有只读查询接口。"; break;
        case 4: missing = "评审记录没有只读查询接口。"; break;
        case 5: missing = "模型角色 / 提示词模板没有只读查询接口。"; break;
        case 6: missing = "角色状态流水（character_status）没有按章的只读投影接口。"; break;
        default: missing = "自动运行策略没有只读查询接口。"; break;
        }
        Empty(draw, modeBody, "sparkles", "尚未接入业务层", missing);
    }

    // ---- 自报本页真实内容高度 ----
    //
    // Shell 给的布局区是写死的 2400px，页面若按它铺内容，工作区就会多出上千 px
    // 只能滚到空白的滚动范围（与总控页同一个问题，见 WorkspaceA.cpp 末尾的同名
    // 调用）。这里报「实际画到的最底 + 一个 kGap 的余量」。
    //
    // ⚠️ 取 `max(实际底, 视口底)`：右栏的面板底与分隔线是按 `inspector.max.y`
    //    （= 布局区底）画满的，只报正文底会在内容短的那几个模式下让右栏**悬空**，
    //    下面露出一条没画的面板底。视口底是下限，不是「撑高」——
    //    2400 那种把空盒子撑出来的做法正是这一行要根治的。
    pages::SetPageContentHeight(std::max(usedBottom, ViewportBottom(area)) - area.min.y + kGap);

    // ---- 右栏：当前章的真实属性 ----
    DrawRoundRect(draw, inspector.min, inspector.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(inspector.min.x + 0.5f, inspector.min.y),
                  ImVec2(inspector.min.x + 0.5f, inspector.max.y), ColorLineSubtle(), 1.0f);
    const BookChapter* chapter = s.chapter();
    if (chapter != nullptr) {
        std::string statusLabel;
        theme::Tone statusTone = theme::Tone::Idle;
        ChapterTone(chapter->status, statusLabel, statusTone);
        KeyValues(draw,
                  Rect{inspector.min.x + 16.0f, tabY + 60.0f, inspector.max.x - 16.0f, tabY + 220.0f},
                  {{"章节", "第 " + std::to_string(chapter->ord) + " 章"},
                   {"标题", chapter->title.empty() ? kDash : chapter->title},
                   {"字数", chapter->words > 0 ? std::to_string(chapter->words) : "未统计"},
                   {"状态", statusLabel},
                   {"更新", TimeText(chapter->updated)},
                   {"本章镜头", std::to_string(s.shots.size())}});
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(inspector.min.x + 16.0f, tabY + 230.0f), inspectorW - 32.0f,
                        ColorTextMuted(), "数据源：" + s.dbPath, true);
    } else {
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(inspector.min.x + 16.0f, tabY + 64.0f), inspectorW - 32.0f,
                        ColorTextMuted(), "未绑定工程", true);
    }
}

// ================================================================ P5.3 资产
//
// 同样从 WorkspaceA.cpp 搬来。旧版 12 张 "资产 N" 卡、沈砚/老沈/第 1 章、
// 基线 v2 / 当前 v3 / 差异 0.2418、S010–S014 chip、4 张参考图全是写死的。
// 现在逐项来自 entities + visual_assets + visual_artifacts。
void AssetsPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const std::vector<SegmentOption> options{{"d", "详情"}, {"o", "总览"}};
    const std::string_view picked =
        Segmented(draw, RectAt(area.min.x, area.min.y, SegmentedWidth(options), 32.0f), options,
                  overview_ ? "o" : "d", "assets-seg");
    overview_ = (picked == "o");

    const Rect body{area.min.x, area.min.y + 44.0f, area.max.x, area.max.y};
    if (!s.bound || !s.error.empty() || s.assets.empty()) {
        BookEmpty(body, draw, "masks", "还没有实体资产。跑一次初始化与资产流水线后这里才有内容。");
        return;
    }
    selected_ = std::clamp(selected_, 0, static_cast<int>(s.assets.size()) - 1);

    // ⚠️ kind 筛选是**侧栏与主区共享**的一份状态（设计稿 Assets.jsx:130 和 Shell.jsx:523
    //    读同一个 entityKind）。只筛侧栏的话，点完 chip 主区纹丝不动，看着像 chip 坏了。
    //    这里读同一份 BookKindFilter()，不另存一份 —— 存两份迟早只改得动一边。
    //
    //    详情**不**跟着筛：设计稿的 cur 取自未筛选的全集（Assets.jsx:131），所以筛到一个
    //    不含当前选中项的 kind 时详情页不空。照这个口径，否则点完 chip 主体变空态。
    const std::string& kindFilter = BookKindFilter();
    std::vector<int> visible;
    visible.reserve(s.assets.size());
    for (int i = 0; i < static_cast<int>(s.assets.size()); ++i) {
        if (kindFilter.empty() || s.assets[static_cast<std::size_t>(i)].kind == kindFilter) {
            visible.push_back(i);
        }
    }

    const BookAsset& asset = s.assets[static_cast<std::size_t>(selected_)];

    if (overview_) {
        if (visible.empty()) {
            BookEmpty(body, draw, "masks",
                      "这一类还没有实体。在左侧侧栏换回「全部」看看这个工程里有哪些实体");
            return;
        }
        const int columns = AutoGridCols(body.width(), 210.0f, 14.0f);
        const float cardW =
            (body.width() - 14.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        for (int slot = 0; slot < static_cast<int>(visible.size()); ++slot) {
            const int i = visible[static_cast<std::size_t>(slot)];  // 真实下标
            const BookAsset& row = s.assets[static_cast<std::size_t>(i)];
            const int column = slot % columns;
            const int r = slot / columns;
            // ⚠️ 这里原来把宽高直接写进了 Rect 的四参构造，而 kit::Rect 的四参是
            //    **(minX, minY, maxX, maxY)**，不是 (x, y, w, h)。于是 max.x = cardW(208)
            //    小于 min.x = 296，DrawRoundRect 的 `max.x <= min.x` 直接 return：
            //    总览网格的卡片**整张没画、也点不到**，界面上只剩名字和状态两行文字浮在
            //    背景上。thumb 用了 card.max.x，同一个原因一起消失。
            //    编译不报错、运行不崩，截图看得出「卡没了」但看不出是哪儿 —— 见
            //    kit::Rect 四参构造上的警告。宽高一律走 RectAt。
            const Rect card = RectAt(body.min.x + (cardW + 14.0f) * static_cast<float>(column),
                                     body.min.y + (192.0f + 14.0f) * static_cast<float>(r), cardW,
                                     192.0f);
            const bool on = (i == selected_);
            // ⚠️ 一次 HitTest 取齐 hovered + clicked。悬停描边是设计稿里 .card:hover
            //    的一部分（与项目中心 DrawHubCard 同一条规则：hover 把 border 从
            //    line-subtle 提到 accent-glow）。原来这里只有 `Clicked(card, ...)`，
            //    整条卡 hover 链路是断的 —— 鼠标划过去一点反应都没有，而**静息态
            //    截图完全看不出来**（hover 前后的差别本来就只在那一帧）。取证靠
            //    「帧内注入 MousePos + 钉住动画时钟」才逼出来：坐标在热区内、
            //    页面静止，两帧像素却逐字节相同。
            const Hit hit = HitTest(card, "asset-card-" + std::to_string(i));
            DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                         on ? ColorAccent()
                            : (hit.hovered ? ColorAccentGlow() : ColorLineSubtle()),
                         1.0f);
            // ⚠️ 缩略图：真实设定图要经 gpu 纹理链路解码（出图页才接）。没有就显示
            //    "无产出图"，不拿 Art() 占位画冒充这个角色的设定图。
            const Rect thumb{card.min.x + 8.0f, card.min.y + 8.0f, card.max.x - 8.0f, card.min.y + 158.0f};
            DrawRoundRect(draw, thumb.min, thumb.max, 8.0f, ColorFillMuted(), ColorLineSubtle(), 1.0f);
            DrawIconCentered(draw, "masks", thumb.center(), 24.0f, ColorTextMuted());
            const std::string name = row.name.empty() ? std::string(kDash) : row.name;
            DrawTextClipped(draw, FontBoldAt(13.0f), 13.0f, ImVec2(card.min.x + 12.0f, card.min.y + 164.0f),
                            cardW - 24.0f, ColorText(), name, true);
            std::string statusLabel;
            theme::Tone statusTone = theme::Tone::Idle;
            AssetTone(row.hasAsset ? row.assetStatus : std::string(), statusLabel, statusTone);
            const std::string sub = row.hasAsset ? statusLabel : "无视觉资产";
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(card.min.x + 12.0f, card.min.y + 180.0f),
                            cardW - 24.0f, ColorTextMuted(), sub, true);
            if (hit.clicked) {
                selected_ = i;
            }
        }
        return;
    }

    // ---- 详情 ----
    float y = body.min.y;
    DrawIcon(draw, "masks", ImVec2(body.min.x, y), 16.0f, ColorAccent());
    const std::string entityName = asset.name.empty() ? kDash : asset.name;
    draw->AddText(FontBoldAt(15.0f), 15.0f, ImVec2(body.min.x + 24.0f, y - 1.0f), ColorText(),
                  entityName.data(), entityName.data() + entityName.size());
    std::string statusLabel;
    theme::Tone statusTone = theme::Tone::Idle;
    AssetTone(asset.hasAsset ? asset.assetStatus : std::string(), statusLabel, statusTone);
    const float tagW = TagWidth(statusLabel, false, true);
    Tag(draw, RectAt(body.min.x + 24.0f + LabelWidth(FontBoldAt(15.0f), 15.0f, entityName.c_str()) + 12.0f,
                    y - 1.0f, tagW, 20.0f),
        statusLabel, statusTone, false, true);
    y += 28.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 当前选中项被 kind 筛选排除在外时，明说它不在筛选结果里 —— 照设计稿的 cur 口径
    // （Assets.jsx:131 从未筛选的全集取）详情照样显示，但用户会以为筛选没生效。
    if (!kindFilter.empty() && std::find(visible.begin(), visible.end(), selected_) == visible.end()) {
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, y), body.width(),
                        ColorTextMuted(),
                        "当前这一项不在「" + AssetKindLabel(kindFilter) + "」的筛选结果里", true);
        y += 18.0f;
    }

    // 真实字段：类别 / 实体状态 / 资产名 / canon / 生产态 / 设定图路径 / 形象层进度。
    // 旧版的"别名 老沈""出处 第 1 章""降级策略 保留上一版"在 entities 表里**没有列**，
    // 所以这些行不画 —— 不用相邻字段冒充。
    const std::string layers = asset.hasAsset ? (std::to_string(asset.layersDone) + " / " +
                                                 std::to_string(asset.layers) + " 层就绪")
                                              : std::string(kDash);
    KeyValues(draw, Rect{body.min.x, y, body.min.x + 640.0f, y + 150.0f},
              {{"类别", AssetKindLabel(asset.kind)},
               {"实体 ID", "#" + std::to_string(asset.entityId)},
               {"实体状态", asset.entityStatus.empty() ? kDash : asset.entityStatus},
               {"视觉资产", asset.hasAsset ? (asset.assetName.empty() ? kDash : asset.assetName)
                                           : "尚未建立"},
               {"Canon", asset.canonStatus.empty() ? kDash : asset.canonStatus},
               {"生产状态", asset.hasAsset ? statusLabel : kDash},
               {"形象层", layers},
               {"设定图", asset.sheetRelPath.empty() ? kDash : asset.sheetRelPath},
               {"降级", asset.degraded ? "有降级产物" : "无"}});
    y += 164.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 一致性对比 / 关联时间线 / 绑定镜头 chip：**没有可用来源**，逐条说清缺什么。
    const Rect missing{body.min.x, y, body.max.x, body.min.y + 220.0f};
    Empty(draw, missing, "compare", "尚无一致性对比数据",
          "对比需要同一角色的两层成图（visual_artifacts 里 front + turnaround 都 DONE）"
          "并解码出像素差；当前该资产形象层 " +
              (asset.hasAsset ? (std::to_string(asset.layersDone) + "/" +
                                 std::to_string(asset.layers) + " 就绪")
                              : std::string("尚未建立")) +
              "，且帧图解码尚未接入 ImGui 侧。");
    y += 236.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    static const char* kGapsTitle = "尚未接入的区块";
    // DrawTextClipped 收 string_view，自己算结束指针；末参是 wrap(bool)，不是 text_end。
    DrawTextClipped(draw, FontBoldAt(12.5f), 12.5f, ImVec2(body.min.x, y), body.width(), ColorText(),
                    kGapsTitle, true);
    y += 20.0f;
    for (const char* gap : {"关联时间线：需要按章的事件 + 分镜引用，资产页还没有这条查询。",
                            "绑定镜头 chip：需要 shots.reference_json 解析成 visual_assets 路径。",
                            "参考图：项目参考库 refs.json 的读取接口未接到 ImGui 侧。"}) {
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, y), body.width(),
                        ColorTextMuted(), gap, true);
        y += 18.0f;
    }
}

// ================================================================ P5.5 出图
// 满幅画布页（.canvas-page），不走 .vw 骨架。
void ImageFlowPage::Draw(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorFillMuted());
    // 点阵背景：radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px
    DrawDotGrid(draw, area.min, area.max, 22.0f, ColorLineNormal());

    // 浮动面板占右侧 348+16，画布在它左边。画布边界已经排除了面板，
    // 所以 FlowCanvas 的 fitInset 传 0 —— 再传 380 会重复扣一次宽度，
    // 把可用宽压到接近 0，缩放退化成看不清的一团。
    const float panelW = 348.0f;
    const float canvasRight = area.max.x - panelW - 32.0f;
    const Rect canvas{area.min, ImVec2(std::max(canvasRight, area.min.x + 200.0f), area.max.y)};
    // 数据指纹变了就重建 —— 快照是异步回投的，只判 empty 会把图冻在最空的那一刻。
    if (flowNodes_.empty() || flowDataKey_ != FlowDataKey()) {
        BuildGraph();
    }
    if (g_imageFlowNeedsFit) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        g_imageFlowNeedsFit = false;
    }
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f);

    // 浮动工具条（左上，玻璃 → --glass 实色，r10，pad 8/12）
    const float barW = 520.0f;
    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 16.0f + barW,
                   area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "image", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    // ⚠️ 早先是 `const char* barTitle = "出图流程 · 分镜图_v3"` —— 「分镜图_v3」是
    //    **设计稿的 mock 字符串**，照抄等于把一个不存在的资产名写死在界面上
    //    （与「实体 · 林晚」同一类错误，见 DrawBreadcrumbs 的注释）。
    //    换成结构性标签：真有的东西是「本章有几个镜」。
    {
        const pages::BookSideView& bs = pages::BookSide();
        const std::string barTitle = "出图流程 · 本章 " + std::to_string(bs.shots.size()) + " 镜";
        draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                      ColorText(), barTitle.data(), barTitle.data() + barTitle.size());
    }
    draw->AddLine(ImVec2(bar.min.x + 196.0f, bar.min.y + 8.0f),
                  ImVec2(bar.min.x + 196.0f, bar.max.y - 8.0f), ColorLineNormal(), 1.0f);
    ButtonSpec smallPrimary;
    smallPrimary.variant = ButtonVariant::Primary;
    smallPrimary.size = ButtonSize::Small;
    const char* submit = "批量出图";
    const float submitW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, submit));
    // ⚠️ 早先这里 `Button(...)` 的**返回值直接丢弃** —— 画了个 primary 主按钮，点了
    //    什么也不发生。同一页 `panelTab_ == 1` 分支里明明写着「批量提交未接」。
    //    改成点了给 toast 把原因说清楚：面板是**可折叠**的（folded_），禁用了按钮
    //    用户就只能看到「按不动」，而按不动的原因写在可能被折起来的面板里。
    if (Button(draw, RectAt(bar.max.x - submitW - 12.0f, bar.center().y - 12.0f, submitW, 24.0f), submit,
               smallPrimary, "if-submit")) {
        WorkspaceToast("批量提交未接 · 业务层没有替前端提交 V4 出图的接口");
    }

    // 浮动面板（右 348px，内缩 16，r14）
    const float panelHeight = folded_ ? 48.0f : std::min(560.0f, area.height() - 32.0f);
    const Rect panel{area.max.x - panelW - 16.0f, area.min.y + 16.0f, area.max.x - 16.0f,
                     area.min.y + 16.0f + panelHeight};
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "link", ImVec2(panel.min.x + 16.0f, panel.min.y + 16.0f), 15.0f, ColorAccent());
    const float headTagW = TagWidth("运行中", false, true);
    Tag(draw, RectAt(panel.max.x - 60.0f - headTagW, panel.min.y + 13.0f, headTagW, 20.0f), "运行中",
        theme::Tone::Accent, false, true);
    if (IconButton(draw, RectAt(panel.max.x - 48.0f, panel.min.y + 12.0f, 24.0f, 24.0f), "chevdown",
                   false, false, "if-fold")) {
        folded_ = !folded_;
    }
    if (folded_) {
        return;
    }

    const std::vector<SegmentOption> tabs{
        {"0", "绑定"}, {"1", "批量出图"}, {"2", "图评审"}, {"3", "结果"}};
    const std::string value = std::to_string(panelTab_);
    const std::string_view picked =
        Segmented(draw,
                  Rect{panel.min.x + 16.0f, panel.min.y + 52.0f, panel.max.x - 16.0f, panel.min.y + 84.0f},
                  tabs, value, "if-tabs");
    if (!picked.empty()) {
        panelTab_ = std::atoi(std::string(picked).c_str());
    }

    const Rect body{panel.min.x + 16.0f, panel.min.y + 94.0f, panel.max.x - 16.0f, panel.max.y - 56.0f};
    if (panelTab_ == 0) {
        // ---- 绑定：Comfy 节点绑定的真状态 ----
        //
        // ⚠️ 早先这里是两行**编出来的**绑定关系：
        //   `{"S012 · 转身", "prompt_v3"} → {"Comfy / ksampler", "workflow.json"}`
        // 镜号、文件名全是设计稿的 mock，而且末尾那句 `缺少 KSampler.seed` 是一个
        // **具体的断言** —— 它声称某个参数字段缺失，而这与打开的是哪本书无关。
        //
        // 真值源：Comfy 会话连没连上（`ComfySession::LastError()`）+ 队列里有没有
        // 任务。连都没连的时候显示什么都能算成「绑定失败」，所以**先说连接状态**，
        // 有连接才列队列里的真实任务；两样都没有就写明为什么没有。
        const comfy::ComfySession& session = comfy::ComfySession::Instance();
        const bool configured = !Settings().comfyBaseUrl.empty();
        const std::vector<comfy::QueueModel::Row> rows = session.Queue().Snapshot();
        if (!configured) {
            Empty(draw, body, "link", "Comfy 未配置",
                  "设置里填 Comfy 地址后这里才有绑定信息。绑定关系来自 Comfy 会话，"
                  "本地编不出来。");
        } else if (!session.LastError().empty()) {
            Empty(draw, body, "link", "Comfy 未连接", session.LastError());
        } else if (rows.empty()) {
            Empty(draw, body, "link", "队列里没有任务",
                  "Comfy 已连接，但队列是空的。绑定关系是提交时建立的，"
                  "这里不编 KSampler / workflow 的对应关系。");
        } else {
            // ⚠️ 原来这里是 `if (y + 24.0f > body.max.y) { break; }`：队列有多少行就只画
            //    看得下的那几行，剩下的**无声消失**，界面上看不出还有第 N+1 个任务 ——
            //    列表被截断且不提示 = 界面在骗人（队列一忙、面板一矮就复现）。
            //    改成画**全部**行；面板自己就是视口，超出由 ScrollRegion 滚。
            ScrollRegion bindScroll("img-bind-queue", body);
            if (bindScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* bdraw = bindScroll.drawList();
                const Rect inner = bindScroll.content();
                float y = inner.min.y;
                for (const comfy::QueueModel::Row& row : rows) {
                    const std::string label = row.label.empty() ? row.promptId : row.label;
                    DrawTextClipped(bdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x, y), 170.0f,
                                    ColorTextSecondary(), label, true);
                    bdraw->AddText(FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 180.0f, y),
                                   ColorTextMuted(), "→", "→" + 3);
                    DrawTextClipped(bdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 210.0f, y),
                                    inner.width() - 210.0f, ColorText(),
                                    row.state == comfy::TaskState::Running ? "运行中" : "排队中",
                                    true);
                    y += 24.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                bindScroll.setContentHeight(y - inner.min.y);
            }
        }
    } else if (panelTab_ == 1) {
        // ---- 批量出图：候选镜来自**本章真实镜表**，提交动作不接线 ----
        //
        // ⚠️ 这个分支以前**根本不存在**：页签有 4 个（绑定/批量出图/图评审/结果），
        //    分发却只有 `if 0 / else if 2 / else` ⇒ 「批量出图」落进 else，看到的是
        //    **「结果」页的尺寸/步数/种子/耗时**。症状是「点了页签没反应 / 串页」，
        //    而那一瞬间的截图里两个页签完全一样，肉眼分辨不出是串页还是没做。
        //
        // 候选镜用 `BookSide().shots`（当前选中章，与侧栏树/故事板同一份），状态取
        // 镜自己的 canon_status。**不编批次号、不编张数** —— 批量提交要走 Comfy
        // 写接口，业务层没有替前端提交 V4 的只读投影，所以这里只列得出候选，提交
        // 一行明说没接，而不是放一个点了什么都不做的按钮。
        const BookSideView& bs = BookSide();
        if (!bs.bound || !bs.error.empty() || bs.shots.empty()) {
            Empty(draw, body, "layers",
                  bs.bound ? "本章还没有镜" : "还没打开工程",
                  bs.bound ? "T1–T17 跑出分镜后这里才有可批量出图的候选。"
                           : "批量出图的候选来自 shots 表，先打开一个跑过初始化链的工程。");
        } else {
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.min.y),
                            body.width(), ColorTextMuted(),
                            ("本章 " + std::to_string(bs.shots.size()) + " 个镜 · 候选来源 shots 表").c_str(),
                            true);
            DrawRoundRect(draw, ImVec2(body.min.x, body.min.y + 22.0f),
                          ImVec2(body.max.x, body.min.y + 58.0f), 6.0f,
                          ColorOf(theme::CurrentDerived()
                                      .tagBg[static_cast<std::size_t>(theme::Tone::Warn)]));
            const char* noSubmit = "批量提交未接：业务层没有替前端提交 V4 出图的接口，这一页只列候选。";
            draw->AddText(FontAt(12.0f), 12.0f, ImVec2(body.min.x + 10.0f, body.min.y + 34.0f),
                          ColorOf(theme::Current().statusWarn), noSubmit,
                          noSubmit + std::strlen(noSubmit));
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 一章几十个镜时只画
            //    看得下的一半，候选镜**无声消失**，界面上看不出「本章还有 N 个候选」。
            //    列表被截断且不提示 = 界面在骗人。改成画**全部**候选。
            //    「本章 N 个镜」说明与「批量提交未接」的警告条留在滚动区**外面**。
            ScrollRegion candScroll("img-batch-candidates",
                                    Rect{body.min.x, body.min.y + 70.0f, body.max.x, body.max.y});
            if (candScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* cdraw = candScroll.drawList();
                const Rect inner = candScroll.content();
                float y = inner.min.y;
                for (const BookShotView& shot : bs.shots) {
                    const std::string code = ShotCode(shot.ord);
                    cdraw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(inner.min.x, y), ColorAccent(),
                                   code.data(), code.data() + code.size());
                    const std::string action = shot.action.empty() ? std::string(kDash) : shot.action;
                    DrawTextClipped(cdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 52.0f, y),
                                    inner.width() - 180.0f, ColorText(), action, true);
                    const std::string canon =
                        shot.canonStatus.empty() ? std::string(kDash) : shot.canonStatus;
                    const float tagW = TagWidth(canon, false, true);
                    Tag(cdraw, RectAt(inner.max.x - tagW, y - 1.0f, tagW, 20.0f), canon,
                        theme::Tone::Idle, false, true);
                    y += 26.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                candScroll.setContentHeight(y - inner.min.y);
            }
        }
    } else if (panelTab_ == 2) {
        // ---- 图评审 ----
        //
        // ⚠️ 这里原来是一个 5 行的假评审表，三处同时是假的：
        //   ① `Checkbox` 的返回值被丢弃 ⇒ 点不动（注册了 InvisibleButton 却没人读）；
        //   ② `on` 传字面量 `i < 3` ⇒ 勾选态**每帧重建**，永远是「前 3 个勾上」；
        //   ③ 五行的 label 都是同一个字符串「构图稳定」⇒ 标签叠印成一坨。
        // 「勾上 3 个、5 个同名、点不动」的三件套在静息态截图上看着还挺像个评审页 ——
        // 所以是取证对照才发现的，不是肉眼。
        //
        // 改成 honest：业务层没有出图评审结果的只读投影（`visual_assets.status` 只到
        // 生产状态，没有 per-check 的评分），所以**不编 5 条评审项**。有真实视觉资产的
        // 实体就列出来（名字 + 生产状态），一条都没有就说明为什么没有。
        const BookSideView& rv = BookSide();
        Art(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 150.0f}, 6, true);
        // 插画 150 高之后留够落笔空间：Art 的实际下沿比 rect 略高一点，早先 +162 的
        // 间距会让第一行名字压在插画下沿上（截图里能直接看出来）。
        const float listTop = body.min.y + 180.0f;
        // 先把「真的列得出来」的实体收齐：有资产才进清单，否则空态才说得清。
        std::vector<const BookAssetView*> listed;
        for (const BookAssetView& asset : rv.assets) {
            if (asset.hasAsset) {
                listed.push_back(&asset);
            }
        }
        if (listed.empty()) {
            Empty(draw, Rect{body.min.x, listTop, body.max.x, body.max.y}, "target",
                  "没有可评审的出图", rv.bound
                                     ? "这一类实体还没有视觉资产。先在资产工作区出图，再回来评审。"
                                     : "还没打开工程。评审清单来自 entities × visual_assets。");
        } else {
            // ⚠️ 原来这里是 `if (rowY + 22.0f > body.max.y) { break; }` —— 清单比面板高时
            //    多出来的资产**无声消失**，界面上看不出「还有 N 项没列」：列表被截断且不
            //    提示 = 界面在骗人。改成画**全部**，行本体自己滚；插画与页脚留在区外。
            ScrollRegion reviewScroll("img-review-assets",
                                      Rect{body.min.x, listTop, body.max.x, body.max.y});
            if (reviewScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* rdraw = reviewScroll.drawList();
                const Rect inner = reviewScroll.content();
                // y 依次累加（不再用全量下标 × 24）：跳过的实体不会在中间留空洞。
                float ly = inner.min.y;
                for (const BookAssetView* asset : listed) {
                    const std::string name = asset->name.empty() ? std::string(kDash) : asset->name;
                    const std::string status =
                        asset->statusLabel.empty() ? std::string(kDash) : asset->statusLabel;
                    DrawTextClipped(rdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x, ly),
                                    inner.width() - 120.0f, ColorText(), name, true);
                    const float tagW = TagWidth(status, false, true);
                    Tag(rdraw, RectAt(inner.max.x - tagW, ly - 1.0f, tagW, 20.0f), status,
                        asset->tone, false, true);
                    ly += 24.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                reviewScroll.setContentHeight(ly - inner.min.y);
            }
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.max.y - 16.0f),
                            body.width(), ColorTextMuted(),
                            "逐项评分（构图/光线/服饰/色彩）没有只读投影，这里只列生产状态。", true);
        }
    } else {
        // ---- 结果 ----
        //
        // ⚠️ 早先这里是
        //   `KeyValues({{"尺寸","1024x576"},{"步数","28"},{"种子","1289471"},{"耗时","12.4s"}})`
        //    四个**编出来的**生成参数。业务层没有出图结果的只读投影 —— visual_assets
        //    只有 status（生产状态），没有尺寸/步数/种子/耗时这些字段。所以这四个数字
        //    没有任何一个是真的，却长得极像一次真实的出图结果。
        //
        // 改成列 BookAssetView 里**真有的**字段：名字 / 生产状态 / 层数完成度 / 是否降级。
        // 一条视觉资产都没有时说清楚为什么没有，不拿 Art 占位图 + 假参数撑场面。
        const BookSideView& rv = BookSide();
        Art(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 150.0f}, 6, true);
        const float listTop = body.min.y + 180.0f;
        std::vector<const BookAssetView*> listed;
        for (const BookAssetView& asset : rv.assets) {
            if (asset.hasAsset) {
                listed.push_back(&asset);
            }
        }
        if (listed.empty()) {
            Empty(draw, Rect{body.min.x, listTop, body.max.x, body.max.y}, "check",
                  "这一类实体还没有出图结果",
                  rv.bound ? "visual_assets 里还没有任何已产出的图像。业务层也没有出图结果的"
                             "只读投影，所以这里不列参数。"
                           : "还没打开工程。");
        } else {
            // ⚠️ 原来这里是 `if (rowY + 22.0f > body.max.y) { break; }` —— 出图一多，
            //    后面的结果行**无声消失**，界面上看不出「还有 N 张没列」：列表被截断且不
            //    提示 = 界面在骗人。改成画**全部**，行本体自己滚；插画留在滚动区外。
            ScrollRegion resultScroll("img-result-assets",
                                      Rect{body.min.x, listTop, body.max.x, body.max.y});
            if (resultScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* gdraw = resultScroll.drawList();
                const Rect inner = resultScroll.content();
                float ly = inner.min.y;
                for (const BookAssetView* asset : listed) {
                    const std::string name = asset->name.empty() ? std::string(kDash) : asset->name;
                    const std::string status =
                        asset->statusLabel.empty() ? std::string(kDash) : asset->statusLabel;
                    DrawTextClipped(gdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x, ly),
                                    inner.width() - 200.0f, ColorText(), name, true);
                    // 层数完成度是真的（layers / layersDone 来自 visual_assets 的分层表）。
                    const std::string layers =
                        asset->layers > 0 ? ("分层 " + std::to_string(asset->layersDone) + "/" +
                                             std::to_string(asset->layers))
                                          : std::string(kDash);
                    DrawTextClipped(gdraw, FontAt(11.5f), 11.5f, ImVec2(inner.max.x - 190.0f, ly + 3.0f),
                                    90.0f, ColorTextMuted(), layers, true);
                    const std::string flag = asset->degraded ? "已降级" : "完整";
                    const float fw = TagWidth(flag, false, true);
                    Tag(gdraw, RectAt(inner.max.x - fw, ly - 1.0f, fw, 20.0f), flag,
                        asset->degraded ? theme::Tone::Warn : theme::Tone::Ok, false, true);
                    ly += 24.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                resultScroll.setContentHeight(ly - inner.min.y);
            }
        }
    }

    // 画布工具（左下竖排）
    const float toolX = area.min.x + 16.0f;
    float toolY = area.max.y - 16.0f - 3.0f * 32.0f;
    for (const char* icon : {"plus", "minus", "grid"}) {
        const kit::Rect tool = RectAt(toolX, toolY, 28.0f, 28.0f);
        DrawRoundRect(draw, tool.min, tool.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
        DrawIconCentered(draw, icon, ImVec2(toolX + 14.0f, toolY + 14.0f), 14.0f, ColorTextSecondary());
        toolY += 32.0f;
    }
}

// ================================================================ P5.6 出片
void VideoFlowPage::Draw(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorFillMuted());
    DrawDotGrid(draw, area.min, area.max, 22.0f, ColorLineNormal());

    // 与出图页同构：右侧浮动面板 348+16，画布让出这块再 fit
    const float panelW = 348.0f;
    const float canvasRight = area.max.x - panelW - 32.0f;
    const Rect canvas{area.min, ImVec2(std::max(canvasRight, area.min.x + 200.0f), area.max.y)};
    // 同出图页：数据指纹变了就重建。
    if (flowNodes_.empty() || flowDataKey_ != FlowDataKey()) {
        BuildGraph();
    }
    if (g_videoFlowNeedsFit) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        g_videoFlowNeedsFit = false;
    }
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f);

    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 460.0f, area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "film", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    const char* barTitle = "出片流程 · H3 视频";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                  ColorText(), barTitle, barTitle + std::strlen(barTitle));

    const Rect panel{area.max.x - panelW - 16.0f, area.min.y + 16.0f, area.max.x - 16.0f,
                     area.min.y + 16.0f + std::min(520.0f, area.height() - 140.0f)};
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f);
    const float headTagW = TagWidth("出片中", false, true);
    Tag(draw, RectAt(panel.max.x - 16.0f - headTagW, panel.min.y + 13.0f, headTagW, 20.0f), "出片中",
        theme::Tone::Accent, false, true);

    const std::vector<SegmentOption> tabs{{"0", "首尾帧链"}, {"1", "视频任务"}, {"2", "成片"}};
    const std::string value = std::to_string(panelTab_);
    const std::string_view picked =
        Segmented(draw,
                  Rect{panel.min.x + 16.0f, panel.min.y + 48.0f, panel.max.x - 16.0f, panel.min.y + 80.0f},
                  tabs, value, "vf-tabs");
    if (!picked.empty()) {
        panelTab_ = std::atoi(std::string(picked).c_str());
    }
    const Rect body{panel.min.x + 16.0f, panel.min.y + 90.0f, panel.max.x - 16.0f, panel.max.y - 16.0f};
    if (panelTab_ == 0) {
        // ---- 首尾帧链 ----
        //
        // ⚠️ 早先这里循环 3 次造 `"S0" + (10+i) + " -> S0" + (11+i)`，连接状态写死
        //    `i == 1 ? "断链" : "已连接"` —— 镜号是编的，断链也是编的（与本工程无关）。
        //    真值源：BookSide().shots 里**当前选中章相邻的两个镜**。
        //    「断链」这个结论需要判定帧链连通性，业务层没有这个只读投影，所以不编 ——
        //    改列每个镜自己的 canonStatus（真字段），那才是真知道的东西。
        const BookSideView& cv = BookSide();
        if (cv.shots.size() < 2) {
            Empty(draw, body, "link",
                  cv.bound ? "本章还不足两个镜" : "还没打开工程",
                  cv.bound ? "首尾帧链是**相邻两镜**之间的关系，至少要两个镜才画得出来。"
                           : "镜列表来自 novel.db 的 shots 表。");
        } else {
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 一章的镜一多，
            //    后面的帧链对**无声消失**，界面上看不出「本章还有 N 对相邻镜没列」：
            //    列表被截断且不提示 = 界面在骗人。改成画**全部**对。
            //    底部那句说明留在滚动区**外面**（区高留出 24，不让它压住页脚）。
            ScrollRegion chainScroll("vf-frame-chain",
                                     Rect{body.min.x, body.min.y, body.max.x, body.max.y - 24.0f});
            if (chainScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* chdraw = chainScroll.drawList();
                const Rect inner = chainScroll.content();
                const std::size_t pairs = cv.shots.size() - 1;
                float y = inner.min.y;
                for (std::size_t i = 0; i < pairs; ++i) {
                    const BookShotView& from = cv.shots[i];
                    const BookShotView& to = cv.shots[i + 1];
                    const std::string label =
                        ShotCode(from.ord) + " → " + ShotCode(to.ord);
                    chdraw->AddText(FontAt(12.5f), 12.5f, ImVec2(inner.min.x, y),
                                    ColorTextSecondary(), label.data(), label.data() + label.size());
                    // 右侧标签显示**后一个镜**自己的设定状态 —— 真字段，不臆断链路通断。
                    const std::string canon =
                        to.canonStatus.empty() ? std::string(kDash) : to.canonStatus;
                    const float tw = TagWidth(canon, false, false);
                    Tag(chdraw, RectAt(inner.max.x - tw, y - 3.0f, tw, 20.0f), canon,
                        theme::Tone::Idle, false, false);
                    y += 26.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                chainScroll.setContentHeight(y - inner.min.y);
            }
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.max.y - 16.0f),
                            body.width(), ColorTextMuted(),
                            "标签是后一个镜的设定状态；帧链是否断需要连续性报告，"
                            "本页不臆断。", true);
        }
    } else if (panelTab_ == 1) {
        // ---- 视频任务：走 Comfy 队列的**真实快照** ----
        //
        // ⚠️ 这里原来画 4 条 `Progress(20/40/60/80, running = i == 0)` —— 写死的算术级数，
        //    与真实运行态毫无关系。界面上永远显示「第一条在跑、其余到 80%」，而底栏
        //    任务队列早就改用 `QueueModel::Row::progress` 的真值了，同一个队列两个口径。
        // 「看起来很像在跑」是这类假数据最难认的地方。
        //
        // 真值源与底栏任务队列**同一份**（`comfy::ComfySession` 的 `QueueModel`），
        // 不在这里另接一条。队列空就是空，不补 4 条占位。
        const std::vector<comfy::QueueModel::Row> rows =
            comfy::ComfySession::Instance().Queue().Snapshot();
        // ⚠️ 这里不能 `return`：胶片条画在页签分支**之外**，早退会把那一条也一起跳过。
        if (rows.empty()) {
            Empty(draw, body, "film", "队列里没有视频任务",
                  "视频任务来自 ComfyUI 队列。取真实数据源：comfy::ComfySession::Queue()。");
        } else {
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 队列里的任务
            //    一多，后面的**无声消失**，界面上看不出还有第 N+1 个在排队：列表被截断
            //    且不提示 = 界面在骗人。改成画**全部**行。
            ScrollRegion queueScroll("vf-video-queue", body);
            if (queueScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* qdraw = queueScroll.drawList();
                const Rect inner = queueScroll.content();
                float y = inner.min.y;
                for (const comfy::QueueModel::Row& row : rows) {
                    const bool running = row.state == comfy::TaskState::Running;
                    const bool failed = row.state == comfy::TaskState::Failed;
                    const std::string code = row.label.empty() ? row.promptId : row.label;
                    DrawTextClipped(qdraw, MonoAt(12.0f), 12.0f, ImVec2(inner.min.x, y), 160.0f,
                                    failed ? ColorOf(theme::Current().statusDanger)
                                           : ColorTextSecondary(),
                                    code, true);
                    Progress(qdraw, RectAt(inner.min.x + 170.0f, y + 2.0f, inner.width() - 210.0f, 6.0f),
                             row.progress * 100.0f, running, true);
                    y += 26.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                queueScroll.setContentHeight(y - inner.min.y);
            }
        }
    } else {
        // ---- 成片 ----
        //
        // ⚠️ 早先这里是 `KeyValues({{"分辨率","1920x1080"},{"帧率","24fps"},
        //    {"帧数","112"},{"缺失镜头","S013"}})` —— 四个**编出来的**成片参数，
        //    连「缺失镜头 S013」这种具体断言都是假的。
        // 业务层没有成片产物的只读投影（没有 video 文件的扫描接口接到这一层），
        // 所以这里只说清楚「真知道什么」：本章有几个镜、每个镜多长。
        const BookSideView& fv = BookSide();
        if (fv.shots.empty()) {
            Empty(draw, body, "film", fv.bound ? "本章没有镜" : "还没打开工程",
                  fv.bound ? "成片要由镜生成，业务层还没有把成片产物的只读投影接到这一层。"
                           : "镜列表来自 novel.db 的 shots 表。");
        } else {
            std::vector<std::pair<std::string, std::string>> kv;
            kv.emplace_back("本章镜数", std::to_string(fv.shots.size()));
            int totalSec = 0;
            for (const BookShotView& shot : fv.shots) {
                totalSec += shot.durationSec;
            }
            kv.emplace_back("总时长", std::to_string(totalSec) + "s");
            kv.emplace_back("视觉资产", std::to_string(static_cast<int>([&] {
                int n = 0;
                for (const BookAssetView& a : fv.assets) {
                    if (a.hasAsset) {
                        ++n;
                    }
                }
                return n;
            }())) + " 项");
            KeyValues(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f}, kv);
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.min.y + 110.0f),
                            body.width(), ColorTextMuted(),
                            "分辨率 / 帧率 / 缺失镜头都没有只读投影，这里不编。", true);
        }
    }

    // 底部胶片条：left16 / right384，格宽 118 + 6 间距。
    //
    // ⚠️ 早先这里无条件循环 6 次，`const bool ready = i < 3;` —— 前 3 格画缩略图 +
    // 编出来的镜号 `S010..S012` + 编出来的 `24fps` + 一个绿点，后 3 格写「待出片」。
    // 整个条与本工程毫无关系。而且 1711 行的注释还写着「队列空就是空，不补 6 条占位」
    // —— 面板那边已经改成真值源了，胶片条这边还是补 6 条。
    //
    // 真值源：BookSide().shots（当前选中章的镜）。格数跟着镜数走，镜号 / 时长 / 设定
    // 状态都是真字段。**没有镜就整条不出**（保留容器与标题，写明为什么空），
    // 而不是拿 6 个编出来的格子撑场面。
    const Rect strip{area.min.x + 16.0f, area.max.y - 92.0f, area.max.x - 384.0f, area.max.y - 16.0f};
    DrawShadowed(draw, strip.min, strip.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f);
    const char* stripTitle = "成片";
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(strip.min.x + 14.0f, strip.min.y + 12.0f),
                  ColorTextMuted(), stripTitle, stripTitle + std::strlen(stripTitle));
    const BookSideView& sv = BookSide();
    if (sv.shots.empty()) {
        static constexpr char kNoShots[] = "本章没有镜 · 成片条按镜数生成，不补占位格";
        draw->AddText(FontAt(11.5f), 11.5f,
                      ImVec2(strip.min.x + 52.0f, strip.center().y - 5.75f), ColorTextMuted(),
                      kNoShots, kNoShots + sizeof(kNoShots) - 1);
    } else {
        float x = strip.min.x + 52.0f;
        for (const BookShotView& shot : sv.shots) {
            if (x + 118.0f > strip.max.x - 8.0f) {
                break;  // 放不下就不画，不压缩也不重叠
            }
            const Rect cell{x, strip.min.y + 8.0f, x + 118.0f, strip.max.y - 8.0f};
            DrawRoundRect(draw, cell.min, cell.max, 8.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
            Art(draw, Rect{cell.min.x + 6.0f, cell.min.y + 6.0f, cell.min.x + 62.0f, cell.max.y - 6.0f},
                shot.ord, true);
            const std::string code = ShotCode(shot.ord);
            draw->AddText(MonoAt(10.5f), 10.5f, ImVec2(cell.min.x + 70.0f, cell.min.y + 12.0f),
                          ColorAccent(), code.data(), code.data() + code.size());
            // 时长是 BookShotView 的真字段（durationSec），不是写死的 24fps。
            const std::string dur = std::to_string(shot.durationSec) + "s";
            draw->AddText(FontAt(10.5f), 10.5f, ImVec2(cell.min.x + 70.0f, cell.min.y + 28.0f),
                          ColorTextMuted(), dur.data(), dur.data() + dur.size());
            // 圆点色调跟着 canonStatus 的空/非空走：设定状态是判据，没有就画中性灰，
            // 不画成「已完成」的绿 —— 绿点在胶片条上会被读成「这一格出好了」。
            const bool hasCanon = !shot.canonStatus.empty();
            StatusDot(draw, ImVec2(cell.min.x + 74.0f, cell.min.y + 44.0f),
                      hasCanon ? theme::Tone::Accent : theme::Tone::Idle, false);
            x += 124.0f;
        }
    }
}

// ================================================================ P4.11 项目中心
// 数据全部来自 shine::project，本页不再有任何编造的卡片：
//   最近列表 = ProjectService::Recent()  —— 读 %APPDATA%/ShineTVStudio/projects.json
//   卡片副行 = project.json 的 premise；封面标签 / meta = project.json 的 templateId
//   打开     = ProjectService::Open()    —— 真读 project.json + 置顶索引
//   新建     = ProjectService::Create()  —— 真落骨架（ValidateSpec + Materialize）
//   移除     = ProjectIndex::Remove()    —— 只摘登记，不删项目文件
// 业务层返回空就是空态，**不补假卡**。
//
// I/O 口径：ProjectService::Recent() 每次调用都重读索引文件（Project.h 有意如此），
// 所以卡片只在本文件首次进入 / 一次写操作之后刷新，不在每帧读盘。
namespace {

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
    std::string confirmTitle;
    std::string confirmBody;
    std::string confirmOk = "确定";
    std::string pendingId;
    std::string status;
    bool statusError = false;
};

HubState& Hub() {
    static HubState state;
    return state;
}

// 封面种子跟着项目 id 走：跨帧、跨排序、跨过滤都是同一张封面。
int SeedOf(std::string_view id) {
    std::uint32_t hash = 2166136261u;
    for (const char ch : id) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619u;
    }
    return static_cast<int>(hash % 12u); // Art 只有 12 组调色板
}

// project.json 的 templateId → 封面短标签（设计稿：小说 / 影视 / 空白）。
std::string TplLabel(std::string_view templateId) {
    if (templateId == "novel") {
        return "小说";
    }
    if (templateId == "film") {
        return "影视";
    }
    if (templateId == "blank") {
        return "空白";
    }
    return {};
}

// 模板 id → 图标（向导第 1 步每行一个）。
const char* TemplateIcon(std::string_view templateId) {
    if (templateId == "novel") {
        return "book";
    }
    if (templateId == "film") {
        return "film";
    }
    return "folder";
}

// 民用日期 → 天序（Hinnant）。只为算「今天 / 昨天 / N 天前」的真实天数差。
long long DayNumber(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const int yoe = year - static_cast<int>(era * 400);
    const int mp = month + (month > 2 ? -3 : 9);
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// lastOpened 的相对时间：今天 HH:mm / 昨天 HH:mm / N 天前 / 更早给日期。
// 数据来自 RecentEntry::lastOpened（口径照 Qt 版 ProjectHubView.cpp:98）。
std::string RelativeTime(std::chrono::system_clock::time_point tp) {
    const std::time_t when = std::chrono::system_clock::to_time_t(tp);
    const std::tm* lt = std::localtime(&when);
    if (lt == nullptr) {
        return {};
    }
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::tm* ln = std::localtime(&now);
    const long long days =
        (ln != nullptr ? DayNumber(ln->tm_year + 1900, ln->tm_mon + 1, ln->tm_mday) : 0) -
        DayNumber(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    char buf[32] = {};
    if (days <= 0) {
        std::snprintf(buf, sizeof buf, "今天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days == 1) {
        std::snprintf(buf, sizeof buf, "昨天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days < 7) {
        std::snprintf(buf, sizeof buf, "%lld 天前", days);
    } else {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1,
                      lt->tm_mday);
    }
    return buf;
}

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

// UTF-8 字符数：设计稿的「200 字」上限按字符算，不是字节。
std::size_t CharCount(std::string_view text) {
    std::size_t count = 0;
    for (const char ch : text) {
        if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// 重新拉最近列表（Recent() 每次都重读索引，所以只在需要时调）。
void RefreshHub(HubState& hub) {
    hub.cards.clear();
    for (const project::RecentEntry& entry : hub.service.Recent()) {
        HubCard card;
        card.entry = entry;
        card.artSeed = SeedOf(entry.id);
        card.when = RelativeTime(entry.lastOpened);
        if (const std::expected<project::ProjectFile, project::Error> file =
                project::LoadProjectFile(entry.rootDir);
            file.has_value()) {
            card.readable = true;
            card.premise = std::string(util::Trim(file->premise));
            card.tplLabel = TplLabel(file->templateId);
            if (const project::ProjectTemplate* tpl = project::FindTemplate(file->templateId);
                tpl != nullptr) {
                card.tplName = tpl->name;
            }
        }
        hub.cards.push_back(std::move(card));
    }
    if (hub.sort == "n") { // 名称
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.name < b.entry.name;
        });
    } else { // 最近打开
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.lastOpened > b.entry.lastOpened;
        });
    }
    hub.loaded = true;
}

// 搜索过滤：项目名或一句话创意命中即可（webui 匹配 name + desc）。
void RefilterHub(HubState& hub) {
    const std::string key = LowerAscii(util::Trim(hub.query));
    hub.shown.clear();
    for (std::size_t i = 0; i < hub.cards.size(); ++i) {
        const HubCard& card = hub.cards[i];
        if (key.empty() || LowerAscii(card.entry.name).find(key) != std::string::npos ||
            LowerAscii(card.premise).find(key) != std::string::npos) {
            hub.shown.push_back(static_cast<int>(i));
        }
    }
}

// 打开项目：真 Open（读 project.json + 置顶索引），失败把业务层的中文 message 摆出来。
void OpenHubCard(HubState& hub, const HubCard& card) {
    const std::expected<project::ProjectRef, project::Error> ref =
        hub.service.Open(card.entry.rootDir);
    if (!ref) {
        hub.status = "打开项目失败：" + ref.error().message;
        hub.statusError = true;
        return;
    }
    hub.status = "已打开项目「" + ref->name + "」· " + util::PathToUtf8(ref->rootDir);
    hub.statusError = false;
    RefreshHub(hub);
}

// 从最近列表移除：ProjectService 没这个接口，走 ProjectIndex（只摘登记，不删文件）。
void RemoveHubCard(HubState& hub, const std::string& id) {
    project::ProjectIndex index;
    (void)index.Load(); // 坏文件也已回退空索引
    (void)index.Remove(id);
    if (const std::expected<void, project::Error> saved = index.Save(); !saved) {
        hub.status = "移除未落盘：" + saved.error().message;
        hub.statusError = true;
    } else {
        hub.status = "已从最近列表移除（项目文件未删除）";
        hub.statusError = false;
    }
    RefreshHub(hub);
}

// 主题轮转（pfoot 的 palette 按钮）。动作与 Shell::SetTheme 一致 ——
// 水墨换衬线族必须重建字体图集，否则中文缺字。
void CycleHubTheme(HubState& hub) {
    const auto it =
        std::find(theme::kAllThemes.begin(), theme::kAllThemes.end(), theme::CurrentThemeId());
    const std::size_t index =
        (it == theme::kAllThemes.end()) ? 0 : static_cast<std::size_t>(it - theme::kAllThemes.begin());
    const theme::ThemeId next = theme::kAllThemes[(index + 1) % theme::kAllThemes.size()];
    theme::ApplyTheme(next);
    if (theme::ThemeUsesSerif(next)) {
        if (!BuildFontAtlas(/*serif=*/true)) {
            shine::log::Error("serif font atlas rebuild failed — falling back to sans, 文字可能缺字");
        }
        theme::ApplyCurrentTheme();
    }
    (void)theme::PersistTheme(theme::DefaultThemeFile());
    hub.status = "主题已切到「" + std::string(theme::ThemeDisplayName(next)) + "」";
    hub.statusError = false;
}

// 向导第 2 步的「浏览…」：系统选目录（Win32 边界调用，按钮触发一次即返回）。
std::string PickFolder() {
    BROWSEINFOW info = {};
    info.hwndOwner = reinterpret_cast<HWND>(ImGui::GetMainViewport()->PlatformHandle);
    info.ulFlags = BIF_RETURNONLYFSDIRS;
    info.lpszTitle = L"选择项目位置";
    PIDLIST_ABSOLUTE picked = SHBrowseForFolderW(&info);
    if (picked == nullptr) {
        return {}; // 用户取消
    }
    wchar_t buffer[MAX_PATH] = {};
    const bool ok = SUCCEEDED(SHGetPathFromIDListW(picked, buffer));
    CoTaskMemFree(picked);
    return ok ? util::PathToUtf8(std::filesystem::path{buffer}) : std::string{};
}

// 项目将创建到的目录：<位置>\<项目名>（名字空就还没有目标）。
std::filesystem::path HubTargetDir(const HubState& hub) {
    if (util::Trim(hub.dir).empty() || util::Trim(hub.name).empty()) {
        return {};
    }
    return util::PathFromUtf8(util::Trim(hub.dir)) / util::PathFromUtf8(util::Trim(hub.name));
}

struct ModalBox {
    Rect frame;
    Rect header;
    Rect body;
    Rect footer;
};

// 弹窗外壳：scrim + 圆角面板 + 头 / 体 / 脚（ProjectHub.jsx 的 .modal）。
ModalBox DrawModal(ImDrawList* draw, Rect area, float width, float height) {
    const float top = area.height() * 0.15f;
    const Rect frame{(area.width() - width) * 0.5f, top, (area.width() + width) * 0.5f, top + height};
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorScrim());
    DrawShadowed(draw, frame.min, frame.max, 14.0f, ColorOverlay(), ColorLineNormal(), 1.0f);
    constexpr float headerH = 52.0f;
    constexpr float footerH = 60.0f;
    ModalBox box;
    box.frame = frame;
    box.header = RectAt(frame.min.x, frame.min.y, frame.width(), headerH);
    box.body = RectAt(frame.min.x, frame.min.y + headerH, frame.width(),
                      frame.height() - headerH - footerH);
    box.footer = RectAt(frame.min.x, frame.max.y - footerH, frame.width(), footerH);
    return box;
}

void DrawModalTitle(ImDrawList* draw, Rect header, const char* icon, const char* title) {
    DrawIcon(draw, icon, ImVec2(header.min.x + 18.0f, header.center().y - 7.0f), 14.0f, ColorAccent());
    draw->AddText(FontBoldAt(15.0f), 15.0f, ImVec2(header.min.x + 40.0f, header.center().y - 7.5f),
                  ColorText(), title, title + std::strlen(title));
    DrawRoundRect(draw, ImVec2(header.min.x, header.max.y - 1.0f), ImVec2(header.max.x, header.max.y),
                  0.0f, ColorLineSubtle());
}

struct ModalAction {
    std::string_view label;
    ButtonSpec spec;
    std::string_view id;
};

// 弹窗页脚：按钮右对齐（.modal-f 是「提示 + spacer + 按钮组」）。
// 返回每个按钮本帧是否被点。
std::vector<int> DrawModalFooter(ImDrawList* draw, Rect footer,
                                 const std::vector<ModalAction>& actions) {
    std::vector<int> hit(actions.size(), 0);
    float x = footer.max.x - 20.0f;
    for (std::size_t i = actions.size(); i-- > 0;) {
        const ModalAction& action = actions[i];
        const std::string label(action.label);
        const float textSize = action.spec.size == ButtonSize::Small ? 12.0f : 13.0f;
        ImFont* font = action.spec.variant == ButtonVariant::Primary ? FontBoldAt(textSize)
                                                                    : FontAt(textSize);
        const float iconW = action.spec.icon.empty()
                                ? 0.0f
                                : (action.spec.size == ButtonSize::Small ? 13.0f : 15.0f);
        const float width =
            ButtonWidth(action.spec.size, iconW, LabelWidth(font, textSize, label.c_str()));
        const float height = ButtonHeight(action.spec.size);
        const Rect bounds{x - width, footer.center().y - height * 0.5f, x,
                          footer.center().y + height * 0.5f};
        hit[i] = Button(draw, bounds, label, action.spec, action.id) ? 1 : 0;
        x -= width + 8.0f;
    }
    return hit;
}

// 卡片：整卡可点 + pfoot 的四个入口（打开 / 资源管理器 / 更多 / 主题）。
// 返回 true = 本帧通过整卡或「打开」按钮触发了打开。
bool DrawHubCard(ImDrawList* draw, HubState& hub, const HubCard& card, Rect bounds,
                 float bodyWidth) {
    // 整卡命中先登记：页脚按钮后登记，ImGui 里后命中的 item 优先，
    // 效果等价于 webui 的 e.stopPropagation()（点「移除」不会顺手打开项目）。
    const Hit hit = HitTest(bounds, "hub-card-" + card.entry.id);
    constexpr float radius = 10.0f;
    DrawShadowed(draw, bounds.min, bounds.max, radius, ColorPanel(),
                 hit.hovered ? ColorAccentGlow() : ColorLineSubtle(), 1.0f);

    // 封面满幅（views.css:82 .cover 无内缩）。这版 ImGui 没有 PushClipPath，
    // 卡片顶部的两个圆角用同色三角补掉。
    draw->PushClipRect(bounds.min, bounds.max, true);
    Art(draw, Rect{bounds.min.x, bounds.min.y, bounds.max.x, bounds.min.y + 120.0f}, card.artSeed,
        true);
    draw->PopClipRect();
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x + radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x - radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    if (!card.tplLabel.empty()) {
        Tag(draw, RectAt(bounds.min.x + 10.0f, bounds.min.y + 10.0f, TagWidth(card.tplLabel, true, false),
                         TagHeight(true)),
            card.tplLabel, theme::Tone::Accent, true);
    }

    const float bodyX = bounds.min.x + 14.0f;
    float y = bounds.min.y + 120.0f + 12.0f;
    // 项目名：索引里的 name（与 project.json 同源）
    DrawTextClipped(draw, FontBoldAt(14.5f), 14.5f, ImVec2(bodyX, y), bodyWidth, ColorText(),
                    card.entry.name);
    y += 21.0f;
    // 副行：一句话创意。没有就整行留空 —— 不拿模板名之类的字段顶替
    if (!card.premise.empty()) {
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX, y), bodyWidth, ColorTextMuted(),
                        card.premise);
    }
    y += 21.0f;
    // meta：模板名 · lastOpened 相对时间
    DrawIcon(draw, "book", ImVec2(bodyX, y + 2.0f), 12.0f, ColorTextMuted());
    const std::string meta =
        card.tplName.empty() ? card.when : (card.tplName + " · " + card.when);
    DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX + 20.0f, y), bodyWidth - 20.0f,
                    ColorTextMuted(), meta);
    y += 20.0f;
    DrawRoundRect(draw, ImVec2(bodyX, y), ImVec2(bounds.max.x - 14.0f, y + 1.0f), 0.0f,
                  ColorLineSubtle());
    y += 11.0f;

    bool open = false;
    ButtonSpec openSpec;
    openSpec.variant = ButtonVariant::Primary;
    openSpec.size = ButtonSize::Small;
    const float openW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
    const Rect openRect{bodyX, y, bodyX + openW, y + ButtonHeight(ButtonSize::Small)};
    const bool openPressed =
        Button(draw, openRect, "打开", openSpec, "hub-open-" + card.entry.id);

    float fx = openRect.max.x + 6.0f;
    const float iconSize = 22.0f;
    const float iconH = ButtonHeight(ButtonSize::Small);
    const bool revealPressed =
        IconButton(draw, RectAt(fx, y, iconSize, iconH), "folder", false, false,
                   "hub-reveal-" + card.entry.id, "在资源管理器中显示");
    fx += iconSize + 6.0f;
    const bool morePressed = IconButton(draw, RectAt(fx, y, iconSize, iconH), "dots", false, false,
                                        "hub-more-" + card.entry.id, "更多");
    const bool themePressed = IconButton(
        draw, RectAt(bounds.max.x - 14.0f - iconSize, y, iconSize, iconH), "palette", false, false,
        "hub-theme-" + card.entry.id, "切换主题");

    // 整卡点击要扣掉页脚按钮：ImGui 的 IsItemClicked 只看「光标在本 item 矩形内」，
    // 页脚按钮压在卡片上，不扣掉的话点「移除」会顺手把项目也打开
    // （等价 webui 里 pfoot 上的 e.stopPropagation）。
    open = hit.clicked && !openPressed && !revealPressed && !morePressed && !themePressed;

    if (revealPressed) {
        const std::string error = util::ShellReveal(card.entry.rootDir);
        hub.status = error.empty() ? ("已在资源管理器中显示「" + card.entry.name + "」") : error;
        hub.statusError = !error.empty();
    }
    if (morePressed) {
        hub.confirm = true;
        hub.confirmTitle = "从列表移除「" + card.entry.name + "」？";
        hub.confirmBody = "仅从最近项目列表移除，不会删除项目文件；之后可通过「打开…」重新加入。";
        hub.confirmOk = "移除";
        hub.pendingId = card.entry.id;
    }
    if (themePressed) {
        CycleHubTheme(hub);
    }
    return open || openPressed;
}

// 新建项目向导：模板 / 命名 / 创意 / 确认。数据源 = AllTemplates / PreviewTree / Create。
void DrawHubWizard(HubState& hub, Rect area, ImDrawList* draw) {
    static const std::vector<std::string> stepNames{"模板", "命名", "创意", "确认"};
    const ModalBox box = DrawModal(draw, area, 600.0f, 420.0f);
    DrawModalTitle(draw, box.header, "sparkles", "新建项目");
    const float stepsW = StepsWidth(stepNames);
    Steps(draw, RectAt(box.header.max.x - 20.0f - stepsW, box.header.center().y - 10.0f, stepsW, 20.0f),
          stepNames, hub.step);

    const Rect body{box.body.min.x + 20.0f, box.body.min.y + 16.0f, box.body.max.x - 20.0f,
                    box.body.max.y - 8.0f};
    if (hub.step == 0) {
        float y = body.min.y;
        for (const project::ProjectTemplate& tpl : project::AllTemplates()) {
            const Rect row{body.min.x, y, body.max.x, y + 62.0f};
            const Hit hit = HitTest(row, "hub-tpl-" + tpl.id);
            const bool on = hub.tpl == tpl.id;
            DrawShadowed(draw, row.min, row.max, 10.0f, on ? ColorFillMuted() : ColorPanel(),
                         on ? ColorAccent() : (hit.hovered ? ColorLineStrong() : ColorLineSubtle()),
                         1.0f);
            DrawIconCentered(draw, TemplateIcon(tpl.id), ImVec2(row.min.x + 28.0f, row.center().y),
                             20.0f, on ? ColorAccent() : ColorTextMuted());
            draw->AddText(FontBoldAt(13.0f), 13.0f, ImVec2(row.min.x + 60.0f, row.min.y + 14.0f),
                          ColorText(), tpl.name.data(), tpl.name.data() + tpl.name.size());
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(row.min.x + 60.0f, row.min.y + 34.0f),
                            row.width() - 100.0f, ColorTextMuted(), tpl.description);
            if (on) {
                DrawIconCentered(draw, "check", ImVec2(row.max.x - 20.0f, row.center().y), 16.0f,
                                 ColorAccent());
            }
            if (hit.clicked) {
                hub.tpl = tpl.id;
            }
            y += 70.0f;
        }
    } else if (hub.step == 1) {
        const Rect nameField =
            Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 48.0f}, "项目名称", {});
        Input(draw, nameField, hub.name, "例如：灯语回声", "hub-wiz-name");
        const Rect dirField = Field(
            draw, Rect{body.min.x, nameField.max.y + 18.0f, body.max.x, nameField.max.y + 66.0f},
            "位置", "项目目录将创建在该路径下");
        constexpr const char* browseLabel = "浏览…";
        const float browseW =
            ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, browseLabel));
        Input(draw, Rect{dirField.min.x, dirField.min.y, dirField.max.x - browseW - 8.0f,
                         dirField.max.y},
              hub.dir, "E:\\projects", "hub-wiz-dir");
        ButtonSpec browseSpec;
        browseSpec.variant = ButtonVariant::Secondary;
        browseSpec.icon = "folder";
        if (Button(draw, RectAt(dirField.max.x - browseW, dirField.min.y, browseW, 30.0f),
                   browseLabel, browseSpec, "hub-wiz-browse")) {
            const std::string picked = PickFolder();
            if (!picked.empty()) {
                hub.dir = picked;
            }
        }
        // 将创建目录：目标路径 + PreviewTree 给出的真实骨架清单
        const Rect preview{body.min.x, dirField.max.y + 16.0f, body.max.x, body.max.y};
        DrawRoundRect(draw, preview.min, preview.max, 10.0f, ColorFillMuted(), ColorLineSubtle(),
                      1.0f);
        const char* previewLabel = "将创建目录";
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 10.0f),
                      ColorTextMuted(), previewLabel, previewLabel + std::strlen(previewLabel));
        const std::filesystem::path target = HubTargetDir(hub);
        const std::string targetText = target.empty() ? "<项目名>" : util::PathToUtf8(target);
        DrawTextClipped(draw, MonoAt(12.0f), 12.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 28.0f),
                        preview.width() - 28.0f, ColorText(), targetText);
        const std::vector<std::string> tree = project::PreviewTree(hub.tpl);
        float ty = preview.min.y + 50.0f;
        for (std::size_t i = 0; i < tree.size() && i < 6; ++i) {
            DrawTextClipped(draw, MonoAt(11.0f), 11.0f,
                            ImVec2(preview.min.x + 14.0f, ty), preview.width() - 28.0f,
                            ColorTextMuted(), tree[i]);
            ty += 15.0f;
        }
        if (tree.size() > 6) {
            const std::string more = "… 共 " + std::to_string(tree.size()) + " 项";
            draw->AddText(MonoAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, ty), ColorTextMuted(),
                          more.data(), more.data() + more.size());
        }
    } else if (hub.step == 2) {
        const std::string label = "一句话创意（" + std::to_string(CharCount(hub.idea)) + "/200）";
        const Rect ideaField = Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.max.y},
                                     label, "将写入 project.json，供 LLM 初始化参考；留空可跳过");
        TextArea(draw,
                 Rect{ideaField.min.x, ideaField.min.y, ideaField.max.x, ideaField.min.y + 120.0f},
                 hub.idea, 5, "hub-wiz-idea");
    } else {
        const Rect card{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f};
        DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
        Art(draw, Rect{card.min.x + 16.0f, card.min.y + 16.0f, card.min.x + 112.0f, card.min.y + 80.0f},
            7, false);
        const std::string title = util::Trim(hub.name).empty() ? "未命名项目"
                                                                    : std::string(util::Trim(hub.name));
        draw->AddText(FontBoldAt(17.0f), 17.0f, ImVec2(card.min.x + 128.0f, card.min.y + 16.0f),
                      ColorText(), title.data(), title.data() + title.size());
        const std::filesystem::path target = HubTargetDir(hub);
        const std::string pathText = target.empty() ? std::string{} : util::PathToUtf8(target);
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(card.min.x + 128.0f, card.min.y + 40.0f),
                        card.width() - 144.0f, ColorTextMuted(), pathText);
        const project::ProjectTemplate* tpl = project::FindTemplate(hub.tpl);
        const std::string tplName = tpl != nullptr ? tpl->name : std::string{};
        float tagX = card.min.x + 128.0f;
        if (!tplName.empty()) {
            const float tagW = TagWidth(tplName, true, false);
            Tag(draw, RectAt(tagX, card.min.y + 62.0f, tagW, TagHeight(true)), tplName,
                theme::Tone::Accent, true);
            tagX += tagW + 6.0f;
        }
        const char* ideaTag = util::Trim(hub.idea).empty() ? "无创意" : "含一句话创意";
        const float ideaW = TagWidth(ideaTag, true, false);
        Tag(draw, RectAt(tagX, card.min.y + 62.0f, ideaW, TagHeight(true)), ideaTag,
            theme::Tone::Idle, true);
        const char* note = "创建后将打开工作坊：总控 / 小说 / 资产 / 分镜 / 出图 / 出片 六个工作区可用。";
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(body.min.x, card.max.y + 14.0f), body.width(), ColorTextMuted(), note);
    }

    // 页脚左侧：本次操作的实际结果（错误时用 danger 色），没有就摆事实提示
    const char* hint = hub.status.empty() ? "项目骨架只写入本机目录" : nullptr;
    if (hub.status.empty()) {
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f),
                      ColorTextMuted(), hint, hint + std::strlen(hint));
    } else {
        DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                        ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f), 300.0f,
                        hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status, true);
    }

    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec secondarySpec;
    secondarySpec.variant = ButtonVariant::Secondary;
    ButtonSpec primarySpec;
    primarySpec.variant = ButtonVariant::Primary;
    if (hub.step == 3) {
        primarySpec.icon = "sparkles";
    }
    std::vector<ModalAction> actions;
    actions.push_back({"取消", ghostSpec, "hub-wiz-cancel"});
    if (hub.step > 0) {
        actions.push_back({"上一步", secondarySpec, "hub-wiz-prev"});
    }
    actions.push_back({hub.step < 3 ? "下一步" : "创建", primarySpec,
                       hub.step < 3 ? "hub-wiz-next" : "hub-wiz-create"});
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);

    if (hit[0] != 0) {
        hub.wizard = false;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hub.step > 0 && hit[1] != 0) {
        hub.step -= 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hit.back() == 0) {
        return;
    }
    if (hub.step < 3) {
        // canNext：只有第 1 步（命名）会挡人，其余步都能过（设计稿 ProjectHub.jsx:22）
        if (hub.step == 1 && util::Trim(hub.name).empty()) {
            hub.status = "项目名称不能为空（ValidateSpec 也会拦，这里先提示一次）";
            hub.statusError = true;
            return;
        }
        if (hub.step == 2 && CharCount(hub.idea) > 200) {
            hub.status = "一句话创意超过 200 字，请删减后再继续";
            hub.statusError = true;
            return;
        }
        hub.step += 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }

    // 第 4 步：真建。ValidateSpec / Materialize 的中文错误直接摆给用户。
    project::ProjectSpec spec;
    spec.name = std::string(util::Trim(hub.name));
    spec.rootDir = HubTargetDir(hub);
    spec.templateId = hub.tpl;
    spec.premise = std::string(util::Trim(hub.idea));
    const std::expected<project::ProjectRef, project::Error> created = hub.service.Create(spec);
    if (!created) {
        hub.status = "创建失败：" + created.error().message;
        hub.statusError = true;
        return;
    }
    hub.wizard = false;
    hub.name.clear();
    hub.idea.clear();
    hub.step = 0;
    hub.status = "已创建并打开项目「" + created->name + "」· " + util::PathToUtf8(created->rootDir);
    hub.statusError = false;
    RefreshHub(hub);
}

// 「打开…」对话框：列最近项目，点了就 Open。
void DrawHubOpenDialog(HubState& hub, Rect area, ImDrawList* draw) {
    const ModalBox box = DrawModal(draw, area, 520.0f, 440.0f);
    DrawModalTitle(draw, box.header, "folder", "打开项目");
    // 右上角标出列表的真实来源（索引文件所在目录）
    const std::string indexDir = util::PathToUtf8(project::DefaultIndexFile().parent_path());
    const float indexW = LabelWidth(FontAt(11.5f), 11.5f, indexDir.c_str());
    DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                    ImVec2(box.header.max.x - 20.0f - indexW, box.header.center().y - 5.75f),
                    indexW + 1.0f, ColorTextMuted(), indexDir);

    if (hub.cards.empty()) {
        Empty(draw, Rect{box.body.min.x, box.body.min.y, box.body.max.x, box.body.max.y}, "folder",
              "还没有可打开的项目", "先新建一个项目，或把已有项目目录登记进来");
    } else {
        float y = box.body.min.y + 6.0f;
        for (const HubCard& card : hub.cards) {
            const Rect row{box.body.min.x + 12.0f, y, box.body.max.x - 12.0f, y + 62.0f};
            const Hit hit = HitTest(row, "hub-open-" + card.entry.id);
            DrawShadowed(draw, row.min, row.max, 10.0f, ColorPanel(),
                         hit.hovered ? ColorLineStrong() : ColorLineSubtle(), 1.0f);
            Art(draw, Rect{row.min.x + 10.0f, row.min.y + 10.0f, row.min.x + 74.0f, row.min.y + 52.0f},
                card.artSeed, false);
            const float nameX = row.min.x + 86.0f;
            float nameW = LabelWidth(FontBoldAt(13.0f), 13.0f, card.entry.name.c_str());
            draw->AddText(FontBoldAt(13.0f), 13.0f, ImVec2(nameX, row.min.y + 12.0f), ColorText(),
                          card.entry.name.data(), card.entry.name.data() + card.entry.name.size());
            if (!card.tplLabel.empty()) {
                const theme::Tone tone = card.tplLabel == "小说"  ? theme::Tone::Accent
                                          : card.tplLabel == "影视" ? theme::Tone::Info
                                                                    : theme::Tone::Idle;
                const float tagW = TagWidth(card.tplLabel, true, false);
                Tag(draw, RectAt(nameX + nameW + 8.0f, row.min.y + 11.0f, tagW, TagHeight(true)),
                    card.tplLabel, tone, true);
            }
            const std::string meta = card.tplName.empty() ? ("最近 " + card.when)
                                                          : (card.tplName + " · " + card.when);
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(nameX, row.min.y + 34.0f),
                            row.width() - 160.0f, ColorTextMuted(), meta);
            ButtonSpec openSpec;
            openSpec.variant = ButtonVariant::Primary;
            openSpec.size = ButtonSize::Small;
            const float openW =
                ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
            const Rect openRect{row.max.x - 14.0f - openW, row.min.y + 19.0f, row.max.x - 14.0f,
                                row.min.y + 19.0f + ButtonHeight(ButtonSize::Small)};
            if (Button(draw, openRect, "打开", openSpec, "hub-open-btn-" + card.entry.id) ||
                hit.clicked) {
                // 先按值取出来：OpenHubCard 会刷新列表，hub.cards 随即重建
                const HubCard target = card;
                hub.openDlg = false;
                OpenHubCard(hub, target);
                return;
            }
            y += 70.0f;
        }
    }

    const char* hint = "单击卡片直接打开";
    draw->AddText(FontAt(11.0f), 11.0f, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f),
                  ColorTextMuted(), hint, hint + std::strlen(hint));
    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec folderSpec;
    folderSpec.variant = ButtonVariant::Ghost;
    folderSpec.icon = "folder";
    folderSpec.disabled = hub.cards.empty();
    const std::vector<ModalAction> actions{
        {"打开所在文件夹", folderSpec, "hub-open-reveal"},
        {"关闭", ghostSpec, "hub-open-close"}};
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
    if (hit[0] != 0) {
        const std::string error = util::ShellReveal(hub.cards.front().entry.rootDir);
        hub.status = error.empty() ? "已在资源管理器中显示项目目录" : error;
        hub.statusError = !error.empty();
    }
    if (hit[1] != 0) {
        hub.openDlg = false;
    }
}

} // namespace

// 全屏，不套外壳。居中列 max-w 1080，底两层径向渐变 + 240px 高 ArtInk 带。
void DrawProjectHub(Rect area, ImDrawList* draw) {
    HubState& hub = Hub();
    if (!hub.loaded) {
        if (util::Trim(hub.dir).empty()) {
            // 位置默认给当前工作目录：ValidateSpec 要求父目录真实存在，
            // 设计稿里写死的 E:\projects 在别的机器上不一定有。
            std::error_code ec;
            hub.dir = util::PathToUtf8(std::filesystem::current_path(ec));
            if (ec) {
                hub.dir = ".";
            }
        }
        RefreshHub(hub);
    }

    DrawVGradient(draw, area.min, area.max, 0.0f, ColorOf(theme::Current().bgVoid),
                  ColorOf(theme::CurrentDerived().accentDim));
    ArtInk(draw, Rect{area.min.x, area.max.y - 240.0f, area.max.x, area.max.y}, 3, 0.3f);

    const float columnW = std::min(1080.0f, area.width() - 64.0f);
    const Rect column{(area.width() - columnW) * 0.5f, 40.0f, (area.width() + columnW) * 0.5f,
                      area.max.y - 40.0f};

    const float float_ = ReduceMotion() ? 0.0f : (Pulse(5.0f) - 0.5f) * 8.0f;
    const Rect logo{column.min.x, column.min.y + float_, column.min.x + 52.0f,
                    column.min.y + 52.0f + float_};
    DrawRoundRect(draw, ImVec2(logo.min.x, logo.min.y - 4.0f), ImVec2(logo.max.x, logo.max.y + 4.0f),
                  18.0f, 0, ColorAccentGlow(), 8.0f);
    DrawDiagGradient(draw, logo.min, logo.max, 14.0f, ColorAccent(), ColorAccentHover());
    DrawIconCentered(draw, "play", logo.center(), 26.0f, ColorAccentFg());
    const char* brand = "ShineTV Studio";
    draw->AddText(FontBoldAt(26.0f), 26.0f, ImVec2(logo.max.x + 16.0f, column.min.y + 8.0f),
                  ColorText(), brand, brand + std::strlen(brand));
    const char* subtitle = "把小说变成画面";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(logo.max.x + 16.0f, column.min.y + 40.0f),
                  ColorTextMuted(), subtitle, subtitle + std::strlen(subtitle));

    // ---- 工具条：搜索 / 排序 / 打开… / 新建项目 ----
    const float barY = column.min.y + 76.0f;
    const Rect search{column.min.x, barY, column.min.x + 280.0f, barY + 30.0f};
    // 设计稿的搜索框是「图标 + 输入」合一（.search）。kit::Input 的左内边距只有
    // 10px、塞不下 14px 图标，所以输入框右移 24px，再在没 hover/没聚焦时把它
    // 自己的左边框擦掉 —— 两个控件合起来仍然是一个 280px 的圆角搜索框。
    const Rect searchInput{search.min.x + 24.0f, search.min.y, search.max.x, search.max.y};
    Input(draw, searchInput, hub.query, "搜索项目…", "hub-search");
    if (!ImGui::IsItemFocused() && !ImGui::IsItemHovered()) {
        draw->AddRectFilled(searchInput.min, ImVec2(searchInput.min.x + 1.0f, searchInput.max.y),
                            ColorFillMuted());
    }
    DrawIcon(draw, "search", ImVec2(search.min.x + 8.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());

    const std::vector<SegmentOption> sortOptions{{"r", "最近打开"}, {"n", "名称"}};
    const std::string_view picked = Segmented(
        draw, RectAt(search.max.x + 10.0f, barY, SegmentedWidth(sortOptions), 30.0f), sortOptions,
        hub.sort, "hub-sort");
    if (picked != hub.sort) {
        hub.sort = std::string(picked);
        RefreshHub(hub); // 排序换了要真重排（RefreshHub 按 hub.sort 排）
    }

    ButtonSpec openProjectSpec;
    openProjectSpec.variant = ButtonVariant::Secondary;
    openProjectSpec.icon = "folder";
    const char* openLabel = "打开…";
    const float openW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, openLabel));
    const bool openClicked =
        Button(draw, RectAt(column.max.x - openW, barY, openW, 30.0f), openLabel, openProjectSpec,
               "hub-open");
    ButtonSpec newProject;
    newProject.variant = ButtonVariant::Primary;
    newProject.icon = "plus";
    const char* newLabel = "新建项目";
    const float newW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, newLabel));
    const bool newClicked =
        Button(draw, RectAt(column.max.x - openW - 10.0f - newW, barY, newW, 30.0f), newLabel,
               newProject, "hub-new");
    if (openClicked) {
        hub.openDlg = true;
    }
    if (newClicked) {
        hub.wizard = true;
        hub.step = 0;
        hub.status.clear();
        hub.statusError = false;
    }
    // 搜索过滤放在工具条之后：本帧输入框里刚敲进来的字立刻生效
    RefilterHub(hub);

    // ---- 计数行：搜索结果条数 + 上一次操作的结果 ----
    const Rect count{column.min.x, barY + 30.0f + 22.0f, column.max.x, barY + 30.0f + 22.0f + 16.0f};
    const std::string countText = "最近项目 · 共 " + std::to_string(hub.shown.size()) + " 个项目";
    draw->AddText(FontBoldAt(12.0f), 12.0f, ImVec2(count.min.x, count.min.y + 1.0f),
                  ColorTextMuted(), countText.data(), countText.data() + countText.size());
    if (!hub.status.empty()) {
        const float maxW = column.width() * 0.5f;
        const float statusW = std::min(LabelWidth(FontAt(12.0f), 12.0f, hub.status.c_str()), maxW);
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(count.max.x - statusW, count.min.y + 1.0f),
                        statusW, hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status);
    }

    // ---- 卡片栅格 / 空态 ----
    const Rect grid{column.min.x, count.max.y + 10.0f, column.max.x, column.max.y - 8.0f};
    if (hub.shown.empty()) {
        const Rect emptyCard{grid.min.x, grid.min.y, grid.max.x, grid.min.y + 260.0f};
        DrawShadowed(draw, emptyCard.min, emptyCard.max, 10.0f, ColorPanel(), ColorLineSubtle(),
                     1.0f);
        const bool filtering = !util::Trim(hub.query).empty();
        const char* emptyTitle = filtering ? "没有匹配的项目" : "还没有项目";
        const char* emptyBody =
            filtering ? "换个关键词试试，或新建一个项目开始你的第一部作品"
                      : "点「新建项目」开始你的第一部作品";
        Empty(draw, Rect{emptyCard.min.x, emptyCard.min.y + 60.0f, emptyCard.max.x,
                         emptyCard.max.y - 60.0f},
              "folder", emptyTitle, emptyBody);
    } else {
        constexpr float cardH = 250.0f; // 120 封面 + pbody(12+19+21+21+20+11+24+13)
        const int columns = std::max(1, AutoFillCols(grid.width(), 240.0f, 14.0f));
        const float cardW = (grid.width() - 14.0f * static_cast<float>(columns - 1)) /
                            static_cast<float>(columns);
        // 打开会重排列表（lastOpened 变了），所以只记下标、循环外再执行 ——
        // 循环里刷新会让 hub.cards 重建，后面几张卡读到的是错位的条目。
        int openIndex = -1;
        for (std::size_t i = 0; i < hub.shown.size(); ++i) {
            const HubCard& card = hub.cards[static_cast<std::size_t>(hub.shown[i])];
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            const Rect bounds{grid.min.x + (cardW + 14.0f) * static_cast<float>(column),
                              grid.min.y + (cardH + 14.0f) * static_cast<float>(row), cardW, cardH};
            if (DrawHubCard(draw, hub, card, bounds, cardW - 28.0f)) {
                openIndex = static_cast<int>(i);
            }
        }
        if (openIndex >= 0) {
            OpenHubCard(hub, hub.cards[static_cast<std::size_t>(
                                 hub.shown[static_cast<std::size_t>(openIndex)])]);
        }
    }

    // ---- 自报栅格实际高度（Shell 的 `hub-scroll` 靠它才能滚）----
    //
    // ⚠️ 不报的话项目卡超过一屏就被裁掉**且滚不到**（1080 高窗口约 6 张，第 7 张
    //    起够不着）。上一轮 Shell 那侧写的是 `setContentHeight(region.content().height())`
    //    —— 上报值恰好等于视口高，等于上报了 0 行，ScrollMaxY 恒为 0。**看着加了、
    //    实际等于没加**。Shell 侧现在有一次性哨兵会把这个报出来。
    // 高度按栅格公式算（行数 = ceil(条目数/列数)），与上面画卡用的是同一套列宽算法。
    {
        float usedBottom;
        if (hub.shown.empty()) {
            usedBottom = grid.min.y + 260.0f;
        } else {
            constexpr float kHubCardH = 250.0f;
            const int cols = std::max(1, AutoFillCols(grid.width(), 240.0f, 14.0f));
            const int rowCount =
                (static_cast<int>(hub.shown.size()) + cols - 1) / cols; // 向上取整
            usedBottom = grid.min.y + (kHubCardH + 14.0f) * static_cast<float>(rowCount) - 14.0f;
        }
        // 与列首/计数行保持一致的下边界，向上多留 8px 收尾。
        pages::SetPageContentHeight(usedBottom - area.min.y + 8.0f);
    }

    // ---- 弹窗：确认 > 向导 > 打开项目 ----
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (hub.confirm) {
            hub.confirm = false;
        } else if (hub.wizard) {
            hub.wizard = false;
        } else if (hub.openDlg) {
            hub.openDlg = false;
        }
    }
    if (hub.confirm) {
        const ModalBox box = DrawModal(draw, area, 460.0f, 210.0f);
        DrawModalTitle(draw, box.header, "info", hub.confirmTitle.c_str());
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(box.body.min.x + 18.0f, box.body.min.y + 18.0f),
                        box.body.width() - 36.0f, ColorTextSecondary(), hub.confirmBody, true);
        ButtonSpec ghostSpec;
        ghostSpec.variant = ButtonVariant::Ghost;
        ButtonSpec dangerSpec;
        dangerSpec.variant = ButtonVariant::Danger;
        const std::vector<ModalAction> actions{{"取消", ghostSpec, "hub-confirm-cancel"},
                                               {hub.confirmOk, dangerSpec, "hub-confirm-ok"}};
        const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
        if (hit[0] != 0) {
            hub.confirm = false;
        }
        if (hit[1] != 0) {
            RemoveHubCard(hub, hub.pendingId);
            hub.confirm = false;
        }
    }
    if (hub.wizard) {
        DrawHubWizard(hub, area, draw);
    }
    if (hub.openDlg) {
        DrawHubOpenDialog(hub, area, draw);
    }
}

} // namespace shine::pages
