#pragma once
// shine::pages —— novel.db 快照数据层（小说 / 资产 / 分镜三页共用同一份）
//
// 原先是 WorkspaceB.cpp 里一个 700 行的匿名命名空间：结构体、开库查询、视图重建、
// 绑定/选中、状态词表全挤在**一个** .cpp 里，对外只暴露 WorkspacePages.h 里几个
// 转发函数。改按**线程模型**拆成两半：
//
//   * BookQuery.cpp —— **worker 线程**侧：开库 + 查询，产出纯数据快照（无句柄）。
//   * BookData.cpp  —— **UI 线程**侧：快照持有、只读视图重建、绑定与选中、值 → 显示。
//
// 这条缝不是按行数切的，是按线程边界切的：worker 不碰 UI 状态，UI 线程不碰
// sqlite 句柄与文件 IO。分错就会出现「UI 线程里开着库」这种明令禁止的形状。
//
// ⚠️ 这里是**内部**表示（BookState）与**对外**只读视图（BookSideView）两套，
//    不是重复：前者带库主键与 worker 需要的中间量，后者是给外壳侧栏 / 检查器用的
//    纯数据拷贝。RebuildBookSide（BookData.cpp）是两者之间唯一的转换点。
#include "ui/imgui/pages/WorkspacePages.h"

#include "novel/NovelContinuity.h"
#include "novel/NovelTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::pages {

// BookState 的主键字段都是 novelcore::RowId；导出别名，让 BookQuery / BookData /
// 各页都写裸 `RowId`，不必每处带前缀。
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

// 全树唯一那份 novel.db 快照。UI 线程读写（worker 经 async::PostToUi 回投后由
// ApplyBook 并进来），不对页面之外暴露。
BookState& Book();

// 重新读一轮 novel.db。**立即**把 loading 翻真并重建外壳视图，然后派发 worker；
// 结果回投时按 generation 丢弃过期的那一份。
void RequestBookReload();

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
//    （换箭头、去掉 #、换措辞）都该表现为「解析不出来 → 原样显示 detail」，
//    而不是崩、也不是编一个镜对出来。id 在本章镜头表里查不到也一样：那是数据
//    对不上，如实回退比画一个假的 S00x 诚实。
std::string ShotPairToCodes(const std::string& detail, const std::vector<BookShot>& shots);

// 三页共用的**空态**：没绑工程 / 在读 / 库打不开 / 库里没数据，分四种说法。
// 不在这里编任何数字 —— 分清「还没加载」「加载失败」「确实没有」三件事。
void BookEmpty(kit::Rect body, ImDrawList* draw, const char* icon, const char* hint);

// ---- 值 → 显示：小说 / 资产两页共用的状态词表 ----
//
// ⚠️ 三处状态词表（章状态 / 资产状态 / 实体分类）都只有**一份**，各自在本文件里
//    定义成导出函数。它们原先是匿名命名空间里的，资产页之外没人用得到；
//    拆文件之后调用点跨了 .cpp，继续匿名就断了链。

// chapters.updated（epoch 秒）→「MM-DD HH:MM」。<=0 / 转换失败 → kDash。
std::string TimeText(std::int64_t epochSeconds);

// 章状态（chapters.status 规范四值：draft|writing|review|done）→ 标签 + 色调。
void ChapterTone(const std::string& status, std::string& label, theme::Tone& tone);

// VisualAssetRow::status 的八值生产态（`11` §2.6）→ 标签 + 色调。
void AssetTone(const std::string& status, std::string& label, theme::Tone& tone);

// ---- worker 侧查询，定义在 BookQuery.cpp ----
void LoadBook(const std::filesystem::path& root, int wantChapter, BookState& out);

} // namespace shine::pages
