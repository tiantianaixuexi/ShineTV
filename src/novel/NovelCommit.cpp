#include "novel/NovelCommit.h"

#include "core/Log.h"
#include "novel/NovelChecks.h"
#include "novel/NovelGraph.h"
#include "novel/NovelNames.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Reflect.h"
#include "util/Strings.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::int64_t NowSec() noexcept { return util::NowMillis() / 1000; }

// `01` §1.1.2 的 31 种 kind（D8 用）——**唯一一份**（别在别处再列一遍）
[[nodiscard]] const std::set<std::string>& KnownKinds() {
    static const std::set<std::string> kinds = {
        "universe",  "world_rule", "history",      "culture",       "language",
        "religion",  "economy",    "tech",         "society",       "calendar",
        "person",    "creature",   "clothing",     "prop",          "treasure",
        "item",      "location",   "faction",      "power_system",  "ability",
        "event",     "plot",       "arc",          "conflict",      "theme",
        "motif",     "ending",     "secret",       "foreshadowing", "mystery",
        "resource"};
    return kinds;
}

[[nodiscard]] bool Contains(std::string_view hay, std::string_view needle) noexcept {
    return hay.find(needle) != std::string_view::npos;
}

// 章级快照落盘（不变式 I11）：`<dir>/ch<NNN>.json`
[[nodiscard]] std::expected<std::filesystem::path, std::string>
WriteChapterSnapshot(std::string_view dir, const StateDiff& diff, std::string_view diffHash,
                     const std::vector<RowId>& versionIds) {
    if (dir.empty()) {
        return std::unexpected(std::string{"未提供章级快照目录（不变式 I11 要求提交前有快照）"});
    }
    const std::filesystem::path root = util::PathFromUtf8(dir);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        return std::unexpected(fmt::format("快照目录建不出来：{}", util::PathToUtf8(root)));
    }
    const std::filesystem::path file = root / fmt::format("ch{:03}.json", diff.chapter_id);

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return std::unexpected(std::string{"快照 JSON 分配失败"});
    }
    yyjson_mut_val* rootVal = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, rootVal);
    yyjson_mut_obj_add_int(doc, rootVal, "chapter_id", diff.chapter_id);
    yyjson_mut_obj_add_int(doc, rootVal, "committed_at", NowSec());
    yyjson_mut_obj_add_strncpy(doc, rootVal, "diff_hash", diffHash.data(), diffHash.size());
    yyjson_mut_obj_add_strncpy(doc, rootVal, "input_state_hash", diff.input_state_hash.data(),
                               diff.input_state_hash.size());
    // 涉及的实体级快照 id（Before 值在那几张行里，回滚按它们走）
    yyjson_mut_val* versionArr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, rootVal, "entity_version_ids", versionArr);
    for (const RowId id : versionIds) {
        yyjson_mut_arr_add_int(doc, versionArr, id);
    }
    // 原始 diff（作为**内嵌 JSON**放进快照，回滚时按它反推）
    const std::string diffJson = StateDiffToJson(diff);
    yyjson_mut_obj_add_val(doc, rootVal, "diff", yyjson_mut_raw(doc, diffJson.c_str()));

    std::size_t len = 0;
    char* text = yyjson_mut_val_write(rootVal, YYJSON_WRITE_PRETTY, &len);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        return std::unexpected(std::string{"快照序列化失败"});
    }
    const bool written = util::WriteFileBytes(file, std::string_view{text, len});
    std::free(text);
    if (!written) {
        return std::unexpected(fmt::format("快照写盘失败：{}", util::PathToUtf8(file)));
    }
    return file;
}

// 解析 TempoRef（TempId → 真实 id）
[[nodiscard]] RowId ResolveTempo(const TempoRef& ref, const std::map<std::string, RowId>& tempIds) {
    if (!ref.temp_id.empty()) {
        const auto it = tempIds.find(ref.temp_id);
        return it == tempIds.end() ? 0 : it->second;
    }
    return ref.entity_id;
}

// ———— ★★ S87（T1）：**语义引用解析** —— 名字就是引用键 ————
// 责任边界（用户拍板）：LLM 只表达"**我指的是谁 / 我要新建谁**"；resolver 只**发现歧义**，
// **绝不替作者判定"两个同名是不是同一个人"** —— 不用"最近章节 / 最近出现 / 关系数量"之类的启发式。
//（这类启发式很危险：小说里有时间线、闪回、多视角，"最近出现"根本不等价于"同一个实体"。）
//
// 决策树（**系统规则**，逐条实现在 `ResolveRefFull`）：
//   ① 明确数字 `*_id`      ⇒ 直接用（存在性 / kind 由 K02/K03 判）
//   ② 本章 `temp_id` 命中   ⇒ 用（= 我本章新建的）
//   ③ 纯数字字符串          ⇒ 当 id 用（兼容模型写 `"entity_id":"43"`）
//   ④ 名字（归一化后）：
//        · 同 kind 命中 **1** 个 ⇒ **复用**
//        · 同 kind 命中 **多个** ⇒ **拒 + 候选列表**（要模型自己指定，系统不猜）
//        · 0 个但**别的 kind** 有 ⇒ 拒（"库里「X」是 location，此处期望 person"）
//        · **0 个**              ⇒ 拒（"库里没有「X」；要新建请进 entities[] 并给它 temp_id"）
//                                  ＋**相近名字候选**（只提示，绝不自动采用）
// ★ 口径唯一（用户要求）：实体名归一化**只有一份实现** —— `novel/NovelNames.h`。
// R10 诊断（`NovelRepair`）与实际解析（本文件）必须同源：各写一套必然漂成
// "扫描说没重复、解析说'有歧义'"（或反之），那种不一致极难查。
[[nodiscard]] std::string NormalizeRefName(std::string_view s) { return NormalizeEntityName(s); }

[[nodiscard]] std::string TrimRef(std::string_view s) { return TrimEntityRef(s); }

struct NameHit { // 库里一条"名字命中"
    RowId id = 0;
    std::string kind;
    std::string name;
    int created_chapter = 0;
};

// 库内**名字索引**（一次查询、按归一化名字分组）。
// ⚠️ 排除 `event`：事件**不用名字去重 / 不用名字引用**（它靠自己的键与来源定位）。
// ⚠️ 这**不是** UNIQUE 约束 —— 同一 `(kind, 归一化名字)` **允许多行**：真出现"两个李默"时，
//    resolver 返回**候选列表**让模型/人指定，而**不是**由数据库替你决定身份。
class EntityNameIndex {
public:
    explicit EntityNameIndex(db::sqlite::Database& db) { Load(db); }

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }

    // 取"名字归一化后相同"的候选；`kindFilter` 非空时只保留该 kind。
    [[nodiscard]] std::vector<NameHit> Find(std::string_view norm,
                                            std::string_view kindFilter = {}) const {
        const auto it = byName_.find(std::string{norm});
        if (it == byName_.end()) {
            return {};
        }
        if (kindFilter.empty()) {
            return it->second;
        }
        std::vector<NameHit> out;
        for (const NameHit& h : it->second) {
            if (h.kind == kindFilter) {
                out.push_back(h);
            }
        }
        return out;
    }

    // 相近名（**只用于报文提示**，绝不用于自动解析）：互相包含，如 `撑伞人影` vs `撑伞人影（青年）`。
    [[nodiscard]] std::vector<NameHit> Similar(std::string_view norm, std::size_t limit = 5) const {
        std::vector<NameHit> out;
        if (norm.empty()) {
            return out;
        }
        for (const auto& [key, hits] : byName_) {
            if (key.empty() || key == norm) {
                continue;
            }
            if (key.find(norm) == std::string::npos && norm.find(key) == std::string::npos) {
                continue;
            }
            for (const NameHit& h : hits) {
                out.push_back(h);
                if (out.size() >= limit) {
                    return out;
                }
            }
        }
        return out;
    }

private:
    void Load(db::sqlite::Database& db) {
        auto st = db.Prepare("SELECT id,kind,name,created_chapter FROM entities WHERE kind<>?1");
        if (!st) {
            log::Warn("EntityNameIndex：查询失败（{}）—— 本轮只认 id / temp_id", st.error().message);
            return;
        }
        (void)st->BindText(1, kind::event);
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            NameHit h;
            h.id = st->ColumnInt(0);
            h.kind = st->ColumnText(1);
            h.name = st->ColumnText(2);
            h.created_chapter = static_cast<int>(st->ColumnInt(3));
            const std::string norm = NormalizeRefName(h.name);
            if (!norm.empty()) {
                byName_[norm].push_back(std::move(h));
            }
        }
        loaded_ = true;
    }

    std::map<std::string, std::vector<NameHit>> byName_;
    bool loaded_ = false;
};

[[nodiscard]] std::string DescribeHits(const std::vector<NameHit>& hits, std::size_t limit = 6) {
    std::string out;
    const std::size_t n = hits.size() < limit ? hits.size() : limit;
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) {
            out += " / ";
        }
        out += fmt::format("#{}（第{}章 {}「{}」）", hits[i].id, hits[i].created_chapter, hits[i].kind,
                           hits[i].name);
    }
    if (hits.size() > n) {
        out += fmt::format(" ……共 {} 个", hits.size());
    }
    return out;
}

// 解析上下文：本章 temp_id 映射 + 本章新建实体名 + 库内名字索引
struct RefCtx {
    const std::map<std::string, RowId>* tempIds = nullptr; // 本章 temp_id → 真实 id
    // 归一化名 → 本章 `entities[]` 里声明该名字的 id **列表**（列表长度 >1 见下：`force_new` 可让
    // 同一章出现两个同名实体 ⇒ 那时**拒**，要模型用 temp_id 指定，而不是悄悄取最后一个）
    const std::map<std::string, std::vector<RowId>>* chapterNames = nullptr;
    const EntityNameIndex* index = nullptr; // 库内（不含 event）
    // ★ T4：**kind 判定的oracle**（只在 D12 的罕见分支里查一次：解析出来的那个 id 到底是什么 kind）。
    // 为什么需要：模型会把**错槽位**的 temp_id 一起写进来（真跑第 21 章实证：
    // `location_id=59`（有效 location）+ `location_temp_id=en:身后按扳手之手`（本章声明的 **event**））
    // —— 那种 ref 对 location 槽位**根本不构成"说法"**，不该按 D12 冲突处理（否则为一处噪声废掉一整章）。
    db::sqlite::Database* db = nullptr;
};

struct RefResolution {
    RowId id = 0;
    std::string error; // 非空 = 解析失败（可直接进 fail 报文）
};

[[nodiscard]] std::string KindLabel(std::string_view kindWanted) {
    return kindWanted.empty() ? std::string{"实体"} : fmt::format("{}", kindWanted);
}

[[nodiscard]] RefResolution ResolveRefFull(RowId id, const std::string& refRaw, const RefCtx& ctx,
                                           std::string_view kindWanted) {
    // ① 明确数字 id（模型自己指定了就用它 —— 存在性/kind 由 K02/K03 判）
    if (id > 0) {
        // ★ T4-D12（**权威判定**在落库这一侧）：同时给了 id 与 ref ⇒ 必须**指向同一实体**。
        // **冗余允许、冲突拒绝**：同实体 ⇒ 放行（canonical = id）；不同实体 / ref 解析不了 ⇒ 拒。
        const std::string otherRef = TrimRef(refRaw);
        if (!otherRef.empty()) {
            const RefResolution other = ResolveRefFull(0, otherRef, ctx, kindWanted); // 递归：id=0 那一路
            // ★ T4：先看这个 ref 解析出来的实体**是不是这个槽位要的 kind** ——
            // 不是 ⇒ 它对槽位**不构成"说法"**（错槽位噪声）⇒ **不参与 D12**，按 `*_id` 走 + 记一条 warn。
            // 真跑实证（第 21 章）：`location_id=59` + `location_temp_id=en:身后按扳手之手`（kind=event）
            // 若判成 D12 冲突，模型会因为"多写了个错槽位字段"而**整章被拒**（而 `location_id` 本身是对的）。
            if (other.id > 0 && !kindWanted.empty() && ctx.db != nullptr) {
                std::string actualKind;
                if (auto st = ctx.db->Prepare("SELECT kind FROM entities WHERE id=?1"); st) {
                    (void)st->BindInt(1, other.id);
                    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                        actualKind = st->ColumnText(0);
                    }
                }
                if (!actualKind.empty() && actualKind != kindWanted) {
                    log::Warn("引用冗余字段被忽略：字段期望 **{}**，但 ref「{}」解析到 #{}（kind={}）—— "
                              "**错槽位的引用对槽位不构成说法**，按 id={} 继续（T4-D12 只判同 kind 冲突）",
                              KindLabel(kindWanted), otherRef, other.id, actualKind, id);
                    return {id, {}};
                }
            }
            if (other.id <= 0) {
                return {0, fmt::format("D12：同时给了 id={} 与 ref=「{}」，但该 ref 解析不了：{}"
                                       "（**冗余允许、冲突拒绝**：请只留一个）",
                                       id, otherRef, other.error)};
            }
            if (other.id != id) {
                return {0, fmt::format("D12：同时给了 id={} 与 ref=「{}」⇒ 解析到 **#{}**：两个说法指向"
                                       "**不同**实体，请只留一个（系统不替你选）",
                                       id, otherRef, other.id)};
            }
        }
        return {id, {}};
    }
    const std::string ref = TrimRef(refRaw);
    if (ref.empty()) {
        return {0, fmt::format("既没填已有{}的 id，也没填名字 / temp_id（二者必有一）",
                               KindLabel(kindWanted))};
    }
    // ② 本章 temp_id
    if (ctx.tempIds != nullptr) {
        if (const auto it = ctx.tempIds->find(ref); it != ctx.tempIds->end()) {
            return {it->second, {}};
        }
    }
    // ③ 纯数字字符串 ⇒ 当 id 用（兼容 `"entity_id":"43"`）
    bool allDigits = true;
    for (const char c : ref) {
        if (c < '0' || c > '9') {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        if (const RowId asId = std::strtoll(ref.c_str(), nullptr, 10); asId > 0) {
            return {asId, {}};
        }
    }
    // ④ 名字（归一化）：★ T4-① **候选集合，不是解析优先级** ——
    // 候选 = （本章 `entities[]` 声明的）**∪**（库内同 kind 同名的），再**按 entity id 去重**，
    // 最后才判 0 / 1 / >1。**绝不能**写成"chapterNames 命中就 return"：
    // 那样当本章 `force_new` 建了同名实体时，"本章优先"会把库里那个同名实体**悄悄赢掉**
    //（真跑场景：库内 #43 李默 + 本章 #201 李默(force_new=true)，`entity_ref="李默"` 必须得到
    //  **2 个候选 ⇒ 拒**，而不是选 #201）。
    const std::string norm = NormalizeRefName(ref);
    std::vector<NameHit> cand;
    const auto pushUnique = [&cand](const NameHit& h) {
        for (const NameHit& c : cand) {
            if (c.id == h.id) {
                return; // **按 entity id 去重**（同一 id 从两个来源来 ⇒ 仍算**一个**候选）
            }
        }
        cand.push_back(h);
    };
    const bool indexUsable = ctx.index != nullptr && ctx.index->loaded();
    if (ctx.chapterNames != nullptr) {
        if (const auto it = ctx.chapterNames->find(norm); it != ctx.chapterNames->end()) {
            for (const RowId one : it->second) {
                // 本章声明的候选：created_chapter 用 -1 标记（报文里显示"本章新建"）
                pushUnique(NameHit{one, std::string{KindLabel(kindWanted)}, ref, -1});
            }
        }
    }
    if (indexUsable) {
        for (const NameHit& h : ctx.index->Find(norm, kindWanted)) {
            pushUnique(h);
        }
    }
    if (cand.size() == 1) {
        return {cand.front().id, {}}; // **唯一命中** ⇒ 用（≡ 复用）
    }
    if (cand.size() > 1) {
        // **系统规则**：不猜。给**全部**候选（标明来源），要模型/作者自己指定。
        return {0, fmt::format("「{}」**有歧义**：同名同 kind 的有 {} 个 ⇒ {}。请直接填要用的实体 "
                               "**id（数字）**（或用本章 temp_id 指定本章新建的那个）；若你要的其实是"
                               "**新建**，请放进 entities[] 并给它 temp_id。"
                               "（系统**不在**\"本章新建的\"与\"库里已有的\"之间替你选，也不按"
                               "\"最近出现\"猜）",
                               ref, cand.size(), DescribeHits(cand))};
    }
    if (!indexUsable && ctx.chapterNames == nullptr) {
        return {0, fmt::format("名字索引不可用，「{}」无法解析（请直接填实体 id）", ref)};
    }
    if (!indexUsable) {
        // 索引不可用：本章也没声明过这个名字 ⇒ 无法判"库里有没有"
        return {0, fmt::format("名字索引不可用，「{}」无法解析（请直接填实体 id）", ref)};
    }
    const std::vector<NameHit> otherKind = ctx.index->Find(norm, {});
    if (!otherKind.empty()) {
        return {0, fmt::format("「{}」在库里是 {}，此处期望 **{}** —— 名字认对了但 kind 不符，"
                               "请改用正确的名字或实体 id",
                               ref, DescribeHits(otherKind), KindLabel(kindWanted))};
    }
    const std::vector<NameHit> similar = ctx.index->Similar(norm);
    return {0, fmt::format("库里没有名为「{}」的{}（已按空白 / 全角 / 大小写归一化）{}。"
                           "要引用**已有**实体请照库里的名字写；**要新建**请放进 entities[] 并给它 "
                           "temp_id；若只是名字变体，请用库里那个名字",
                           ref, KindLabel(kindWanted),
                           similar.empty() ? std::string{}
                                           : fmt::format("；相近名字：{}", DescribeHits(similar)))};
}

