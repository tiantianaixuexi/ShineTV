#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/PageCommon.h"

#include "core/Async.h"
#include "util/Encoding.h"

#include <algorithm>
#include <utility>

namespace shine::pages {

using namespace shine::kit;

namespace {

// 只在 UI 线程读写的重载代号：worker 回投时用它丢弃过期结果。
std::uint64_t g_bookGen = 0;

// P4 外壳（Shell 侧栏树 / 检查器）读的那份**只读视图**。BookState 的持有者是
// Book()（在匿名命名空间之外，因为三页都要读它）—— 但外壳仍然只拿只读视图：
// 全量快照落地时重建，切章时只更新当前章相关字段。外壳不做 IO。
// 函数内 static 持有，getter 返回引用，所以外壳每帧读它没有拷贝成本。
BookSideView& BookSideCache() {
    static BookSideView view;
    return view;
}

// 资产工作区的 kind 筛选存处。⚠️ 必须是**函数内 static**，不能是 BookSideView 的
// 字段：视图每次 RebuildBookSide 都被整块重建（`v = BookSideView{}`），
// 存在里面必被冲掉 —— 症状是筛完立刻弹回「全部」。
// 值是 entities.kind 原文。nullopt 表示全部，空字符串仍可匹配旧库里的空 kind。
std::optional<std::string>& BookKindFilterSlot() {
    static std::optional<std::string> kind;
    return kind;
}

void SyncBookSideState(const BookState& s) {
    BookSideView& v = BookSideCache();
    v.bound = s.bound;
    v.loading = s.loading;
    v.error = s.error;
    v.selectedChapter = s.selectedChapter;
    v.selectedShot = s.selectedShot;
    v.selectedAsset = s.selectedAsset;
}

void RefreshBookSideShots(const BookState& s) {
    BookSideView& v = BookSideCache();
    // ⚠️ 只有**当前选中章**的镜（shots 与 BookState.shots 同口径），不是全书。
    v.shots.clear();
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
        row.imageRelPath = shot.imageRelPath;  // 这一镜的成片图；空 = 还没出图
        v.shots.push_back(std::move(row));
    }
}

void RefreshBookSideRelation(const BookState& s) {
    BookSideView& v = BookSideCache();
    // 检查器「关联」段锚在当前选中镜上。没有镜或没有关联场时，相应字段留空。
    v.relation = BookRelationView{};
    if (const BookShot* selected = s.shot(); selected != nullptr) {
        v.relation.shotCode = ShotCode(selected->ord);
        for (const BookSceneLink& link : s.sceneLinks) {
            if (link.id == selected->sceneId) {
                v.relation.sceneOrd = link.ord;
                v.relation.sceneTitle = link.title;
                v.relation.foreshadows = link.foreshadows;
                break;
            }
        }
    }
}

// 全量快照落地或换工程时，重建章列表、资产列表和当前章视图。
void RebuildBookSide(const BookState& s) {
    BookSideView& v = BookSideCache();
    v = BookSideView{};  // 换工程时清掉旧数据
    SyncBookSideState(s);
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
    v.chapterVStages = s.chapterVStages;
    RefreshBookSideShots(s);
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
        // 逐层产物（派生链要按层画图）—— 与上面三个计数同源，不是另一份查询。
        row.artifacts = asset.artifacts;
        // 色调与中文标签都复用资产页那份 AssetTone（唯一的状态→颜色映射）。
        // label 也存进视图：AssetTone() 在本文件的匿名命名空间里，Shell 的检查器
        // 够不着，让它自己再映射一遍就是第二份状态词表。
        AssetTone(asset.hasAsset ? asset.assetStatus : std::string(), row.statusLabel, row.tone);
        v.assets.push_back(std::move(row));
    }
    // 最近的出图产物（总控「最近产物」卡的缩略图）。纯数据拷贝 —— 文件大小已经在
    // worker 侧 stat 过（那里本来就做 IO），UI 线程不再碰盘。
    v.recentImages = s.recentImages;
    RefreshBookSideRelation(s);
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

} // namespace

BookState& Book() {
    static BookState state;
    return state;
}