// 便捷版（失败返回 0）
[[nodiscard]] RowId ResolveRef(RowId id, const std::string& ref, const RefCtx& ctx,
                               std::string_view kindWanted) {
    return ResolveRefFull(id, ref, ctx, kindWanted).id;
}

// 失败报文（把"为什么解析不到"讲清楚；歧义时**带候选列表**）
[[nodiscard]] std::string RefFail(std::string_view where, RowId id, const std::string& ref,
                                  const RefCtx& ctx, std::string_view kindWanted) {
    const RefResolution r = ResolveRefFull(id, ref, ctx, kindWanted);
    return fmt::format("{}引用解析失败（id={}，name/temp_id=「{}」）：{}", where, id, ref,
                       r.error.empty() ? "解析不到真实 id" : r.error);
}

// 本章声明过的 TempId 集合（`entities[]` + `events[]`）—— 门禁用它判"temp 引用是否合法"。
[[nodiscard]] std::set<std::string> DeclaredTempIds(const StateDiff& diff) {
    std::set<std::string> out;
    for (const NewEntityDelta& e : diff.entities) {
        if (!e.temp_id.empty()) {
            out.insert(e.temp_id);
        }
    }
    for (const EventDelta& e : diff.events) {
        if (!e.temp_id.empty()) {
            out.insert(e.temp_id);
        }
    }
    return out;
}

// ★ S77/T4：**"前缀 + 纯数字"的 TempId / 引用值**（`en:1` / `en:07` / `ev:3`）—— 见 D9 的注释。
// ⚠️ **定义放在这里**（`CheckRef` 之前）：树里 `RefGate`/`CheckRef` 都在**匿名命名空间**内，
// 若只在命名空间作用域定义、又在这里前置声明，调用点会看到**两个候选** ⇒ 重载歧义（实测踩到）。
// T4 把它的用途从"校验 `entities[].temp_id`"**扩到所有引用值**（`*_ref` / `*_temp_id`）。
[[nodiscard]] bool LooksLikeNumericTempId(std::string_view tid) {
    const auto colon = tid.rfind(':');
    if (colon == std::string_view::npos || colon + 1 >= tid.size()) {
        return false; // 无前缀 / 空后缀 ⇒ 不是这个形态
    }
    for (std::size_t i = colon + 1; i < tid.size(); ++i) {
        if (tid[i] < '0' || tid[i] > '9') {
            return false;
        }
    }
    return true;
}

// 门禁的引用上下文（★ S87/T1：门禁也**认名字**，与落库时同一套决策树）。
struct RefGate {
    const std::set<std::string>* tempIds = nullptr; // 本章声明的 temp_id
    const std::set<std::string>* names = nullptr;   // 本章 entities[] 声明的名字（已归一化）
    // ★ T4-①：本章用 **`force_new`** 声明的名字（已归一化）。
    // 语义（用户钉死）：`force_new` **允许**造同 kind 同名实体；但一旦这么做了，**该名字不再是唯一引用键**
    // ⇒ 用这个名字做引用时必须**拒**（候选 = 本章新建的 ∪ 库里已有的）。
    // ⚠️ 注意方向：这里拦的是**引用**，**不是**"只要 force_new 同名就拒"（后者会推翻 T3b 验收标准 5）。
    const std::set<std::string>* forceNewNames = nullptr;
    const EntityNameIndex* index = nullptr;         // 库内名字索引（不含 event）
};

// 校验一个引用字段：`*_id` / **名字** / 本章 `temp_id` 三选一。返回错误文案（空 = 通过）。
// `required`：必填字段在"全都没填"时必须报错（如 `participants[].entity_id`）。
// ★ S87（T1）：**在提交前**就把"歧义 + 候选列表 / 库里没有这个名字"回灌给模型
//（落库时才拒的报文模型看不到 ⇒ 白跑一轮）。判据与落库**完全同源**（`ResolveRefFull` 的规则）。
[[nodiscard]] std::string CheckRef(std::string_view where, RowId id, const std::string& refRaw,
                                   const RefGate& gate, std::string_view kindWanted, bool required) {
    const std::string ref = TrimRef(refRaw);
    // ★ T4-D9（扩到引用值）：引用值不许是"**已知 temp-id 前缀 + 纯数字**"的**保留形状**（`en:7` / `ev:7`）。
    // 它既不是真实名字、也不是库 id；放过去只会得到"库里没有名为「en:7」的实体"这种**误导**报文。
    // ⚠️ 这条与"`*_ref` 是否需要前缀"**无关**：去前缀 ≠ 允许歧义形状（`en:7` 长得像 id 7，是"把序号当 id"的源头）。
    if (LooksLikeNumericTempId(ref)) {
        return fmt::format("{}：「{}」是**保留形状**（已知 temp-id 前缀 + 纯数字）—— temp_id 只是本章内的"
                           "标签，不是名字也不是库 id；请填库内**数字 id**，或真实名字",
                           where, ref);
    }
    // ★ T4-D12：**同时**给了 `*_id` 与 `*_ref` / `*_temp_id` ⇒ 必须**指向同一实体**
    //（**冗余允许、冲突拒绝** —— 见 `Plan/HANDOFF-novel-semantic-ref.md` 的四态表）。
    // 门禁**尽力**判（能解的才判）：解不动的情形（本章 temp_id / 本章声明的名字 / 索引不可用）
    // 交给**落库时**的同一判定（`ResolveRefFull` 权威）。
    if (id > 0 && !ref.empty()) {
        RowId same = -1;
        const bool chapterLocal =
            (gate.tempIds != nullptr && gate.tempIds->count(ref) > 0) ||
            (gate.names != nullptr && gate.names->count(NormalizeRefName(ref)) > 0);
        if (!chapterLocal && gate.index != nullptr && gate.index->loaded()) {
            const std::vector<NameHit> hits = gate.index->Find(NormalizeRefName(ref), kindWanted);
            if (hits.size() == 1) {
                same = hits[0].id;
            }
        }
        if (same > 0 && same != id) {
            return fmt::format("D12：{} 同时给了 id={} 与 ref=「{}」⇒ 解析到 **#{}**：两个说法指向**不同**"
                               "实体，请只留一个（模型不该自相矛盾；系统不替你选）",
                               where, id, ref, same);
        }
        return {}; // 一致（或门禁判不动 ⇒ 落库时再判）
    }
    if (id > 0) {
        return {}; // 已存在的库 id：存在性与 kind 由 K02/K03 判（不在这里重复）
    }
    if (ref.empty()) {
        return required
                   ? fmt::format("{} 既没填已有实体 id，也没填**名字** / 本章 temp_id（三者必有一）",
                                 where)
                   : std::string{};
    }
    if (gate.tempIds != nullptr && gate.tempIds->count(ref) > 0) {
        return {}; // 本章 entities[] / events[] 声明的 temp_id
    }
    bool allDigits = true;
    for (const char c : ref) {
        if (c < '0' || c > '9') {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        return {}; // 数字字符串 ⇒ 当 id 用（存在性由 K02/K03 判）
    }
    const std::string norm = NormalizeRefName(ref);
    // ★ T4-①：这个引用值正是**本章 `force_new` 新建**的那个名字，而**库内同 kind 同名已有实体**
    // ⇒ **该名字不再是唯一引用键**（两个候选：本章新建的 / 库里已有的）⇒ **拒**。
    // ⚠️ 只拦"**把该名字用作引用**"这一侧；**不拦** `force_new` 本身（只声明、不被引用 ⇒ 放行）。
    if (gate.forceNewNames != nullptr && gate.forceNewNames->count(norm) > 0 && gate.index != nullptr &&
        gate.index->loaded()) {
        const std::vector<NameHit> same = gate.index->Find(norm, kindWanted);
        if (!same.empty()) {
            return fmt::format("{}：「{}」在**本章**用 `force_new` 新建，而库里同 kind 同名的已有 {} ⇒ "
                               "这个名字**不再是唯一引用键**（本章新建的与库里已有的都是候选）。"
                               "请用 **temp_id** 指定本章那个，或直接填数字 id",
                               where, ref, DescribeHits(same));
        }
    }
    if (gate.names != nullptr && gate.names->count(norm) > 0) {
        return {}; // 本章 entities[] 声明的名字（新建）
    }
    if (gate.index == nullptr || !gate.index->loaded()) {
        return {}; // 没有名字索引 ⇒ 不在这里判（落库时再拒）
    }
    const std::vector<NameHit> hits = gate.index->Find(norm, kindWanted);
    if (hits.size() == 1) {
        return {}; // 唯一命中 ⇒ 复用
    }
    if (hits.size() > 1) {
        return fmt::format("{}：「{}」**有歧义** ⇒ {}。请直接填要用的实体 **id（数字）**，"
                           "或声明新建（entities[] + 给它 temp_id）—— 系统不按\"最近出现\"替你猜",
                           where, ref, DescribeHits(hits));
    }
    const std::vector<NameHit> otherKind = gate.index->Find(norm, {});
    if (!otherKind.empty()) {
        return fmt::format("{}：「{}」在库里是 {}，此处期望 **{}** —— 名字认对了但 kind 不符",
                           where, ref, DescribeHits(otherKind), KindLabel(kindWanted));
    }
    const std::vector<NameHit> similar = gate.index->Similar(norm);
    return fmt::format("{}：库里没有名为「{}」的{}（已按空白 / 全角 / 大小写归一化）。"
                       "要引用**已有**实体请照库里的名字写；**要新建**请放进 entities[] 并给它 "
                       "temp_id{}",
                       where, ref, KindLabel(kindWanted),
                       similar.empty() ? std::string{}
                                       : fmt::format("；相近名字：{}", DescribeHits(similar)));
}

// ★ S87（T1）：引用字段的**字符串**取法 —— 它可能是 **temp_id**（`en:7` / `谭工`）或**实体名字**。
// 🔴 原先只认"像 temp_id 的形状"（`前缀:数字`）⇒ 模型写**名字**（`"entity_id":"撑伞人影"`）时
//    会**静默留 0**（引用凭空消失），只能靠报错 + 重做。名字 = 引用键之后，这里必须**原样收下**：
//    它到底是 temp_id 还是名字**由 resolver 判**（`ResolveRefFull` 决策树），**不在这里猜形状**。
[[nodiscard]] std::string RefStr(yyjson_val* obj, const char* key) {
    yyjson_val* v = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
    if (v == nullptr || !yyjson_is_str(v)) {
        return {};
    }
    return TrimRef(std::string_view{yyjson_get_str(v), yyjson_get_len(v)});
}

// 依次取第一个非空：`X_id` → `X_ref` → `X_temp_id`（`X_ref` 是"名字引用"的显式写法）
[[nodiscard]] std::string FirstRefStr(yyjson_val* obj, const char* idKey, const char* refKey,
                                      const char* tempKey) {
    for (const char* k : {idKey, refKey, tempKey}) {
        if (const std::string s = RefStr(obj, k); !s.empty()) {
            return s;
        }
    }
    return {};
}

[[nodiscard]] yyjson_val* ArrOf(yyjson_val* root, const char* key) {
    yyjson_val* v = yyjson_obj_get(root, key);
    return (v != nullptr && yyjson_is_arr(v)) ? v : nullptr;
}

// ⚠️ **必须在 `util::reflect::FromJsonString` 之后调用** —— 它按**反射后的数组下标**对齐。
void FillStringRefs(yyjson_val* root, StateDiff& out) {
    if (root == nullptr || !yyjson_is_obj(root)) {
        return;
    }
    const auto each = [](yyjson_val* arr, const auto& fn) {
        if (arr == nullptr) {
            return;
        }
        yyjson_arr_iter it = yyjson_arr_iter_with(arr);
        std::size_t i = 0;
        while (yyjson_val* o = yyjson_arr_iter_next(&it)) {
            fn(o, i++);
        }
    };
    each(ArrOf(root, "characters"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.characters.size()) {
            return;
        }
        if (out.characters[i].entity_temp_id.empty()) {
            out.characters[i].entity_temp_id = FirstRefStr(o, "entity_id", "entity_ref", "entity_temp_id");
        }
        if (out.characters[i].location_temp_id.empty()) {
            out.characters[i].location_temp_id = FirstRefStr(o, "location_id", "location_ref", "location_temp_id");
        }
    });
    each(ArrOf(root, "relationships"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.relationships.size()) {
            return;
        }
        if (out.relationships[i].from_temp_id.empty()) {
            out.relationships[i].from_temp_id = FirstRefStr(o, "from_id", "from_ref", "from_temp_id");
        }
        if (out.relationships[i].to_temp_id.empty()) {
            out.relationships[i].to_temp_id = FirstRefStr(o, "to_id", "to_ref", "to_temp_id");
        }
    });
    each(ArrOf(root, "items"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.items.size()) {
            return;
        }
        if (out.items[i].item_temp_id.empty()) {
            out.items[i].item_temp_id = FirstRefStr(o, "item_id", "item_ref", "item_temp_id");
        }
        if (out.items[i].owner_temp_id.empty()) {
            out.items[i].owner_temp_id = FirstRefStr(o, "owner_id", "owner_ref", "owner_temp_id");
        }
        if (out.items[i].location_temp_id.empty()) {
            out.items[i].location_temp_id = FirstRefStr(o, "location_id", "location_ref", "location_temp_id");
        }
    });
    each(ArrOf(root, "knowledge"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.knowledge.size()) {
            return;
        }
        if (out.knowledge[i].entity_temp_id.empty()) {
            out.knowledge[i].entity_temp_id = FirstRefStr(o, "entity_id", "entity_ref", "entity_temp_id");
        }
    });
    each(ArrOf(root, "events"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.events.size()) {
            return;
        }
        if (out.events[i].location_temp_id.empty()) {
            out.events[i].location_temp_id = FirstRefStr(o, "location_id", "location_ref", "location_temp_id");
        }
        each(ArrOf(o, "participants"), [&](yyjson_val* p, std::size_t j) {
            if (j >= out.events[i].participants.size()) {
                return;
            }
            if (out.events[i].participants[j].entity_temp_id.empty()) {
                out.events[i].participants[j].entity_temp_id = FirstRefStr(p, "entity_id", "entity_ref", "entity_temp_id");
            }
        });
    });
}

} // namespace

// ———— StateDiff 自身 ————

bool StateDiff::HasAnyDelta() const noexcept { return DeltaCount() > 0; }

std::size_t StateDiff::DeltaCount() const noexcept {
    return entities.size() + characters.size() + relationships.size() + items.size() +
           locations.size() + events.size() + causal.size() + plotlines.size() + foreshadows.size() +
           mysteries.size() + knowledge.size() + timeline.size();
}

std::string StateDiff::Hash() const {
    // 规范化 JSON（字段顺序由反射固定）→ FNV-1a → 16 位十六进制
    const std::string json = StateDiffToJson(*this);
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char ch : json) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 1099511628211ULL;
    }
    return fmt::format("{:016x}", hash);
}

std::string StateDiff::Summary() const {
    return fmt::format(
        "章={} 实体+{} 角色{} 关系{} 道具{} 事件{} 因果{} 情节线{} 伏笔{} 谜团{} 知识{} 时间轴{} 地点{}"
        "{}",
        chapter_id, entities.size(), characters.size(), relationships.size(), items.size(),
        events.size(), causal.size(), plotlines.size(), foreshadows.size(), mysteries.size(),
        knowledge.size(), timeline.size(), locations.size(),
        no_change_declared ? "（显式声明无变化）" : "");
}

std::string StateDiffToJson(const StateDiff& diff) {
    return util::reflect::ToJsonString(diff, false); // 紧凑：用于哈希与 `audit_logs.detail`
}

bool StateDiffFromJson(std::string_view text, StateDiff& out) {
    if (util::Trim(text).empty()) {
        return false;
    }
    yyjson_doc* probe = yyjson_read(text.data(), text.size(), 0);
    if (probe == nullptr) {
        // S55：**区分"JSON 语法就过不了"和"语法过了但字段映射不上"** —— 原先三者都返回
        // false、调用方只能笼统地说"不是合法的 StateDiff JSON"，实测时把我自己也误导了
        //（先误判成"markdown 围栏"，其实围栏早修好了）。有区分才知道该改提示词还是改映射。
        log::Warn("StateDiffFromJson：JSON 语法解析失败（{} 字）—— 检查是否被截断/含裸引号",
                  text.size());
        return false;
    }
    const bool isObject = yyjson_is_obj(yyjson_doc_get_root(probe));
    // ★ S61：**契约外的顶层键必须可见**。宽容读取（缺键走默认）是刻意的，但它会把
    // "键名根本不属于这套契约"也一并吞掉：实测 Extractor 的提示词写的是
    // `new_entities` / `foreshadow_updates`（契约里是 `entities` / `foreshadows`）⇒ 模型报的
    // 新人物/新伏笔被**静默丢弃**，只有同名的 `events` 落了库（四章 diff 全是 events=6、
    // 其余 0，库里 person 恒 1）。⇒ 契约外的键一律告警，键名一错第一次跑就看得见。
    // `summary` 是 `NovelDirector` 自己的字段（不在 StateDiff 契约内，单独读取）→ 白名单。
    if (isObject) {
        constexpr auto kKnown = util::reflect::FieldNames<StateDiff>();
        std::string unknown;
        std::size_t nUnknown = 0;
        yyjson_obj_iter it = yyjson_obj_iter_with(yyjson_doc_get_root(probe));
        for (yyjson_val* k = yyjson_obj_iter_next(&it); k != nullptr; k = yyjson_obj_iter_next(&it)) {
            const std::string_view key{yyjson_get_str(k), yyjson_get_len(k)};
            if (key == "summary") continue; // 见上：本模块自用字段
            bool known = false;
            for (const std::string_view n : kKnown) {
                if (n == key) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                ++nUnknown;
                if (nUnknown <= 6) {
                    unknown += (unknown.empty() ? "" : ", ") + std::string{key};
                }
            }
        }
        if (nUnknown > 0) {
            log::Warn("StateDiffFromJson：{} 个**契约外的顶层键**会被忽略（{}）—— 键名须与 "
                      "`02` §2.5 一致（entities/characters/relationships/…），否则模型报的 "
                      "delta 会静默丢失；请核对**提示词里的形状**与契约是否同一套",
                      nUnknown, unknown);
        }
    }
    if (!isObject) {
        log::Warn("StateDiffFromJson：JSON 能解析但**根不是对象**（StateDiff 必须是 `{{...}}`）");
        yyjson_doc_free(probe);
        return false;
    }
    const int filled = util::reflect::FromJsonString(text, out);
    // ★ S65：反射**之后**再补"字符串 TempId"那一路引用（要按反射后的数组下标对齐）。
    // ⚠️ 顺序不能反：放在反射前的话，数组还是空的，什么都补不上。
    FillStringRefs(yyjson_doc_get_root(probe), out);
    yyjson_doc_free(probe);
    if (filled <= 0) {
        log::Warn("StateDiffFromJson：JSON 合法但**字段一个都没映射上**（{} 字）"
                  "—— 多半是键名/结构不符合 `02` §2.5 的契约（不是格式问题）",
                  text.size());
    }
    return filled > 0;
}

// ———— 契约校验（`07` §2.2 的 ③，机器部分）————

// ★ S77/T4：`LooksLikeNumericTempId()` **定义已上移到 `CheckRef` 之前**（T4 把它扩到引用值，
// 两处都要用；留在命名空间作用域会和匿名命名空间的前置声明撞成重载歧义）。

std::vector<CommitIssue> ValidateStateDiff(db::sqlite::Database& db, const StateDiff& diff) {
    std::vector<CommitIssue> out;
    NovelGraph graph(db);

    if (diff.chapter_id <= 0) {
        out.push_back({"contract", "high", "StateDiff 缺 chapter_id"});
    }
    // D7：显式声明无变化时子数组必须为空
    if (diff.no_change_declared && diff.HasAnyDelta()) {
        out.push_back({"contract", "high",
                       fmt::format("D7：no_change_declared=true，但子数组非空（共 {} 条 delta）",
                                   diff.DeltaCount())});
    }

    std::set<std::string> declaredDeadNames;
    for (std::size_t i = 0; i < diff.entities.size(); ++i) {
        const NewEntityDelta& e = diff.entities[i];
        if (e.temp_id.empty()) {
            out.push_back({"contract", "high", fmt::format("entities[{}] 缺 temp_id", i)});
        }
        if (e.name.empty()) {
            out.push_back({"contract", "high", fmt::format("entities[{}]（temp_id={}）缺 name", i, e.temp_id)});
        }
        // D8：kind 必须在 31 种内
        if (e.kind.empty() || KnownKinds().find(e.kind) == KnownKinds().end()) {
            out.push_back({"contract", "high",
                           fmt::format("D8：NewEntity「{}」的 kind「{}」不在 31 种元类别内",
                                       e.name, e.kind)});
        }
        if (e.status == "dead" || e.status == "destroyed") {
            declaredDeadNames.insert(e.name);
        }
    }

    // ★ S77：**D9 —— `temp_id` 不许写成"前缀+纯数字"**（`en:1` / `ev:7`）。
    // 这不是风格问题，是**歧义源**（真跑两组独立实证：第 13 章把 `en:1`/`en:2` 的序号填进
    // `characters[].entity_id`；第 16 章把 `en:2`/`en:3` 的序号填进 `entity_id`/`location_id`，
    // 一次跑出 **12 处**归一化拒绝）—— `en:7` **长得就像 id 7**，而同一份输出里 `*_id` 字段要填
    // **库里的数字 id** ⇒ 模型天然把序号填进去，撞上库里同号的实体（#1=universe、#7=event）⇒
    // K03 挡下整章、重做两轮都改不动。**在入口把歧义消灭**：`temp_id` 只是**本章内的标签**，
    // 必须写成有语义的字符串（`en:断头挂绳`）—— 这样"填 7"就失去了来源。
    for (std::size_t i = 0; i < diff.entities.size(); ++i) {
        if (LooksLikeNumericTempId(diff.entities[i].temp_id)) {
            out.push_back({"contract", "high",
                           fmt::format("D9：entities[{}] 的 temp_id「{}」不能用「前缀+纯数字」—— "
                                       "temp_id 只是**本章内的标签、不是 id**；请改成有语义的写法，"
                                       "如 `en:断头挂绳`（数字写法会诱导把序号当库 id 填进 *_id 字段）",
                                       i, diff.entities[i].temp_id)});
        }
    }

    // ★ S80：**D11 —— `items[].op="acquire"` 必须给持有者**（`owner_id` 或 `owner_temp_id`）。
    // 与 D10 **同一个毛病**：这条原先也只在**落库时**（块 4 的守卫，S69 加）挡 ⇒ 报文是
    // "块 4 `acquire` 的持有者解析不到真实 id"，**不在 K01–K29 里**，模型看不到该改什么
    //（真跑实证：第 18 章两轮都卡这一条，而重做提示里那句措辞它不听 —— 契约规则才拦得住）。
    for (std::size_t i = 0; i < diff.items.size(); ++i) {
        const ItemDelta& it = diff.items[i];
        if (it.op == "acquire" && it.owner_id <= 0 && it.owner_temp_id.empty()) {
            out.push_back({"contract", "high",
                           fmt::format("D11：items[{}] 是 `acquire` 却**没给持有者** —— 必须给 "
                                       "`owner_id`（库内 person 的 id）或 `owner_temp_id`（本章新建"
                                       "人物）；若只是换地点/换状态，请用 `move` / `change_state`",
                                       i)});
        }
    }

    // ★ S79：**D10 —— 因果边必须"两端都有、且不是自环"**。
    // 原先这两条只在**落库时**检查（`NovelGraph::UpsertCausalLink`，块 6 内）——
    // 那时事务已经开了，报文是"块 6 因果失败：因果边禁止自环"，**不在 K01–K29 里**，模型看不到
    // 该改什么（真跑实证：第 18 章连着两轮卡在这一条）。挪到契约层 ⇒ 与其它 D 规则一样进
    // `gates.issues`，由重做提示回灌给模型（`NovelDirector` 现在会把 `commit.error` 一起回灌）。
    for (std::size_t i = 0; i < diff.causal.size(); ++i) {
        const CausalDelta& cd = diff.causal[i];
        const auto emptyRef = [](const TempoRef& r) { return r.temp_id.empty() && r.entity_id <= 0; };
        if (emptyRef(cd.cause) || emptyRef(cd.effect)) {
            out.push_back({"contract", "high",
                           fmt::format("D10：causal[{}] 的 cause/effect **两端都要给**（本章 TempId 或"
                                       "已存在实体 id）—— 不许留空、不许填 0",
                                       i)});
            continue;
        }
        // 自环只在"可比"时判（两边同用 TempId、或同用实体 id；一边 TempId 一边 id 无法在契约层比）
        const bool sameTemp = !cd.cause.temp_id.empty() && cd.cause.temp_id == cd.effect.temp_id;
        const bool sameId = cd.cause.entity_id > 0 && cd.cause.entity_id == cd.effect.entity_id;
        if (sameTemp || sameId) {
            out.push_back({"contract", "high",
                           fmt::format("D10：causal[{}] 是**自环**（cause 与 effect 指向同一个{}）——"
                                       "因果边要连两个**不同**的事件；请删掉它或改其中一端",
                                       i, sameTemp ? "TempId" : "实体 id")});
        }
    }

    // 新实体还没写库 → character 只能引用**已存在**实体（D1）
    // ★ S65：本章声明过的 TempId（`entities[]` + `events[]`）—— 下面所有引用字段共用
    const std::set<std::string> declaredTemp = DeclaredTempIds(diff);
    // ★ S87（T1）：门禁再加**库内名字索引** + 本章 `entities[]` 声明的名字（归一化）——
    // 自此门禁与落库**同口径地"认名字"**（决策树见 `ResolveRefFull`：唯一命中过 / 歧义拒 / 未命中拒）。
    const EntityNameIndex nameIndex(db);
    std::set<std::string> declaredNames;
    for (const NewEntityDelta& e : diff.entities) {
        const std::string n = NormalizeRefName(e.name);
        if (!n.empty()) {
            declaredNames.insert(n);
        }
    }
    // ★ T4-①：本章用 `force_new` 声明的名字（归一化）—— 用于"**按引用值**"拦截（见 `RefGate`）
    std::set<std::string> forceNewNames;
    for (const NewEntityDelta& e : diff.entities) {
        if (e.force_new) {
            if (const std::string n = NormalizeRefName(e.name); !n.empty()) {
                forceNewNames.insert(n);
            }
        }
    }
    const RefGate refGate{&declaredTemp, &declaredNames, &forceNewNames, &nameIndex};
    for (const CharacterDelta& c : diff.characters) {
        if (const std::string err = CheckRef("characters[].entity_id", c.entity_id, c.entity_temp_id,
                                             refGate, kind::person, /*required=*/true);
            !err.empty()) {
            out.push_back({"contract", "high", err});
            continue;
        }
        if (const std::string err = CheckRef("characters[].location_id", c.location_id,
                                             c.location_temp_id, refGate, kind::location,
                                             /*required=*/false);
            !err.empty()) {
            out.push_back({"contract", "high", err});
        }
        // 后半段（存在性 / reason / D4）只对**已有实体**有意义：新建实体没有"上一版状态"可比。
        // ★ S87：**名字引用也算"已有实体"** —— 先把名字落到真实 id 再检查，否则"用名字"就成了
        // 绕过 reason / D4 的后门（原先 `entity_id<=0` 直接 continue，是"只认数字 id"时代的写法）。
        const RowId cEid =
            c.entity_id > 0
                ? c.entity_id
                : ResolveRefFull(0, c.entity_temp_id, RefCtx{nullptr, nullptr, &nameIndex}, kind::person)
                      .id;
        if (cEid <= 0) {
            continue; // 本章 temp_id / 本章新建的名字：没有"上一版状态"可比，跳过
        }
        if (!graph.GetEntity(cEid)) {
            out.push_back({"contract", "high",
                           fmt::format("D1：character.entity_id={} 既不在 entities 也没在本章 NewEntity 声明",
                                       cEid)});
            continue;
        }
        if (c.reason.empty()) {
            out.push_back({"evidence_missing", "low",
                           fmt::format("character {} 没写 reason（状态变化必须有因）", cEid)});
        }
        // D4：出现 dead/destroyed 必须显式声明
        const bool saysDead = Contains(c.body_state, "dead") || Contains(c.body_state, "destroyed") ||
                              Contains(c.mind_state, "dead") || Contains(c.mind_state, "destroyed");
        if (saysDead) {
            const auto entity = graph.GetEntity(cEid);
            const std::string name = entity ? entity->name : std::string{};
            const bool declared = !name.empty() && declaredDeadNames.count(name) > 0;
            const bool alreadyDead = entity && (entity->status == "dead" || entity->status == "destroyed");
            if (!declared && !alreadyDead) {
                out.push_back({"high", "high",
                               fmt::format("D4：character {} 出现 dead/destroyed，但没有 entities[] 里的 "
                                           "status 变更声明（`02` §2.5 的 entities 只有 NewEntity —— "
                                           "既有实体的状态变更需由调用方另行声明）",
                                           cEid)});
            }
        }
    }

    // 事件参与者的 entity_id 必须存在（S65：或引用本章新建实体的 temp_id）
    for (const EventDelta& ev : diff.events) {
        if (const std::string err = CheckRef("events[].location_id", ev.location_id,
                                             ev.location_temp_id, refGate, kind::location,
                                             /*required=*/false);
            !err.empty()) {
            out.push_back({"contract", "high", fmt::format("事件「{}」：{}", ev.temp_id, err)});
        }
        for (const EventParticipantDelta& p : ev.participants) {
            // 🔴 S64：**门禁必须与落库同口径** —— 原先只校 `p.entity_id > 0 && 不存在`，
            // 于是 `entity_id = 0` **溜过门禁**，却在**块 5** 落库时炸
            // （`块 5 参与者失败：事件参与必须指定 event_id 与 entity_id`）⇒ 那一轮 LLM 白跑。
            // S65：改成"`entity_id` 与 `entity_temp_id` **二选一**" —— 后者用于**本章新建**的角色
            //（原先没有这个写法，模型才会把 `en:7` 的序号 7 当 id 填）。
            if (const std::string err =
                    CheckRef("events[].participants[].entity_id", p.entity_id, p.entity_temp_id,
                             refGate, {}, /*required=*/true);
                !err.empty()) {
                out.push_back({"contract", "high", fmt::format("事件「{}」：{}", ev.temp_id, err)});
                continue;
            }
            if (p.entity_id > 0 && !graph.GetEntity(p.entity_id)) {
                out.push_back({"contract", "high",
                               fmt::format("D1：事件「{}」的参与者 entity_id={} 不存在", ev.temp_id, p.entity_id)});
            }
        }
    }

    // S65：这几类引用**此前门禁完全没校**（只靠 K02/K03 的存在性/kind）——
    // 补上"temp_id 是否在本章声明"，否则一个写错的 temp_id 会静默解析成 0（引用丢失）。
    for (const RelationDelta& r : diff.relationships) {
        const std::pair<const char*, std::pair<RowId, const std::string*>> fields[] = {
            {"relationships[].from_id", {r.from_id, &r.from_temp_id}},
            {"relationships[].to_id", {r.to_id, &r.to_temp_id}},
        };
        for (const auto& [where, v] : fields) {
            if (const std::string err = CheckRef(where, v.first, *v.second, refGate, {}, true);
                !err.empty()) {
                out.push_back({"contract", "high", err});
            }
        }
    }
    for (const ItemDelta& it : diff.items) {
        // ★ S87：每列**各自的期望 kind**（物品=item / 持有者=person / 地点=location）——
        // 名字引用因此能当场判出"名字对了但 kind 不符"，不必等到落库后的 K03。
        const std::tuple<const char*, RowId, const std::string*, std::string_view> fields[] = {
            {"items[].item_id", it.item_id, &it.item_temp_id, kind::item},
            {"items[].owner_id", it.owner_id, &it.owner_temp_id, kind::person},
            {"items[].location_id", it.location_id, &it.location_temp_id, kind::location},
        };
        for (const auto& [where, id, ref, k] : fields) {
            const bool required = std::string_view{where} == "items[].item_id";
            if (const std::string err = CheckRef(where, id, *ref, refGate, k, required); !err.empty()) {
                out.push_back({"contract", "high", err});
            }
        }
    }
    for (const KnowledgeDelta& k : diff.knowledge) {
        if (const std::string err = CheckRef("knowledge[].entity_id", k.entity_id, k.entity_temp_id,
                                             refGate, kind::person, /*required=*/true);
            !err.empty()) {
            out.push_back({"contract", "high", err});
        }
    }
    return out;
}