void RequestBookReload() {
    BookState& s = Book();
    s.loading = true;
    SyncBookSideState(s);  // 派发前让外壳看到 loading，已有列表继续使用。
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

namespace {

// 切片回投：只把「当前章切片」并进现有状态，章列表（含全书正文）/ 资产 /
// recentImages / chapterVStages **一个都不动** —— 这就是切章不再「整表刷新」的
// 关键：列表数据本来就没变，换掉它才是那次可见的刷新。
void ApplyBookSlice(BookState& s, BookState&& next) {
    const int keepShot = s.selectedShot;
    s.sceneLinks = std::move(next.sceneLinks);
    s.shots = std::move(next.shots);
    for (int i = 0; i < 8; ++i) {
        s.vStage[i] = next.vStage[i];
    }
    s.continuity = std::move(next.continuity);
    s.selectedChapter = next.selectedChapter;
    s.error = std::move(next.error);
    s.selectedShot = keepShot >= 0 && keepShot < static_cast<int>(s.shots.size()) ? keepShot : 0;
    s.loading = false;
    SyncBookSideState(s);
    RefreshBookSideShots(s);
    RefreshBookSideRelation(s);
}

// 切章派发前同步选中项和 loading，回投时按 generation 丢弃过期结果。
void RequestBookChapterSlice(RowId chapterId, int chapterIndex) {
    BookState& s = Book();
    s.loading = true;
    SyncBookSideState(s);
    RefreshBookSideRelation(s);
    const std::uint64_t gen = ++g_bookGen;
    const std::filesystem::path root = s.root;
    async::RunOnWorker([root, chapterId, chapterIndex, gen] {
        BookState next;
        LoadBookChapterSlice(root, chapterId, chapterIndex, next);
        async::PostToUi([gen, next = std::move(next)]() mutable {
            BookState& cur = Book();
            if (gen != g_bookGen) {
                return;  // 已经又发起了一轮，丢掉这次过期结果
            }
            ApplyBookSlice(cur, std::move(next));
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
        ++g_bookGen;  // 解绑后丢弃仍在途中的查询。
        return;  // 未打开工程 → 空态
    }
    s.root = std::move(root);
    s.bound = true;
    RequestBookReload();
}

} // namespace

// ---- 值 → 显示：状态词表（声明见 BookData.h）----
// ⚠️ 这三份映射各自只有一份。资产侧栏的状态点、检查器的状态行、资产页的 Tag
//    要同一份；各自映射一遍就是第二份词表，日后必然分裂。
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
    // 「正在读取」只在**还没有任何数据**时显示。已有数据时加载中照常显示旧数据
    //（stale-while-revalidate）：切章 / 重读都会翻 loading，整块换空态再重建就是
    // 「每点一章列表刷新一次」的可见部分。数据没变就不该换画面。
    if (s.loading && s.chapters.empty() && s.assets.empty()) {
        Empty(draw, body, icon, "正在读取 novel.db", hint);
        return;
    }
    Empty(draw, body, icon, "尚无数据", hint);
}

// 三页绑定"当前工程根"的入口（必须在匿名命名空间之外，否则 Shell 链接不到）。
// 三个入口共用同一份快照：Shell 在 SetProjectRoot 里连同 BindOverviewProject 一起调，
// 调任意一个即可（同一个 root 只会开一次库）。
std::filesystem::path AssetImagePath(const std::filesystem::path& root,
                                     const std::string& sheetRelPathUtf8) {
    if (sheetRelPathUtf8.empty() || root.empty()) {
        return {};
    }
    const std::filesystem::path rel = util::PathFromUtf8(sheetRelPathUtf8);
    return rel.is_absolute() ? rel : (root / rel);
}

void BindNovelProject(std::filesystem::path root) { BindBook(std::move(root)); }
void BindAssetsProject(std::filesystem::path root) { BindBook(std::move(root)); }
void BindStoryboardProject(std::filesystem::path root) { BindBook(std::move(root)); }

// ---- P4 外壳（Shell 侧栏树 / 检查器）要的那份只读视图 ----
// ⚠️ 这几个也必须在匿名命名空间**之外**（与上面三个 Bind 同一批），
//    否则 Shell 在 SetProjectRoot 里链接不到它们。
// 只读视图。UI 线程读，不做任何 IO；没绑工程时返回的是一份全 0 / 空列表的默认视图。
// 只读视图。UI 线程读，不做任何 IO；没绑工程时返回的是一份全 0 / 空列表的默认视图。
const BookSideView& BookSide() { return BookSideCache(); }

std::string AssetKindLabel(const std::string& kind) {
    static constexpr std::pair<std::string_view, std::string_view> labels[] = {
        {"person", "人物"}, {"character", "角色"}, {"location", "地点"},
        {"item", "物品"}, {"prop", "道具"}, {"treasure", "宝物"},
        {"clothing", "服装"}, {"faction", "势力"}, {"event", "事件"},
    };
    for (const auto& [value, label] : labels) {
        if (kind == value) {
            return std::string(label);
        }
    }
    if (kind.empty()) {
        return kDash;
    }
    // 自定义 kind 撞上界面标签时加引号。原值以同样的引号开头时也加一层。
    if (kind == "全部" || kind == kDash || kind.starts_with("「") ||
        std::ranges::any_of(labels, [&kind](const auto& entry) { return kind == entry.second; })) {
        return "「" + kind + "」";
    }
    return kind;
}

const std::optional<std::string>& BookKindFilter() { return BookKindFilterSlot(); }
void SetBookKindFilter(std::optional<std::string> kind) { BookKindFilterSlot() = std::move(kind); }

// 侧栏资产树点叶子 → 改选中实体。index 是 **BookSideView::assets 的下标**
// （与 BookState::assets 同序），不是筛后树的序号。
// 越界只可能是过期点击或旧工程的下标，先判界、后夹范围，和 SelectBookChapter
// 同一套写法；改完同步外壳视图的选中项。
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
    SyncBookSideState(s);
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
    // ⚠️ 切章走**轻量切片**（只重取本章的场/镜/逐镜成图/V 阶段/连续性），不整库重读：
    //    全量 LoadBook 会把全书正文（500 章 ≈ 13MB）整个重取、整份快照换掉 ——
    //    那正是「每点一章列表刷新一次」的元凶（loading 翻真 + 全量回投各刷一次）。
    RequestBookChapterSlice(s.chapters[static_cast<std::size_t>(s.selectedChapter)].id,
                            s.selectedChapter);
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
    SyncBookSideState(s);
    RefreshBookSideRelation(s);
}

} // namespace shine::pages