// ★ S65b：见头文件注释。判别力 = "按字面解释必然错"才改写。
int NormalizeNumericTempRefs(db::sqlite::Database& db, StateDiff& diff) {
    // 本章声明的 `en:N` / `ev:N` 按**序号**建索引（模型常把 N 当 id 填）
    std::map<RowId, std::string> byOrd;
    // ★ S67：**改写后必须复核 kind** —— 否则就是把"能被 K03 挡下的错"变成"挡不下的错"。
    // 🔴 真跑事故：`items[].owner_id = 36` 字面指向的是 `kind=item` 的实体（期望 person）⇒ 被判
    //   "字面必然错"改写成 `en:36`，而本章的 `en:36` 恰好**也是那个物品** ⇒ K03 再也看不到它，
    //   落库成"物品持有物品 / 自己持有自己"（`entity_ownerships` 里出现 `item 36 ← owner 36`），
    //   还把 K07（同一物品多个持有者）**永久**挡死。所以改写前必须确认"改成 TempId 之后 kind 也对"。
    std::map<RowId, std::string> kindByOrd;
    const auto addTemp = [&byOrd, &kindByOrd](const std::string& tid, std::string_view kind) {
        const auto colon = tid.rfind(':');
        if (colon == std::string::npos || colon + 1 >= tid.size()) {
            return;
        }
        RowId n = 0;
        for (std::size_t i = colon + 1; i < tid.size(); ++i) {
            if (tid[i] < '0' || tid[i] > '9') {
                return;
            }
            n = n * 10 + static_cast<RowId>(tid[i] - '0');
            if (n > 100000000) {
                return;
            }
        }
        if (n > 0) {
            if (byOrd.find(n) == byOrd.end()) {
                byOrd[n] = tid;
            }
            if (kindByOrd.find(n) == kindByOrd.end()) {
                kindByOrd[n] = std::string{kind};
            }
        }
    };
    for (const NewEntityDelta& e : diff.entities) {
        addTemp(e.temp_id, e.kind);
    }
    for (const EventDelta& e : diff.events) {
        addTemp(e.temp_id, "event");
    }
    if (byOrd.empty()) {
        return 0;
    }
    // 库里该 id 的 kind（不存在 → 空）。`item` 期望放宽到同类 kind —— **与 NovelChecks 的
    // `IsItemKind` 同口径**（那处在 NovelChecks.cpp 的匿名命名空间里，取不到；两张表必须一致）。
    const auto kindOf = [&db](RowId id) -> std::string {
        auto st = db.Prepare("SELECT kind FROM entities WHERE id=?1");
        if (!st) {
            return {};
        }
        (void)st->BindInt(1, id);
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            return st->ColumnText(0);
        }
        return {};
    };
    const auto kindOk = [](std::string_view actual, std::string_view expected) {
        if (expected == kind::item) {
            return actual == kind::item || actual == kind::treasure || actual == kind::prop ||
                   actual == kind::clothing || actual == kind::resource;
        }
        return actual == expected;
    };
    int n = 0;
    // 逐字段处理：`*_id` 字面解释必错、且序号有对应 TempId ⇒ 改写
    const auto fix = [&](const char* where, RowId& id, std::string& tempId,
                         std::string_view expected) {
        if (id <= 0 || !tempId.empty()) {
            return;
        }
        const std::string k = kindOf(id);
        if (kindOk(k, expected)) {
            return; // 字面解释是对的 ⇒ 一个字都不动（不改语义）
        }
        const auto it = byOrd.find(id);
        if (it == byOrd.end()) {
            return;
        }
        // ★ S67：**只在该 TempId 的 kind 也对得上时才改写**（见函数开头的真跑事故注释）。
        // 对不上 ⇒ **一个字都不改**，让 K02/K03 照实报错（"能不能改"必须有判别力）。
        const auto kIt = kindByOrd.find(id);
        const std::string newKind = kIt == kindByOrd.end() ? std::string{} : kIt->second;
        if (!kindOk(newKind, expected)) {
            log::Warn("StateDiff 归一化：{} 的 {} 按字面不成立（库 #{} kind='{}'），而对应的「{}」"
                      "kind='{}' 也不符合期望 {} ⇒ **不改写**（改动只会把错藏起来），交给 K02/K03 报错",
                      where, id, id, k.empty() ? "不存在" : k, it->second,
                      newKind.empty() ? "?" : newKind, expected);
            return;
        }
        log::Warn("StateDiff 归一化：{} 的 {} 字面解释不成立（库 #{} 的 kind='{}'，期望 {}）"
                  "而本章声明了「{}」（kind='{}'）⇒ 按 TempId 解释（与 `02` §2.5 的 `*_temp_id` 等价）",
                  where, id, id, k.empty() ? "不存在" : k, expected, it->second, newKind);
        tempId = it->second;
        id = 0;
        ++n;
    };
    for (CharacterDelta& c : diff.characters) {
        fix("characters[].entity_id", c.entity_id, c.entity_temp_id, kind::person);
        fix("characters[].location_id", c.location_id, c.location_temp_id, kind::location);
    }
    for (RelationDelta& r : diff.relationships) {
        fix("relationships[].from_id", r.from_id, r.from_temp_id, kind::person);
        fix("relationships[].to_id", r.to_id, r.to_temp_id, kind::person);
    }
    for (ItemDelta& it : diff.items) {
        fix("items[].item_id", it.item_id, it.item_temp_id, kind::item);
        fix("items[].owner_id", it.owner_id, it.owner_temp_id, kind::person);
        fix("items[].location_id", it.location_id, it.location_temp_id, kind::location);
    }
    for (EventDelta& ev : diff.events) {
        fix("events[].location_id", ev.location_id, ev.location_temp_id, kind::location);
        for (EventParticipantDelta& p : ev.participants) {
            fix("events[].participants[].entity_id", p.entity_id, p.entity_temp_id, kind::person);
        }
    }
    for (KnowledgeDelta& k : diff.knowledge) {
        fix("knowledge[].entity_id", k.entity_id, k.entity_temp_id, kind::person);
    }
    return n;
}

std::string CommitGateReport::Describe() const {
    return fmt::format("G1={} G2={} G3={} G4={} G5={}", g1_review_pass ? 1 : 0, g2_checks_pass ? 1 : 0,
                       g3_snapshot_ready ? 1 : 0, g4_diff_valid ? 1 : 0, g5_no_high_issue ? 1 : 0);
}

// ———— 提交 ————

bool ChapterAlreadyCommitted(db::sqlite::Database& db, RowId chapterId, std::string_view diffHash) {
    if (chapterId <= 0 || diffHash.empty()) {
        return false;
    }
    auto st = db.Prepare(
        "SELECT COUNT(*) FROM audit_logs WHERE action='commit_chapter' AND target_kind='chapter' "
        "AND target_id=?1 AND detail LIKE ?2");
    if (!st) {
        return false;
    }
    (void)st->BindInt(1, chapterId);
    (void)st->BindText(2, fmt::format("%diff_hash={}%", diffHash));
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        return st->ColumnInt(0) > 0;
    }
    return false;
}

CommitResult CommitChapterState(db::sqlite::Database& db, const StateDiff& diff,
                                const CommitContext& ctx) {
    CommitResult out;
    out.diff_hash = diff.Hash();
    out.gates.issues = ValidateStateDiff(db, diff);
    for (const CommitIssue& issue : ctx.upstream_issues) {
        out.gates.issues.push_back(issue);
    }

    if (diff.chapter_id <= 0) {
        out.error = "StateDiff 缺 chapter_id";
        return out;
    }
    // 模式校验放在幂等判定**之前**：模式本身非法是"参数错"，不该被"已提交过"盖掉
    // S9（`07` §2.5）：manual → PROPOSED；auto → CANON。**auto 不跳门禁**（C5）：
    // 下面的 G1–G5 判定对两种模式完全一致，`auto` 只把块 13 的状态从 PROPOSED 换成 CANON。
    const std::string canonMode = ctx.canon_mode.empty() ? "manual" : ctx.canon_mode;
    const bool autoCanon = canonMode == "auto";
    if (!autoCanon && canonMode != "manual") {
        out.error = fmt::format("canon_mode='{}' 非法（只能 manual / auto，07 §2.5）", canonMode);
        return out;
    }
    // I1：同章 + 同 diff 哈希 → 跳过（幂等）
    if (ChapterAlreadyCommitted(db, diff.chapter_id, out.diff_hash)) {
        out.ok = true;
        out.skipped = true;
        out.gates.ok = true;
        log::Info("章节 {} 的这份 StateDiff 已提交过（diff_hash={}），按幂等跳过", diff.chapter_id,
                  out.diff_hash);
        return out;
    }

    // 涉及实体（用于提交前快照）
    std::set<RowId> touched;
    for (const CharacterDelta& c : diff.characters) {
        touched.insert(c.entity_id);
    }
    for (const RelationDelta& r : diff.relationships) {
        touched.insert(r.from_id);
        touched.insert(r.to_id);
    }
    for (const ItemDelta& it : diff.items) {
        touched.insert(it.owner_id);
        touched.insert(it.item_id);
    }
    for (const KnowledgeDelta& k : diff.knowledge) {
        touched.insert(k.entity_id);
    }
    for (const EventDelta& ev : diff.events) {
        for (const EventParticipantDelta& p : ev.participants) {
            touched.insert(p.entity_id);
        }
    }
    touched.erase(0);

    // G3 / 不变式 I11：**提交前**必须已有快照。实体级快照在事务里采（Before 值），
    // 章级快照文件在事务外先落盘；写失败即阻断。
    //
    // ⚠️ 顺序：快照必须排在 G2 **之前** —— K13（`snapshot.before_commit`）的受检对象就是
    // 这个文件，先写它这条才有对象可判（S11 把 K 校验接进提交路径时踩实的）。
    const auto snapshot = WriteChapterSnapshot(ctx.snapshot_dir, diff, out.diff_hash,
                                               out.entity_version_ids);
    if (!snapshot) {
        out.gates.g3_snapshot_ready = false;
        out.error = snapshot.error();
    } else {
        out.gates.g3_snapshot_ready = true;
        out.snapshot_path = util::PathToUtf8(*snapshot);
    }

    // `07` §2.1 / 不变式 I10：`work/ch<NNN>/12_state_diff.json` 必须存在。原先**没有任何写入点**
    // （`03` 的阶段产物未落地）→ K12 只能拿内存对象当受检对象。这里补上：本章的 StateDiff 与
    // 正文/快照一起留档，断点续跑与事后审计都有据可查。写失败**只告警**（审计产物，不阻断提交）。
    if (!ctx.project_dir.empty() && diff.chapter_id > 0) {
        RowId ord = 0;
        if (auto st = db.Prepare("SELECT ord FROM chapters WHERE id=?1"); st) {
            (void)st->BindInt(1, diff.chapter_id);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                ord = st->ColumnInt(0);
            }
        }
        if (ord > 0) {
            const std::filesystem::path dir =
                util::PathFromUtf8(ctx.project_dir) / "work" / fmt::format("ch{:03}", ord);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!util::WriteFileBytes(dir / "12_state_diff.json", StateDiffToJson(diff))) {
                log::Warn("章节 {} 的 12_state_diff.json 落盘失败（不影响提交）", diff.chapter_id);
            }
        }
    }

    // ———— G2：`06` §2.3 的 K01–K29 全量报告（S11 起是**真门禁**）————
    // 调用方给了报告就用它（`g2_source="caller"`）；没给则在本函数内跑一遍（`"inline"`）——
    // 这样「谁能提交」在任何入口都是同一套判据，不给调用方留后门。
    // 不通过的条目以 `{check_id, severity, detail}` 并入 issue 账 → G5 与拒绝原因都能指名道姓。
    bool g2Pass = true;
    if (ctx.validation != nullptr) {
        out.gates.g2_source = "caller";
        g2Pass = ctx.validation->Ok();
        out.checks_describe = ctx.validation->Describe();
        out.failed_check_ids = ctx.validation->FailedIds();
        for (const CheckResult& cr : ctx.validation->checks) {
            if (CheckPassed(cr)) {
                continue;
            }
            out.gates.issues.push_back({fmt::format("{} {}", cr.check_id, cr.name), cr.severity,
                                        cr.detail});
        }
    } else {
        out.gates.g2_source = "inline";
        CheckInputs cin;
        cin.chapter_id = diff.chapter_id;
        cin.project_dir = util::PathFromUtf8(ctx.project_dir);
        cin.snapshot_dir = util::PathFromUtf8(ctx.snapshot_dir);
        cin.diff = &diff; // 内存里的 StateDiff 就是 K12 的受检对象（`03` 的产物落盘尚未落地）
        const ValidationReport report = RunChapterChecks(db, cin);
        g2Pass = report.Ok();
        out.checks_describe = report.Describe();
        out.failed_check_ids = report.FailedIds();
        for (const CheckResult& cr : report.checks) {
            if (CheckPassed(cr)) {
                continue;
            }
            out.gates.issues.push_back({fmt::format("{} {}", cr.check_id, cr.name), cr.severity,
                                        cr.detail});
        }
    }

    const auto hasHigh = [&out]() {
        for (const CommitIssue& issue : out.gates.issues) {
            if (issue.severity == "high") {
                return true;
            }
        }
        return false;
    };

    // G4：契约校验通过且非空（或显式声明无变化）
    out.gates.g4_diff_valid = !hasHigh() && (diff.HasAnyDelta() || diff.no_change_declared);
    // ★ S67b：**单独标出"空 diff 且未声明无变化"** —— 它是**唯一**一类"重跑 extractor 恰恰有效"的
    // G4 失败（真跑实证：第 9 章模型 0 次工具调用、直接吐全空 diff ⇒ G4 拒；而调用方原先按
    // "非 G2 不重做"短路 ⇒ 一整章白废）。其余 G4 失败（如 D7 自相矛盾）仍不该重做。
    out.diff_empty_undeclared = !hasHigh() && !diff.HasAnyDelta() && !diff.no_change_declared;
    // G1 / G2 / G5
    out.gates.g1_review_pass = ctx.review_pass;
    out.gates.g2_checks_pass = g2Pass && !hasHigh();
    out.gates.g5_no_high_issue = !hasHigh();

    out.gates.ok = out.gates.g1_review_pass && out.gates.g2_checks_pass &&
                   out.gates.g3_snapshot_ready && out.gates.g4_diff_valid && out.gates.g5_no_high_issue;
    if (!out.gates.ok) {
        std::string reason;
        for (const CommitIssue& issue : out.gates.issues) {
            if (issue.severity == "high") {
                reason = fmt::format("；{}：{}", issue.code, issue.detail);
                break;
            }
        }
        out.error = fmt::format("提交门禁未通过（{}）{}", out.gates.Describe(),
                                out.error.empty() ? reason : ("；" + out.error));
        return out;
    }

    // ———— 14 块事务 ————
    NovelGraph graph(db);
    if (auto begin = db.Exec("BEGIN IMMEDIATE"); !begin) {
        out.error = fmt::format("BEGIN 失败：{}", begin.error().message);
        return out;
    }
    const auto fail = [&](std::string message) {
        (void)db.Exec("ROLLBACK");
        out.ok = false;
        out.error = std::move(message);
        return out;
    };

    std::map<std::string, RowId> tempIds;
    // ★ S87（T1）：**语义引用解析**的上下文 ——
    //   · `chapterNames`：本章 `entities[]` 的名字 → 真实 id **列表**（模型用**名字**引用本章新建实体时用；
    //     `index` 在块 1 之后才建，所以先靠它兜住"本章新建"这一路；列表 >1 = 本章有两个同名实体 ⇒ 拒）；
    //   · `rctx.index`：库内"名字 → 候选"（块 1 之后建 ⇒ 本章刚落库的实体也在其中）。
    std::map<std::string, std::vector<RowId>> chapterNames;
    RefCtx rctx{&tempIds, &chapterNames, nullptr, &db};

    // 块 0（写在其它块之前）：被改实体的 **Before 快照**（`07` §2.4「章级快照内容 = diff + Before 值」）
    for (const RowId id : touched) {
        if (auto saved = graph.SnapshotEntity(id, fmt::format("ch{} commit (before)", diff.chapter_id));
            saved) {
            out.entity_version_ids.push_back(*saved);
        }
    }
    out.applied.push_back(fmt::format("快照: 实体 {} 个", out.entity_version_ids.size()));

    // 块 1：entities（先插入，回填 TempId → 真实 id）
    for (const NewEntityDelta& e : diff.entities) {
        EntityRow row;
        row.kind = e.kind;
        row.name = e.name;
        row.summary = e.summary;
        row.status = e.status.empty() ? "active" : e.status;
        row.created_chapter = e.created_chapter > 0 ? e.created_chapter : diff.chapter_id;
        // ★ T3b：**显式新建**（`force_new`）—— 明知同名也建（决策树最后一支；审计见 `UpsertEntity`）
        row.force_new = e.force_new;
        auto id = graph.UpsertEntity(row);
        if (!id) {
            return fail(fmt::format("块 1 entities 失败：{}", id.error().message));
        }
        if (!e.temp_id.empty()) {
            tempIds[e.temp_id] = *id;
        }
        // ★ S87：名字也进映射（模型很自然的写法："上文刚声明 `谭工`，下面就写 `谭工`"）。
        // 同名复用（S84）时这里指向被复用的那个 id ⇒ 两路一致。
        if (const std::string n = NormalizeRefName(e.name); !n.empty()) {
            chapterNames[n].push_back(*id);
        }
        out.applied.push_back(fmt::format("entities: +1（{} → #{}）", e.name, *id));
    }

    // ★ S87：建**库内名字索引**（放在块 1 之后 ⇒ 本章刚落库的实体也在索引里；`event` 已排除）
    const EntityNameIndex nameIndex(db);
    rctx.index = &nameIndex;

    // 块 2：character_status（I2：先删同 (entity_id, chapter_id) 再插，重复提交不叠加）
    for (const CharacterDelta& c : diff.characters) {
        // S87：引用解析（数字 id / 本章 temp_id / **名字** —— 决策树见 `ResolveRefFull`）
        const RowId cEid = ResolveRef(c.entity_id, c.entity_temp_id, rctx, kind::person);
        const RowId cLid = ResolveRef(c.location_id, c.location_temp_id, rctx, kind::location);
        if (cEid <= 0) {
            return fail(RefFail("块 2 角色", c.entity_id, c.entity_temp_id, rctx, kind::person));
        }
        if (auto del = db.Prepare("DELETE FROM character_status WHERE entity_id=?1 AND chapter_id=?2");
            del) {
            (void)del->BindInt(1, cEid);
            (void)del->BindInt(2, diff.chapter_id);
            if (auto s = del->Step(); !s) {
                return fail(fmt::format("块 2 清理旧状态失败：{}", s.error().message));
            }
        }
        CharacterStatusRow row;
        row.entity_id = cEid;
        row.chapter_id = diff.chapter_id;
        row.location_id = cLid;
        row.body_state = c.body_state;
        row.mind_state = c.mind_state;
        row.emotion_json = c.emotion_json.empty() ? "{}" : c.emotion_json;
        row.goal = c.goal;
        row.relation_note = c.relation_note;
        row.resource_note = c.resource_note;
        row.secret_note = c.secret_note;
        if (auto id = graph.UpsertCharacterStatus(row); !id) {
            return fail(fmt::format("块 2 character_status 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("character_status: 实体 #{} @ch{}", cEid,
                                          diff.chapter_id));
    }

    // 块 3：relations（upsert / close）
    for (const RelationDelta& r : diff.relationships) {
        // S87：端点解析（关系两端可以是**任何** kind ⇒ 期望 kind 传空）
        const RowId rFrom = ResolveRef(r.from_id, r.from_temp_id, rctx, {});
        const RowId rTo = ResolveRef(r.to_id, r.to_temp_id, rctx, {});
        if (rFrom <= 0) {
            return fail(RefFail("块 3 关系 from", r.from_id, r.from_temp_id, rctx, {}));
        }
        if (rTo <= 0) {
            return fail(RefFail("块 3 关系 to", r.to_id, r.to_temp_id, rctx, {}));
        }
        if (r.op == "close") {
            auto st = db.Prepare(
                "UPDATE relations SET to_chapter=?1 WHERE from_id=?2 AND to_id=?3 AND rel_type=?4 "
                "AND (to_chapter=0 OR to_chapter>?1)");
            if (!st) {
                return fail(fmt::format("块 3 close 准备失败：{}", st.error().message));
            }
            (void)st->BindInt(1, diff.chapter_id);
            (void)st->BindInt(2, rFrom);
            (void)st->BindInt(3, rTo);
            (void)st->BindText(4, r.rel_type);
            if (auto s = st->Step(); !s) {
                return fail(fmt::format("块 3 close 失败：{}", s.error().message));
            }
            out.applied.push_back(fmt::format("relations: close #{}→#{}", rFrom, rTo));
            continue;
        }
        RelationRow row;
        row.from_id = rFrom;
        row.to_id = rTo;
        row.rel_type = r.rel_type;
        row.strength = r.strength;
        row.from_chapter = diff.chapter_id;
        row.reason = r.reason;
        if (auto id = graph.UpsertRelation(row); !id) {
            return fail(fmt::format("块 3 relations 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("relations: upsert #{}→#{}（{}）", rFrom, rTo, r.rel_type));
    }

    // 块 4：entity_ownerships（acquire / lose）
    for (const ItemDelta& it : diff.items) {
        // S87：物品=item / 持有者=person（**名字引用时期望 kind 一起判**）
        const RowId iItem = ResolveRef(it.item_id, it.item_temp_id, rctx, kind::item);
        const RowId iOwner = ResolveRef(it.owner_id, it.owner_temp_id, rctx, kind::person);
        if (iItem <= 0) {
            return fail(RefFail("块 4 物品", it.item_id, it.item_temp_id, rctx, kind::item));
        }
        // ⚠️ S87：`lose` 也**必须**有持有者 —— 否则 `owner_id=0` 的 UPDATE 匹配不到任何行，
        // 变成"静默无操作"（比报错更坏：模型以为已交出，库里还挂着）。
        if (it.op == "lose" && iOwner <= 0) {
            return fail(RefFail("块 4 lose 的持有者", it.owner_id, it.owner_temp_id, rctx, kind::person));
        }
        if (it.op == "lose") {
            auto st = db.Prepare(
                "UPDATE entity_ownerships SET to_chapter=?1 WHERE owner_id=?2 AND item_id=?3 AND "
                "to_chapter=0");
            if (!st) {
                return fail(fmt::format("块 4 lose 准备失败：{}", st.error().message));
            }
            (void)st->BindInt(1, diff.chapter_id);
            (void)st->BindInt(2, iOwner);
            (void)st->BindInt(3, iItem);
            if (auto s = st->Step(); !s) {
                return fail(fmt::format("块 4 lose 失败：{}", s.error().message));
            }
            out.applied.push_back(fmt::format("ownerships: lose #{}↛#{}", iOwner, iItem));
            continue;
        }
        if (it.op != "acquire") {
            out.applied.push_back(fmt::format("ownerships: {} 不改表（move/change_state 无落地表）", it.op));
            continue;
        }
        // ★ S67b：**`acquire` 按"转移"落库** —— 先关闭该物品**其它**仍然有效的持有行。
        // 为什么：不变式 I5 是"**同一物品同时只能有一个持有者**"。模型只写 `acquire`（新持有者）
        // 而漏写旧持有者的 `lose` 时，库里就会出现两行 `to_chapter=0` ⇒ K07 报"多个持有者"，
        // 而且这行脏数据会**永久**留在库里（真跑实证：第 9 章这么写了一次，第 10 章就被 K07 挡死）。
        // ⚠️ 这**不掩盖**模型的错：K17 仍会以"物品 #X 已被 #Y 持有却 acquire"**可见地**拒掉那份 diff
        //    （本块只在 diff 已经过门禁后才执行）；这里只保证"已提交的世界状态始终满足 I5"。
        // 先数（`SqliteDb` 没暴露 `changes()`），再关 —— 顺序不能反
        int closed = 0;
        if (auto cnt = db.Prepare("SELECT COUNT(*) FROM entity_ownerships WHERE item_id=?1 AND "
                                  "to_chapter=0 AND owner_id<>?2");
            cnt) {
            (void)cnt->BindInt(1, iItem);
            (void)cnt->BindInt(2, iOwner);
            if (auto s = cnt->Step(); s && *s == db::sqlite::StepResult::Row) {
                closed = static_cast<int>(cnt->ColumnInt(0));
            }
        }
        if (closed > 0) {
            if (auto cl = db.Prepare("UPDATE entity_ownerships SET to_chapter=?1 WHERE item_id=?2 AND "
                                     "to_chapter=0 AND owner_id<>?3");
                cl) {
                (void)cl->BindInt(1, diff.chapter_id);
                (void)cl->BindInt(2, iItem);
                (void)cl->BindInt(3, iOwner);
                if (auto s = cl->Step(); !s) {
                    return fail(fmt::format("块 4 关闭旧持有失败：{}", s.error().message));
                }
            }
            out.applied.push_back(
                fmt::format("ownerships: 物品 #{} 的旧持有者已按转移关闭（{} 行）", iItem, closed));
        }
        // ⚠️ S69：`acquire` 的持有者**必须**解析到真实 id —— 本块原先**不检查**（块 5 的参与者就检查了），
        // 于是 `owner_id=0` 且没有 `owner_temp_id` 时会**直接写一行"无持有者的持有"**。
        // 🔴 真跑实证（第 12 章）：库里出现
        //   `entity_ownerships(owner_id=0, item_id=93, from_chapter=12, to_chapter=0, how='替身取出放在控制台')`
        //   —— 一行**活跃**持有、持有者却不存在。它是被 S69 的一致性扫描（`R4 ownership_owner_missing`）
        //   当场抓出来的（`--novel-repair` 第一条就点了它）。契约层 `CheckRef` 只把 `items[].item_id`
        //   标成 required、没管 owner ⇒ 必须在落库前挡一道。
        // 修法是**拒提交**而不是静默跳过：模型漏写持有者是契约错，要让它可见地重做
        //（与块 5「参与者解析不到就 fail」同口径）。
        if (iOwner <= 0) {
            return fail(fmt::format("块 4 `acquire`：{} —— 持有必须有持有者，不许写 owner=0",
                                    RefFail("持有者", it.owner_id, it.owner_temp_id, rctx,
                                            kind::person)));
        }
        OwnershipRow row;
        row.owner_id = iOwner;
        row.item_id = iItem;
        row.from_chapter = diff.chapter_id;
        row.how = it.how;
        row.note = it.reason;
        if (auto id = graph.UpsertOwnership(row); !id) {
            return fail(fmt::format("块 4 ownerships 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("ownerships: acquire #{}←#{}", iItem, iOwner));
    }

    // 块 5：event_details + event_participants
    for (const EventDelta& ev : diff.events) {
        EntityRow entity;
        entity.kind = "event";
        entity.name = ev.action.empty() ? fmt::format("ch{} 事件", diff.chapter_id) : ev.action;
        entity.summary = ev.result;
        entity.created_chapter = diff.chapter_id;
        EventDetailRow detail;
        detail.time_label = ev.time_label;
        // S87：地点可以是本章新建的 / 可以是**名字**（kind=location，且**不许静默留 0**）
        detail.location_id = ResolveRef(ev.location_id, ev.location_temp_id, rctx, kind::location);
        if (detail.location_id <= 0 && (ev.location_id > 0 || !ev.location_temp_id.empty())) {
            return fail(RefFail("块 5 事件地点", ev.location_id, ev.location_temp_id, rctx,
                                kind::location));
        }
        detail.cause_note = ev.cause;
        detail.result_note = ev.result;
        auto id = graph.UpsertEvent(entity, detail);
        if (!id) {
            return fail(fmt::format("块 5 事件失败：{}", id.error().message));
        }
        if (!ev.temp_id.empty()) {
            tempIds[ev.temp_id] = *id;
        }
        for (const EventParticipantDelta& p : ev.participants) {
            // S87：参与者可以是本章新建的角色 / 名字（kind 不限：`faction_actor` 可能是组织）
            const RowId pEid = ResolveRef(p.entity_id, p.entity_temp_id, rctx, {});
            if (pEid <= 0) {
                return fail(RefFail("块 5 参与者", p.entity_id, p.entity_temp_id, rctx, {}));
            }
            if (auto added = graph.UpsertEventParticipant({.event_id = *id, .entity_id = pEid,
                                                          .role = p.role});
                !added) {
                return fail(fmt::format("块 5 参与者失败：{}", added.error().message));
            }
        }
        out.applied.push_back(fmt::format("events: +1（{} → #{}，{} 名参与者）", ev.temp_id, *id,
                                          ev.participants.size()));
    }

    // 块 6：causal_links（TempId → 真实 id）
    for (const CausalDelta& cd : diff.causal) {
        const RowId cause = ResolveTempo(cd.cause, tempIds);
        const RowId effect = ResolveTempo(cd.effect, tempIds);
        if (cause <= 0 || effect <= 0) {
            return fail(fmt::format("块 6 因果链的端点解析不到真实 id（cause={} effect={}）", cause, effect));
        }
        if (auto id = graph.UpsertCausalLink({.cause_event_id = cause,
                                             .effect_event_id = effect,
                                             .link_type = cd.link_type,
                                             .note = cd.note});
            !id) {
            return fail(fmt::format("块 6 因果失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("causal: #{}→#{}（{}）", cause, effect, cd.link_type));
    }

    // 块 7：foreshadowings
    for (const ForeshadowDelta& f : diff.foreshadows) {
        ForeshadowRow row;
        row.id = f.foreshadow_id;
        row.title = f.title;
        row.content = f.content;
        row.status = f.status;
        row.setup_ch = f.setup_ch;
        row.payoff_ch = f.payoff_ch;
        row.importance = f.importance;
        row.truth = f.truth;
        if (auto id = graph.UpsertForeshadow(row); !id) {
            return fail(fmt::format("块 7 伏笔失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("foreshadows: {} #{} → {}", f.op, f.foreshadow_id, f.status));
    }

    // 块 8：mysteries + mystery_beats
    for (const MysteryDelta& m : diff.mysteries) {
        MysteryRow row;
        row.id = m.mystery_id;
        row.question = m.question;
        row.answer = m.answer;
        row.status = m.status;
        row.ask_ch = diff.chapter_id;
        auto id = graph.UpsertMystery(row);
        if (!id) {
            return fail(fmt::format("块 8 谜团失败：{}", id.error().message));
        }
        if (!m.beat.content.empty() || !m.beat.beat_type.empty()) {
            // 同块 9：节拍序在被父下编号（原先写死 0，属同一处缺陷）
            RowId nextOrd = 1;
            if (auto st = db.Prepare("SELECT COALESCE(MAX(ord),0)+1 FROM mystery_beats "
                                     "WHERE mystery_id=?1");
                st) {
                (void)st->BindInt(1, *id);
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    nextOrd = st->ColumnInt(0);
                }
            }
            if (auto beat = graph.UpsertMysteryBeat({.mystery_id = *id,
                                                    .beat_type = m.beat.beat_type,
                                                    .chapter_id = diff.chapter_id,
                                                    .content = m.beat.content,
                                                    .target_entity_id = m.beat.target_entity_id,
                                                    .ord = static_cast<int>(nextOrd)});
                !beat) {
                return fail(fmt::format("块 8 谜团节拍失败：{}", beat.error().message));
            }
        }
        out.applied.push_back(fmt::format("mysteries: #{}（{}）", *id, m.status));
    }

    // 块 9：plots + plot_beats
    for (const PlotDelta& pd : diff.plotlines) {
        PlotRow row;
        row.id = pd.plot_id;
        row.kind = pd.kind;
        row.title = pd.title;
        row.status = pd.status;
        row.intro_ch = diff.chapter_id;
        auto id = graph.UpsertPlot(row);
        if (!id) {
            return fail(fmt::format("块 9 剧情线失败：{}", id.error().message));
        }
        if (!pd.beat.title.empty() || !pd.beat.summary.empty()) {
            // `ord` 必须**父下严格递增**（`06` §2.3 K08）：原先写死 0，同一剧情线的多个节拍
            // 会全部挤在 ord=0 → K08 判不严格递增。节拍序由本函数编号（`PlotBeatDelta` 本身没有 ord）。
            RowId nextOrd = 1;
            if (auto st = db.Prepare("SELECT COALESCE(MAX(ord),0)+1 FROM plot_beats WHERE plot_id=?1");
                st) {
                (void)st->BindInt(1, *id);
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    nextOrd = st->ColumnInt(0);
                }
            }
            if (auto beat = graph.UpsertPlotBeat({.plot_id = *id,
                                                 .chapter_id = diff.chapter_id,
                                                 .ord = static_cast<int>(nextOrd),
                                                 .beat_type = pd.beat.beat_type,
                                                 .title = pd.beat.title,
                                                 .summary = pd.beat.summary});
                !beat) {
                return fail(fmt::format("块 9 剧情线节拍失败：{}", beat.error().message));
            }
        }
        out.applied.push_back(fmt::format("plots: #{}（{}）", *id, pd.status));
    }

    // 块 10：character_knowledge
    for (const KnowledgeDelta& k : diff.knowledge) {
        // S87：知情者可以是本章新建的角色 / 名字（期望 kind=person）
        const RowId kEid = ResolveRef(k.entity_id, k.entity_temp_id, rctx, kind::person);
        if (kEid <= 0) {
            return fail(RefFail("块 10 知情者", k.entity_id, k.entity_temp_id, rctx, kind::person));
        }
        if (auto id = graph.UpsertKnowledge({.entity_id = kEid,
                                            .fact_kind = k.fact_kind,
                                            .fact_id = k.fact_id,
                                            .fact_text = k.fact_text,
                                            .knows = k.knows ? 1 : 0,
                                            .chapter_known = k.chapter_known > 0 ? k.chapter_known
                                                                                 : diff.chapter_id});
            !id) {
            return fail(fmt::format("块 10 知情失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("knowledge: 实体 #{} {}={}", k.entity_id, k.fact_kind,
                                          k.knows ? "知道" : "不知道"));
    }

    // 块 11：chapters（**本章已完成**的标志 → 放最后一批写）
    if (!ctx.chapter_body.empty()) {
        ChapterRow row;
        if (auto cur = graph.GetChapter(diff.chapter_id); cur) {
            row = *cur;
        }
        row.id = diff.chapter_id;
        row.body = ctx.chapter_body;
        row.words = ctx.chapter_words > 0 ? ctx.chapter_words
                                          : static_cast<int>(ctx.chapter_body.size());
        row.summary = ctx.chapter_summary;
        row.status = "done";
        if (auto id = graph.UpsertChapter(row); !id) {
            return fail(fmt::format("块 11 章节落盘失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("chapters: 正文 {} 字 + status=done", row.words));
    } else {
        out.applied.push_back("chapters: 跳过（本次没带正文）");
    }

    // 块 12：entity_versions —— **已在块 0 采过 Before 值**（见上），这里只补记账
    out.applied.push_back(fmt::format("entity_versions: {} 条（提交前采集）",
                                      out.entity_version_ids.size()));

    // 块 13：canon_logs（manual → PROPOSED；auto → CANON，门禁已在上面判定，S9）
    const std::string_view canonStatus = autoCanon ? std::string_view{"CANON"}
                                                   : std::string_view{"PROPOSED"};
    if (auto canon = graph.SetCanon("chapter", diff.chapter_id, canonStatus,
                                    fmt::format("diff_hash={} verdict={}", out.diff_hash,
                                                ctx.review_verdict));
        !canon) {
        return fail(fmt::format("块 13 canon_logs 失败：{}", canon.error().message));
    }
    out.applied.push_back(fmt::format("canon_logs: chapter → {}（{}）", canonStatus, canonMode));

    // 块 14：audit_logs（汇总一条；`detail` 内含 diff_hash 供幂等判定）
    if (auto audit = graph.LogAudit(
            "agent", "commit_chapter", "chapter", diff.chapter_id,
            fmt::format("diff_hash={} {}", out.diff_hash, diff.Summary()));
        !audit) {
        return fail(fmt::format("块 14 audit_logs 失败：{}", audit.error().message));
    }
    out.applied.push_back("audit_logs: commit_chapter +1");

    if (auto commit = db.Exec("COMMIT"); !commit) {
        (void)db.Exec("ROLLBACK");
        out.ok = false;
        out.error = fmt::format("COMMIT 失败：{}", commit.error().message);
        return out;
    }
    out.ok = true;
    log::Info("章节 {} 状态回写提交成功：{}（diff_hash={}）", diff.chapter_id, diff.Summary(),
              out.diff_hash);
    return out;
}

// ———— 自检 ————

int RunCommitSelfCheck() {
    int fail = 0;
    const auto expect = [&](bool cond, std::string_view name) {
        if (cond) {
            log::Info("S8 commit PASS {}", name);
        } else {
            ++fail;
            log::Error("S8 commit FAIL {}", name);
        }
    };

    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("S8 commit FAIL 内存库打开失败");
        return fail + 1;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("S8 commit FAIL 建表失败 {}", r.error().message);
        return fail + 1;
    }

    const std::filesystem::path snapRoot =
        std::filesystem::temp_directory_path() / "shine_s8_commit_check";
    std::error_code ec;
    std::filesystem::remove_all(snapRoot, ec);

    NovelGraph g(mem);
    auto lin = g.UpsertEntity({.kind = std::string{kind::person}, .name = "林默"});
    auto foe = g.UpsertEntity({.kind = std::string{kind::person}, .name = "裴照"});
    auto loc = g.UpsertEntity({.kind = std::string{kind::location}, .name = "黑森林"});
    auto ring = g.UpsertEntity({.kind = std::string{kind::item}, .name = "黑戒指"});
    auto chapter = g.UpsertChapter({.ord = 1, .title = "雨夜", .pov_entity_id = lin.value_or(0)});
    if (!lin || !foe || !loc || !ring || !chapter) {
        log::Error("S8 commit FAIL 前置数据失败");
        return fail + 1;
    }

    // 一份「有实质变化」的 StateDiff
    StateDiff diff;
    diff.chapter_id = *chapter;
    diff.input_state_hash = "sha1:s8selfcheck";
    diff.entities.push_back({.temp_id = "en:密使",
                             .kind = "person",
                             .name = "北境密使",
                             .summary = "入城联络的密探",
                             .status = "active",
                             .created_chapter = *chapter});
    diff.characters.push_back({.entity_id = *lin,
                               .location_id = *loc,
                               .body_state = "右手旧伤复发",
                               .mind_state = "高度警觉",
                               .emotion_json = R"({"fear":70,"trust":20})",
                               .goal = "查明密使身份",
                               .relation_note = "对裴照保持戒心",
                               .resource_note = "持有笔迹抄本",
                               .secret_note = "卧底身份未暴露",
                               .reason = "第 1 章夜间潜入军械库"});
    diff.relationships.push_back({.op = "upsert",
                                  .from_id = *lin,
                                  .to_id = *foe,
                                  .rel_type = "rival",
                                  .strength = 30,
                                  .reason = "目击与被目击"});
    diff.items.push_back({.op = "acquire",
                          .item_id = *ring,
                          .owner_id = *lin,
                          .how = "抄录",
                          .reason = "取证"});
    diff.events.push_back({.temp_id = "ev:1",
                           .cause = "第三笔无主支出",
                           .participants = {{.entity_id = *lin, .role = "actor"}},
                           .location_id = *loc,
                           .time_label = "昭元三年·冬·第三夜",
                           .action = "认定笔迹归属",
                           .result = "取得抄本"});
    diff.causal.push_back({.cause = {.temp_id = "ev:1"},
                           .effect = {.entity_id = *lin},
                           .link_type = "reveals",
                           .note = "自检：TempId 解析用"});
    diff.foreshadows.push_back({.op = "progress",
                                .foreshadow_id = 0,
                                .title = "无主支出",
                                .content = "第三笔支出经手人",
                                .truth = "北境密使",
                                .status = "DEVELOPING",
                                .setup_ch = 1,
                                .payoff_ch = 40,
                                .importance = 80});
    diff.mysteries.push_back({.mystery_id = 0,
                              .question = "谁在挪用军费",
                              .answer = "北境密使",
                              .status = "hinted",
                              .beat = {.beat_type = "hint", .content = "账目第三笔对不上"}});
    diff.plotlines.push_back({.plot_id = 0,
                              .kind = "main",
                              .title = "追查密使",
                              .status = "active",
                              .beat = {.beat_type = "setup", .title = "发现异常", .summary = "账目缺口"}});
    diff.knowledge.push_back({.entity_id = *lin,
                              .fact_kind = "secret",
                              .fact_id = 5,
                              .fact_text = "北境密使已入城",
                              .knows = true,
                              .chapter_known = *chapter});

    CommitContext ctx;
    ctx.chapter_id = *chapter;
    ctx.review_verdict = "PASS";
    ctx.snapshot_dir = util::PathToUtf8(snapRoot);
    ctx.chapter_body = "雪原上只剩下风声。";
    ctx.chapter_summary = "林默在雨夜潜入军械库。";

    // ① 门禁：G1 不满足 → **拒绝提交**，且库里一行都不该有
    {
        CommitContext bad = ctx;
        bad.review_pass = false;
        const CommitResult r = CommitChapterState(mem, diff, bad);
        auto statusCount = mem.Prepare("SELECT COUNT(*) FROM character_status");
        int rows = -1;
        if (statusCount && statusCount->Step()) {
            rows = static_cast<int>(statusCount->ColumnInt(0));
        }
        expect(!r.ok && !r.gates.g1_review_pass && rows == 0,
               "G1 未过 → 拒绝提交且不写库（character_status 仍为 0 行）");
    }
    // ② 门禁：没给 snapshot_dir → G3 不过（不变式 I11）
    {
        CommitContext noSnap = ctx;
        noSnap.review_pass = true;
        noSnap.snapshot_dir.clear();
        const CommitResult r = CommitChapterState(mem, diff, noSnap);
        expect(!r.ok && !r.gates.g3_snapshot_ready, "缺快照 → 拒绝提交（G3）");
    }
    // ③ 正常提交：14 块都写到
    CommitResult first;
    {
        CommitContext ok = ctx;
        ok.review_pass = true;
        ok.project_dir = util::PathToUtf8(snapRoot); // S12：让 StateDiff 产物有地方落
        first = CommitChapterState(mem, diff, ok);
        expect(first.ok && !first.skipped, "门禁齐备 → 提交成功");
        expect(first.gates.g1_review_pass && first.gates.g2_checks_pass &&
                   first.gates.g3_snapshot_ready && first.gates.g4_diff_valid &&
                   first.gates.g5_no_high_issue,
               "G1–G5 逐条为真");
        expect(!first.entity_version_ids.empty(), "提交前写了实体级快照（Before 值）");
        // S12（`07` §2.1 / 不变式 I10）：本章 StateDiff 必须落成 work/ch<NNN>/12_state_diff.json
        const auto diffPath = snapRoot / "work" / "ch001" / "12_state_diff.json";
        const auto bytes = util::ReadFileBytes(diffPath);
        StateDiff reread;
        expect(bytes && StateDiffFromJson(*bytes, reread) && reread.Hash() == diff.Hash(),
               "S12：work/ch001/12_state_diff.json 落盘且读回一致（不变式 I10）");
    }
    // ④ 世界状态**确有变化**（这是本 S 的核心判据）
    {
        auto status = g.GetLatestCharacterStatus(*lin, *chapter);
        auto fs = g.ListOpenForeshadows();
        auto rels = g.GetRelations(*lin);
        auto owns = g.GetOwnerships(*lin);
        auto know = g.CharacterKnows(*lin, "secret", 5, *chapter);
        bool hasEvent = false;
        if (auto st = mem.Prepare("SELECT COUNT(*) FROM event_participants WHERE entity_id=?1");
            st) {
            (void)st->BindInt(1, *lin);
            if (st->Step()) {
                hasEvent = st->ColumnInt(0) > 0;
            }
        }
        bool hasCanon = false;
        if (auto st = mem.Prepare(
                "SELECT COUNT(*) FROM canon_logs WHERE target_kind='chapter' AND target_id=?1 "
                "AND status='PROPOSED'");
            st) {
            (void)st->BindInt(1, *chapter);
            if (st->Step()) {
                hasCanon = st->ColumnInt(0) > 0;
            }
        }
        expect(status && status->body_state == "右手旧伤复发" && status->location_id == *loc,
               "character_status 有本章新行");
        expect(fs && fs->size() == 1 && (*fs)[0].status == "DEVELOPING", "foreshadowings 新增且状态前进");
        expect(rels && !rels->empty(), "relations 写入");
        expect(owns && !owns->empty(), "entity_ownerships 写入");
        expect(know && *know, "character_knowledge 写入且按章可判");
        expect(hasEvent, "event_participants 写入");
        expect(hasCanon, "canon_logs 写 PROPOSED（manual，不自动升 CANON）");
        auto chRow = g.GetChapter(*chapter);
        expect(chRow && chRow->status == "done" && !chRow->body.empty(), "chapters 落正文 + status=done");
    }
    // ⑤ 章级快照文件落盘（不变式 I11）
    {
        const auto path = std::filesystem::path{util::PathFromUtf8(first.snapshot_path)};
        const auto bytes = util::ReadFileBytes(path);
        const std::string text = bytes ? *bytes : std::string{};
        expect(!first.snapshot_path.empty() && std::filesystem::exists(path) &&
                   text.find(first.diff_hash) != std::string::npos &&
                   text.find("\"diff\"") != std::string::npos &&
                   text.find("entity_version_ids") != std::string::npos,
               "章级快照 ch001.json 落盘且含 diff_hash / diff / 快照 id");
    }
    // ⑥ 幂等：同一份 diff 再提交 → 跳过，且**行数不变**
    {
        const auto countOf = [&mem](std::string_view table) {
            auto st = mem.Prepare(fmt::format("SELECT COUNT(*) FROM {}", table));
            if (st && st->Step()) {
                return st->ColumnInt(0);
            }
            return static_cast<RowId>(-1);
        };
        const RowId statusBefore = countOf("character_status");
        const RowId auditBefore = countOf("audit_logs");
        const RowId versionsBefore = countOf("entity_versions");
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult second = CommitChapterState(mem, diff, ok);
        expect(second.ok && second.skipped, "重复提交 → 幂等跳过");
        expect(countOf("character_status") == statusBefore && countOf("audit_logs") == auditBefore &&
                   countOf("entity_versions") == versionsBefore,
               "幂等：character_status / audit_logs / entity_versions 都不增加");
    }
    // ⑦ D 规则：no_change_declared 与死人不声明 → 拒绝
    {
        StateDiff contradictory;
        contradictory.chapter_id = *chapter;
        contradictory.no_change_declared = true;
        contradictory.characters.push_back({.entity_id = *lin, .reason = "x"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, contradictory, ok);
        expect(!r.ok && !r.gates.g4_diff_valid, "D7：声明无变化却带 delta → 拒绝");
    }
    {
        StateDiff death;
        death.chapter_id = *chapter;
        death.characters.push_back({.entity_id = *foe, .body_state = "dead", .reason = "被杀"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, death, ok);
        expect(!r.ok, "D4：死人没在 entities 里声明 → 拒绝");
    }
    {
        StateDiff badKind;
        badKind.chapter_id = *chapter;
        badKind.entities.push_back({.temp_id = "en:魂环", .kind = "魂环", .name = "千年魂环"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, badKind, ok);
        expect(!r.ok, "D8：kind 不在 31 种内 → 拒绝");
    }
    // ⑧ `auto`（S9，`07` §2.5）：门禁全满足 → 提交并写 CANON；门禁不满足 → 拒绝（C5 不跳门禁）
    {
        auto autoChapter = g.UpsertChapter({.ord = 99, .title = "第九九章"});
        expect(autoChapter.has_value(), "auto：建章");
        StateDiff autoDiff;
        autoDiff.chapter_id = autoChapter.value_or(0);
        autoDiff.input_state_hash = "sha1:auto-check";
        autoDiff.characters.push_back(
            {.entity_id = *lin, .mind_state = "决意", .reason = "auto 自检"});
        CommitContext autoMode = ctx;
        autoMode.chapter_id = autoChapter.value_or(0);
        autoMode.review_pass = true;
        autoMode.canon_mode = "auto";
        const CommitResult r = CommitChapterState(mem, autoDiff, autoMode);
        // 失败时把原因带上（诊断用；`checks_describe` 会指名道姓是哪条 K）
        expect(r.ok && !r.skipped,
               fmt::format("auto：G1–G5 全满足 → 提交成功（{}｜{}）", r.error, r.checks_describe));
        int canon = 0;
        if (auto st = mem.Prepare("SELECT COUNT(*) FROM canon_logs WHERE target_kind='chapter' "
                                  "AND target_id=?1 AND status='CANON'");
            st) {
            (void)st->BindInt(1, autoChapter.value_or(0));
            if (st->Step()) {
                canon = st->ColumnInt(0);
            }
        }
        expect(canon == 1, "auto：canon_logs 写 CANON（不是 PROPOSED）");
        // 门禁不满足（G1 评审 FAIL）→ 拒绝，不静默降级为 manual
        auto gateChapter = g.UpsertChapter({.ord = 100, .title = "第一百章"});
        expect(gateChapter.has_value(), "auto：建第二张章");
        StateDiff d2;
        d2.chapter_id = gateChapter.value_or(0);
        d2.characters.push_back({.entity_id = *lin, .mind_state = "犹豫", .reason = "gate 自检"});
        CommitContext bad = ctx;
        bad.chapter_id = gateChapter.value_or(0);
        bad.review_pass = false;
        bad.canon_mode = "auto";
        const CommitResult r2 = CommitChapterState(mem, d2, bad);
        expect(!r2.ok && !r2.gates.g1_review_pass, "auto：G1 不满足 → 拒绝（不静默降级）");
    }
    // ⑧b ★ S65/S67：**"引用本章新建实体"必须能表达**（`*_temp_id`）—— 本条**跑在全新库上**
    //（`mem` 是自检现建的库，没有任何历史行/没有我手工修补过的数据）⇒ 它就是
    //"**换一本新书会不会失效**"的机器断言：只要它绿，机制对任何工程都成立。
    // 背景（真跑三轮实证）：模型想写"新人物在新地点卷入事件、新物品被谁持有"，而 `*_id` 字段
    // 只收**已存在** id ⇒ 它只好把 `en:7` 的**序号 7** 当 id 填（撞上无关实体）⇒ K03 挡下、改不动。
    {
        StateDiff td;
        td.chapter_id = *chapter;
        // ⚠️ S77：TempId 一律用**语义标签**（`en:1` 这种纯数字已被 D9 拒 —— 见 `ValidateStateDiff`）
        td.entities.push_back(
            {.temp_id = "en:新地点", .kind = "location", .name = "S65新地点", .created_chapter = *chapter});
        td.entities.push_back(
            {.temp_id = "en:新人物", .kind = "person", .name = "S65新人物", .created_chapter = *chapter});
        td.entities.push_back(
            {.temp_id = "en:新物品", .kind = "item", .name = "S65新物品", .created_chapter = *chapter});
        EventDelta ev;
        ev.temp_id = "ev:101";
        ev.action = "S65事件";
        ev.location_temp_id = "en:新地点";
        ev.location_id = 0;
        ev.participants.push_back({.entity_id = 0, .entity_temp_id = "en:新人物", .role = "actor"});
        td.events.push_back(ev);
        td.items.push_back({.op = "acquire",
                            .item_temp_id = "en:新物品",
                            .owner_temp_id = "en:新人物",
                            .how = "S65 自检"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        ok.canon_mode = "manual";
        const CommitResult r = CommitChapterState(mem, td, ok);
        expect(r.ok, fmt::format("S65：`*_temp_id` 引用本章新建实体 → 全新库提交成功（{}）", r.error));
        RowId physLoc = 0;
        RowId physPerson = 0;
        RowId physItem = 0;
        if (auto st = mem.Prepare("SELECT id,kind FROM entities WHERE name LIKE 'S65%'"); st) {
            while (true) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                const std::string k = st->ColumnText(1);
                if (k == "location") physLoc = st->ColumnInt(0);
                if (k == "person") physPerson = st->ColumnInt(0);
                if (k == "item") physItem = st->ColumnInt(0);
            }
        }
        expect(physLoc > 0 && physPerson > 0 && physItem > 0, "S65：三个新建实体都已落库");
        const auto countWhere = [&mem](const char* sql, RowId a, RowId b) {
            auto st = mem.Prepare(sql);
            if (!st) {
                return -1;
            }
            (void)st->BindInt(1, a);
            if (b > 0) {
                (void)st->BindInt(2, b);
            }
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                return static_cast<int>(st->ColumnInt(0));
            }
            return -1;
        };
        expect(countWhere("SELECT COUNT(*) FROM event_participants WHERE entity_id=?1", physPerson, 0) ==
                   1,
               "S65：参与者 `entity_temp_id` 解析成**真实 id**（不是 0、不是别人）");
        expect(countWhere("SELECT COUNT(*) FROM event_details WHERE location_id=?1", physLoc, 0) == 1,
               "S65：`location_temp_id` 解析成**真实 id**");
        expect(countWhere("SELECT COUNT(*) FROM entity_ownerships WHERE item_id=?1 AND owner_id=?2 "
                          "AND to_chapter=0",
                          physItem, physPerson) == 1,
               "S65：`item_temp_id`/`owner_temp_id` 解析成真实 id，且 I5（唯一持有者）成立");
        // ⑧c S69：`acquire` 给不出持有者 → **拒提交**（不许落"无持有者的持有"。
        // 真跑实证：第 12 章就是这么写出一行 `owner_id=0` 的**活跃**持有，被一致性扫描抓到。）
        {
            StateDiff bad;
            bad.chapter_id = *chapter;
            bad.items.push_back({.op = "acquire", .item_id = physItem, .owner_id = 0,
                                 .how = "S69 自检：无持有者"});
            CommitContext c2 = ctx;
            c2.review_pass = true;
            const CommitResult r2 = CommitChapterState(mem, bad, c2);
            expect(!r2.ok, "S69：`acquire` 无持有者 → 拒提交（不被静默写成 owner=0）");
        }
        // ⑧d ★ S77：**`temp_id` 写"前缀+纯数字" ⇒ 契约当场拒**（D9 —— 把歧义源消灭在入口）
        {
            StateDiff numTid;
            numTid.chapter_id = *chapter;
            numTid.entities.push_back({.temp_id = "en:1", .kind = "item", .name = "数字标签物品",
                                       .created_chapter = *chapter});
            CommitContext c3 = ctx;
            c3.review_pass = true;
            const CommitResult r3 = CommitChapterState(mem, numTid, c3);
            bool hasD9 = false;
            for (const CommitIssue& is : r3.gates.issues) {
                if (is.detail.find("D9") != std::string::npos) {
                    hasD9 = true;
                }
            }
            expect(!r3.ok && hasD9, "S77：`temp_id` 用纯数字（`en:1`）→ 契约拒（D9），不许进库");
        }
        // ⑧e ★ S79：**因果自环 ⇒ 契约拒**（D10）—— 原先只在**落库时**（`UpsertCausalLink`，块 6 内）
        // 挡下，报文是"块 6 因果失败"，模型看不到该改什么（第 18 章连着两轮卡这条）。
        {
            StateDiff loop;
            loop.chapter_id = *chapter;
            loop.causal.push_back(
                {.cause = {.temp_id = "ev:自环"}, .effect = {.temp_id = "ev:自环"},
                 .link_type = "causes"});
            CommitContext c4 = ctx;
            c4.review_pass = true;
            const CommitResult r4 = CommitChapterState(mem, loop, c4);
            bool hasD10 = false;
            for (const CommitIssue& is : r4.gates.issues) {
                if (is.detail.find("D10") != std::string::npos) {
                    hasD10 = true;
                }
            }
            expect(!r4.ok && hasD10, "S79：`causal[]` 自环 → 契约拒（D10），不许进库");
        }
        // ⑧f ★ S87（T1）：**名字就是引用键** —— 决策树四类结果各断一条。
        // 这里**刻意一个数字 id 都不写**：模型应当只说"我指的是谁 / 我要新建谁"。
        {
            const auto count1 = [&mem](const char* sql) {
                auto st = mem.Prepare(sql);
                if (!st) {
                    return -1;
                }
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    return static_cast<int>(st->ColumnInt(0));
                }
                return -1;
            };
            CommitContext c87 = ctx;
            c87.review_pass = true;
            c87.canon_mode = "manual";

            // (a) 唯一命中 ⇒ **复用**（不新建），并解析成真实 id
            StateDiff byName;
            byName.chapter_id = *chapter;
            CharacterDelta byNameCh;
            byNameCh.entity_temp_id = "S65新人物"; // ← 名字（不是 id、也不是本章 temp_id）
            byNameCh.location_temp_id = "S65新地点";
            byName.characters.push_back(byNameCh);
            const CommitResult r87a = CommitChapterState(mem, byName, c87);
            expect(r87a.ok, fmt::format("S87：名字引用（唯一命中）⇒ 提交成功（{}）", r87a.error));
            expect(count1("SELECT COUNT(*) FROM entities WHERE name='S65新人物'") == 1,
                   "S87：名字命中 ⇒ **复用**（没有新建出第二个同名实体）");
            expect(count1(fmt::format("SELECT COUNT(*) FROM character_status WHERE entity_id={} AND "
                                      "chapter_id={}",
                                      physPerson, *chapter)
                              .c_str()) == 1,
                   "S87：名字「S65新人物」解析成 **真实 id**（写进了 character_status）");

            // (b) 同名同 kind **两个** ⇒ **拒 + 候选列表**（系统不替作者猜"是哪一个"）
            RowId dupPerson = 0;
            if (auto ins = mem.Prepare("INSERT INTO entities(kind,name,summary,status,meta_json,"
                                       "created_chapter,updated) VALUES('person','S65新人物','重复声明',"
                                       "'active','{}',?1,0)");
                ins) {
                (void)ins->BindInt(1, *chapter);
                if (auto s = ins->Step(); s) {
                    dupPerson = mem.LastInsertRowId();
                }
            }
            expect(dupPerson > 0, "S87：造一个「同名同 kind」的重复实体（模拟「两个李默」）");
            // ⚠️ 必须换一份**内容不同**的 diff：I1 幂等（同章 + 同 diff 哈希）会在门禁**之前**直接
            // `ok+skipped` 返回 ⇒ 那样根本断不到"歧义被拒"（这个坑自检当场抓到了一次）。
            StateDiff amb = byName;
            amb.characters[0].body_state = "S87-b";
            const CommitResult r87b = CommitChapterState(mem, amb, c87);
            expect(!r87b.ok && r87b.error.find(fmt::format("#{}", physPerson)) != std::string::npos &&
                       r87b.error.find(fmt::format("#{}", dupPerson)) != std::string::npos,
                   fmt::format("S87：同名两个 ⇒ **拒**且**列出两个候选**（{}）", r87b.error));

            // (c) 库里没有这个名字 ⇒ 拒，并在报文里给出"**要新建**就走 entities[]"
            StateDiff miss;
            miss.chapter_id = *chapter;
            CharacterDelta missCh;
            missCh.entity_temp_id = "S87库里没有的名字";
            miss.characters.push_back(missCh);
            const CommitResult r87c = CommitChapterState(mem, miss, c87);
            expect(!r87c.ok && r87c.error.find("entities[]") != std::string::npos,
                   fmt::format("S87：未命中 ⇒ 拒，且报文指出「新建请进 entities[]」（{}）", r87c.error));

            // (d) 名字对了但 **kind 不符** ⇒ 拒（否则会写进错误的列）
            StateDiff wrongKind;
            wrongKind.chapter_id = *chapter;
            ItemDelta wrongItem;
            wrongItem.op = "acquire";
            wrongItem.item_temp_id = "S65新地点"; // ← 库内是 location，此处期望 item
            wrongItem.owner_temp_id = "S65新人物";
            wrongKind.items.push_back(wrongItem);
            const CommitResult r87d = CommitChapterState(mem, wrongKind, c87);
            expect(!r87d.ok && r87d.error.find("kind") != std::string::npos,
                   fmt::format("S87：名字对了但 kind 不符 ⇒ 拒（{}）", r87d.error));

            // (e) ★ T3b：`force_new` —— **从 JSON 反射进来也要生效**（模型走的就是这条路：
            // JSON → `util::reflect`（C++26 静态反射，按成员名映射）→ `NewEntityDelta::force_new`）。
            // 只测 `UpsertEntity` 是不够的：那样"字段加了但反射没读到"的错会漏网（一律变 false ⇒ 又去复用）。
            const std::string js = fmt::format(
                R"({{"contract_version":1,"producer":"extractor","chapter_id":{},"entities":[)"
                R"({{"temp_id":"另一个S65新人物","kind":"person","name":"S65新人物",)"
                R"("summary":"另一个同名的人","force_new":true}}]}})",
                *chapter);
            StateDiff forceNew;
            expect(StateDiffFromJson(js, forceNew) && forceNew.entities.size() == 1 &&
                       forceNew.entities[0].force_new,
                   "T3b：`force_new` 能从 **JSON 反射**进来（读成 true）");
            CommitContext c5 = ctx;
            c5.review_pass = true;
            c5.canon_mode = "manual";
            const CommitResult r87e = CommitChapterState(mem, forceNew, c5);
            expect(r87e.ok, fmt::format("T3b：带 `force_new` 的提交成功（{}）", r87e.error));
            expect(count1("SELECT COUNT(*) FROM entities WHERE name='S65新人物'") == 3,
                   "T3b：显式新建 ⇒ 同名实体**又多了 1 个**（库里共 3 个「S65新人物」）");
            expect(count1("SELECT COUNT(*) FROM audit_logs WHERE action='force_new_entity'") >= 1,
                   "T3b：显式新建留下 `audit_logs(force_new_entity)`（半年后能查清这个 id 的来历）");

            // ———— ★ T4：引用契约 ⑧f-1..9（union 候选 / D12 四态 / D9 保留形状 / force_new 与唯一性）————
            // ⚠️ 全部用**新名字**：`S65新人物` / `S65新地点` 此时库里已有多份（上面 (b)(e) 故意造的），
            //    用它们会把"本组要测的东西"和被造出来的重复混在一起。
            const auto idOf = [&mem](const char* name) -> RowId {
                auto st = mem.Prepare("SELECT id FROM entities WHERE name=?1 ORDER BY id LIMIT 1");
                if (!st) {
                    return 0;
                }
                (void)st->BindText(1, name);
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    return st->ColumnInt(0);
                }
                return 0;
            };
            CommitContext c4 = ctx;
            c4.review_pass = true;
            c4.canon_mode = "manual";

            StateDiff mk;
            mk.chapter_id = *chapter;
            mk.entities.push_back({.temp_id = "T4唯一甲标签", .kind = "person", .name = "T4唯一甲",
                                   .created_chapter = *chapter});
            const CommitResult rMk = CommitChapterState(mem, mk, c4);
            expect(rMk.ok, fmt::format("T4：先建一个唯一名实体（{}）", rMk.error));
            const RowId idUniq = idOf("T4唯一甲");
            expect(idUniq > 0, "T4：唯一名实体已落库");

            // ⑧f-8 ★ **union 按 entity id 去重**（用户指定）：本章声明的名字与库内命中**最终同一个 id**
            //    ⇒ 仍视为**唯一候选** ⇒ 成功。（反例：若实现写成"两个来源=两个候选"，这里必红。）
            {
                StateDiff u8;
                u8.chapter_id = *chapter;
                // 本章**重新声明**同名（无 `force_new` ⇒ `UpsertEntity` 复用同一个实体）
                u8.entities.push_back({.temp_id = "T4重声明标签", .kind = "person", .name = "T4唯一甲",
                                       .created_chapter = *chapter});
                u8.characters.push_back({.entity_temp_id = "T4唯一甲", .body_state = "在"});
                const CommitResult r = CommitChapterState(mem, u8, c4);
                expect(r.ok, fmt::format("T4 ⑧f-8：本章声明与库内指向**同一** id ⇒ union 去重后唯一 ⇒ 成功"
                                         "（{}）",
                                         r.error));
                expect(count1("SELECT COUNT(*) FROM entities WHERE name='T4唯一甲'") == 1,
                       "T4 ⑧f-8：也没有因此新建出第二份同名实体");
            }
            // ⑧f-4：`*_id` + `*_ref` **同实体** ⇒ **允许**（冗余但一致），canonical id == `*_id`
            {
                StateDiff u4;
                u4.chapter_id = *chapter;
                u4.characters.push_back(
                    {.entity_id = idUniq, .entity_temp_id = "T4唯一甲", .body_state = "在"});
                const CommitResult r = CommitChapterState(mem, u4, c4);
                expect(r.ok, fmt::format("T4 ⑧f-4：`*_id` + `*_ref` **同一实体** ⇒ 允许提交（{}）", r.error));
            }
            // ⑧f-5：`*_id` + `*_ref` **不同实体** ⇒ **D12 拒**
            {
                StateDiff u5;
                u5.chapter_id = *chapter;
                u5.characters.push_back(
                    {.entity_id = physPerson, .entity_temp_id = "T4唯一甲", .body_state = "在"});
                const CommitResult r = CommitChapterState(mem, u5, c4);
                expect(!r.ok && r.error.find("D12") != std::string::npos,
                       fmt::format("T4 ⑧f-5：`*_id` + `*_ref` **不同实体** ⇒ D12 拒（{}）", r.error));
            }
            // ⑧f-6：旧写法回归 —— `temp_id = en:语义标签` + 用该 temp_id 引用 ⇒ 仍通过
            {
                StateDiff u6;
                u6.chapter_id = *chapter;
                u6.entities.push_back({.temp_id = "en:T4旧写法", .kind = "person", .name = "T4旧写法人物",
                                       .created_chapter = *chapter});
                u6.characters.push_back({.entity_temp_id = "en:T4旧写法", .body_state = "在"});
                const CommitResult r = CommitChapterState(mem, u6, c4);
                expect(r.ok, fmt::format("T4 ⑧f-6：旧 `en:语义标签` 写法回归 ⇒ 仍通过（{}）", r.error));
            }
            // ⑧f-9（顺带）：**D9 扩到引用值** —— `*_ref` 写 `en:7` 这种保留形状 ⇒ 拒（报文说清"保留形状"）
            {
                StateDiff u9;
                u9.chapter_id = *chapter;
                u9.characters.push_back({.entity_temp_id = "en:7", .body_state = "在"});
                const CommitResult r = CommitChapterState(mem, u9, c4);
                expect(!r.ok && r.error.find("保留形状") != std::string::npos,
                       fmt::format("T4 ⑧f-9：引用值用 `en:7` 保留形状 ⇒ 拒（D9 扩引用值）（{}）", r.error));
            }
            // ⑧f-7：本章 `force_new` 建同名实体后，**再用该名字引用** ⇒ **拒**（不能"本章优先"悄悄赢）
            {
                StateDiff u7;
                u7.chapter_id = *chapter;
                u7.entities.push_back({.temp_id = "T4第三份标签", .kind = "person", .name = "T4唯一甲",
                                       .created_chapter = *chapter, .force_new = true});
                u7.characters.push_back({.entity_temp_id = "T4唯一甲", .body_state = "在"});
                const CommitResult r = CommitChapterState(mem, u7, c4);
                expect(!r.ok && r.error.find("不再是唯一引用键") != std::string::npos,
                       fmt::format("T4 ⑧f-7：本章 `force_new` 后**按名字引用** ⇒ 拒（{}）", r.error));
            }
            // ⑧f-10 ★ T4（**真跑第 21 章实证**）：`*_id` **有效** + 同时给了一个**错槽位**的 temp_id
            //   （字段期望 location，而那个 temp_id 是本章声明的 **event**）⇒ 那个 ref 对槽位
            //   **不构成"说法"** ⇒ **不按 D12 冲突处理**、也不拒，按 `*_id` 继续提交。
            //   ⚠️ 反例保护：若实现把它当冲突（第一版就是这样），模型会因为"多写了个错槽位字段"
            //      而**整章被拒** —— 为一处噪声废掉一整章的 LLM 输出。
            {
                StateDiff u10;
                u10.chapter_id = *chapter;
                u10.entities.push_back({.temp_id = "T4错槽位事件", .kind = "event",
                                        .name = "T4错槽位事件名", .created_chapter = *chapter});
                EventDelta ev10;
                ev10.temp_id = "T4错槽位事件标签";
                ev10.action = "T4 错槽位引用";
                ev10.location_id = physLoc;            // ← **有效**的 location
                ev10.location_temp_id = "T4错槽位事件"; // ← kind=event，**错槽位**
                ev10.participants.push_back({.entity_id = physPerson, .role = "actor"});
                u10.events.push_back(ev10);
                const CommitResult r = CommitChapterState(mem, u10, c4);
                expect(r.ok, fmt::format("T4 ⑧f-10：有效 `*_id` + **错槽位** temp_id ⇒ 不误判 D12、"
                                         "照 `*_id` 提交（{}）",
                                         r.error));
                expect(count1(fmt::format("SELECT COUNT(*) FROM event_details WHERE location_id={}",
                                          physLoc)
                                  .c_str()) >= 1,
                       "T4 ⑧f-10：事件地点落成 **physLoc**（错槽位的 ref 被忽略）");
            }

            // ⑧f-7b：**只声明、不被引用**的 `force_new` ⇒ 必须**放行**（T3b 验收标准 5 不能被 T4 破坏）
            {
                StateDiff u7b;
                u7b.chapter_id = *chapter;
                u7b.entities.push_back({.temp_id = "T4第四份标签", .kind = "person", .name = "T4唯一甲",
                                        .created_chapter = *chapter, .force_new = true});
                const CommitResult r = CommitChapterState(mem, u7b, c4);
                expect(r.ok, fmt::format("T4 ⑧f-7b：只声明不被引用的 `force_new` ⇒ 仍放行（T3b 标准 5）（{}）",
                                         r.error));
            }
        }
    }
    // ⑨ diff 存读往返（`audit_logs.detail` / 快照都用它）
    {
        StateDiff back;
        const std::string json = StateDiffToJson(diff);
        expect(StateDiffFromJson(json, back) && back.Hash() == diff.Hash() &&
                   back.characters.size() == diff.characters.size() &&
                   back.causal.size() == 1 && back.causal[0].cause.temp_id == "ev:1",
               "StateDiff JSON 往返一致（含嵌套 TempoRef）");
        StateDiff broken;
        expect(!StateDiffFromJson("{ not json", broken), "非法 JSON → 解析失败");
    }
    // ⑩ S11：G2 真的是 `06` §2.3 的 K01–K29 报告（`inline`），且失败项指名道姓
    {
        expect(first.gates.g2_source == "inline", "G2 口径 = 提交路径内跑 K01–K29（inline）");
        expect(first.checks_describe.find("K01") != std::string::npos &&
                   first.failed_check_ids.empty(),
               "K 报告摘要可读且干净提交无失败项");

        StateDiff dangling;
        dangling.chapter_id = *chapter;
        dangling.characters.push_back(
            {.entity_id = 999999, .body_state = "不该存在", .reason = "S11 自检"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, dangling, ok);
        bool hasK02 = false;
        for (const std::string& id : r.failed_check_ids) {
            if (id == "K02") {
                hasK02 = true;
            }
        }
        expect(!r.ok && hasK02, "K02（引用不存在实体）→ 拒绝提交并把 K02 报给调用方");
        expect(!r.checks_describe.empty() && r.checks_describe.find("K02") != std::string::npos,
               "被拒时报告里能看到 K02");
    }

    std::filesystem::remove_all(snapRoot, ec);
    if (fail == 0) {
        log::Info("S8 提交自检通过（门禁 G1–G5 / 14 块写入 / 幂等 / 快照 / D 规则 / "
                  "manual→PROPOSED · auto→CANON）");
    }
    return fail;
}

} // namespace shine::novelcore
