// shine_core::project —— 项目索引与最近列表实现（P03-S2）。
//
// 索引文件是**派生数据**（丢了最多丢一次 lastOpened/封面，项目本体毫发无损），所以策略是
// 「宽容重建」：文件缺失 / JSON 坏 / 版本超新 → 返回 false + 清空列表，UI 提示「重建索引」即可，
// 绝不抛异常、绝不崩。与此相对，project.json 是权威数据，读不懂必须报错（见 Project.cpp 的 Migrate）。
//
// 序列化与 S1 同规范：固定键序 + 2 空格缩进 + 尾随恰好一个 '\n' + 字符串一律 util::json::JsonQuote
// （不手写转义）。rootDir / coverThumb 以 UTF-8 字符串存盘，进出只走 util::PathFromUtf8 / PathToUtf8
// （path.string() 在中文路径下会按 ANSI 代码页转码，必乱码 —— util/Encoding.h 的记录）。
#include "project/ProjectIndex.h"

#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Random.h"
#include "util/Strings.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::project {
namespace {

// 索引文件自带的 schemaVersion（与 project.json 的版本各自演进）。
constexpr std::int64_t kIndexSchemaVersion = 1;

[[nodiscard]] std::string ErrText(const std::error_code& ec) { return util::AcpToUtf8(ec.message()); }

// 索引文件的规范化序列化：与 S1 同规范（固定键序、2 空格缩进、尾 '\n'、字符串过 JsonQuote）。
// 数组顺序就是 RecentList 顺序（lastOpened 降序），空数组写 "[]"（无内容可缩进）。
[[nodiscard]] std::string SerializeIndex(const RecentList& list) {
    const std::vector<RecentEntry>& entries = list.Entries();
    std::string s;
    s += "{\n";
    s += "  \"schemaVersion\": " + util::FromInt(kIndexSchemaVersion) + ",\n";
    s += "  \"projects\": [";
    if (entries.empty()) {
        s += "]\n}\n";
        return s;
    }
    s += "\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RecentEntry& e = entries[i];
        s += "    {\n";
        s += "      \"id\": " + util::json::JsonQuote(e.id) + ",\n";
        s += "      \"name\": " + util::json::JsonQuote(e.name) + ",\n";
        s += "      \"rootDir\": " + util::json::JsonQuote(util::PathToUtf8(e.rootDir)) + ",\n";
        s += "      \"coverThumb\": " + util::json::JsonQuote(util::PathToUtf8(e.coverThumb)) + ",\n";
        s += "      \"lastOpened\": " + util::json::JsonQuote(Iso8601Utc(e.lastOpened)) + "\n";
        s += (i + 1 < entries.size()) ? "    },\n" : "    }\n";
    }
    s += "  ]\n}\n";
    return s;
}

} // namespace

// ————————————————————————————————————————————————————— RecentList

void RecentList::Upsert(RecentEntry entry) {
    // 按 id 去重：同 id 只留最新的一份（旧条目整体丢弃，不做字段级合并 —— 调用方语义就是"整条登记"）
    std::erase_if(entries_, [&](const RecentEntry& e) { return e.id == entry.id; });
    // 置顶插入 + 稳定降序排序：新条目放最前，稳定排序保证 lastOpened 相同时它仍排在旧同秒条目之前
    //（"移到首位"与"降序稳定排序"两条要求同时满足）；随后截断到 kMax。
    entries_.insert(entries_.begin(), std::move(entry));
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const RecentEntry& a, const RecentEntry& b) { return a.lastOpened > b.lastOpened; });
    if (entries_.size() > kMax) {
        entries_.resize(kMax);
    }
}

bool RecentList::Remove(const std::string& id) {
    const std::size_t before = entries_.size();
    std::erase_if(entries_, [&](const RecentEntry& e) { return e.id == id; });
    return entries_.size() != before; // 只移除列表条目，**不删项目文件**
}

std::optional<RecentEntry> RecentList::Find(const std::string& id) const {
    for (const RecentEntry& e : entries_) {
        if (e.id == id) {
            return e;
        }
    }
    return std::nullopt;
}

// ————————————————————————————————————————————————————— ProjectIndex

ProjectIndex::ProjectIndex() : indexFile_(DefaultIndexFile()) {}

ProjectIndex::ProjectIndex(std::filesystem::path indexFile) : indexFile_(std::move(indexFile)) {}

bool ProjectIndex::Load() {
    recent_.Clear();
    const std::optional<std::string> bytes = util::ReadFileBytes(indexFile_);
    if (!bytes.has_value()) {
        return false; // 文件缺失/打不开：按"重建索引"语义回空列表
    }
    util::json::OwnedDoc doc = util::json::ParseDoc(*bytes);
    if (!doc) {
        return false; // JSON 坏：同样重建（索引是派生数据，重建无损）
    }
    yyjson_val* root = doc.root();
    if (!yyjson_is_obj(root)) {
        return false;
    }
    // schemaVersion 同样宽容：缺失/非数字 → 1；比新 → 让调用方重建（旧版程序去改写新格式索引会丢字段）
    std::int64_t schemaVersion = 1;
    if (yyjson_val* v = util::json::Get(root, "schemaVersion"); v != nullptr && yyjson_is_num(v)) {
        schemaVersion = yyjson_get_sint(v);
    }
    if (schemaVersion > kIndexSchemaVersion) {
        return false;
    }

    yyjson_val* projects = util::json::GetArr(root, "projects");
    if (projects == nullptr) {
        return true; // 合法 JSON 但没有 projects 数组：空索引算读成功（不是损坏）
    }

    std::vector<RecentEntry> parsed;
    std::size_t i = 0;
    std::size_t n = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(projects, i, n, item) {
        if (item == nullptr || !yyjson_is_obj(item)) {
            continue;
        }
        RecentEntry e;
        e.id = util::json::GetStrCopy(item, "id");
        if (e.id.empty()) {
            continue; // 缺 id 的条目跳过：没有主键，去重/删除/更新都没法定位它
        }
        e.name = util::json::GetStrCopy(item, "name");
        e.rootDir = util::PathFromUtf8(util::json::GetStr(item, "rootDir"));
        e.coverThumb = util::PathFromUtf8(util::json::GetStr(item, "coverThumb"));
        const std::string lastOpened = util::json::GetStrCopy(item, "lastOpened");
        // lastOpened 非法 → epoch 起点（排到列表尾部，好过整条丢弃）
        e.lastOpened = Iso8601UtcParse(lastOpened).value_or(std::chrono::system_clock::time_point{});
        parsed.push_back(std::move(e));
    }
    // 倒序 Upsert 灌回：Upsert 是"置顶 + 稳定降序"，倒着灌才能把文件里的相对顺序原样保留
    //（lastOpened 同秒的并列条目正着灌会被整体颠倒 —— 读回逐条相等的判据就挂了）。
    for (auto it = parsed.rbegin(); it != parsed.rend(); ++it) {
        recent_.Upsert(std::move(*it));
    }
    return true;
}

std::expected<void, Error> ProjectIndex::Save() const {
    std::error_code ec;
    const std::filesystem::path parent = indexFile_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return std::unexpected(Error{"io", "无法创建索引目录：" + util::PathToUtf8(parent) + "（" + ErrText(ec) +
                                                   "）。请检查磁盘空间与写权限；最近列表会以空列表重建，不影响项目文件。"});
        }
    }
    if (!util::WriteFileBytes(indexFile_, SerializeIndex(recent_))) {
        return std::unexpected(Error{"io", "写入最近列表失败：" + util::PathToUtf8(indexFile_) +
                                               "。请检查磁盘空间与写权限；下次启动会重建索引，不影响项目文件。"});
    }
    return {};
}

void ProjectIndex::Touch(const ProjectRef& ref, const std::filesystem::path& coverThumb) {
    std::filesystem::path cover = coverThumb;
    if (cover.empty()) {
        // 打开项目不该把已有封面缩略图抹掉：参数没给就沿用旧值（Upsert 是"整条覆盖"语义，
        // 保不保留封面这个决定放在 Touch 这层做）
        if (const std::optional<RecentEntry> old = recent_.Find(ref.id); old.has_value()) {
            cover = old->coverThumb;
        }
    }
    RecentEntry e;
    e.id = ref.id;
    e.name = ref.name;
    e.rootDir = ref.rootDir;
    e.coverThumb = cover;
    e.lastOpened = std::chrono::system_clock::now();
    recent_.Upsert(std::move(e));
}

bool ProjectIndex::Remove(const std::string& id) { return recent_.Remove(id); }

// ————————————————————————————————————————————————————— 默认索引位置

std::filesystem::path DefaultIndexFile() {
    // %APPDATA% 必须走 **W 版** API：_wgetenv / GetEnvironmentVariableW。窄字符 getenv 在中文
    // 用户名下返回 GBK 字节，拼进 path 就错目录（util/Encoding.h 的头号坑）。
    constexpr DWORD kBufChars = 32768; // 环境变量上限 32767，留 1 个给结尾 '\0'
    wchar_t buffer[kBufChars] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buffer, kBufChars - 1);
    if (n == 0 || n >= kBufChars) {
        // APPDATA 取不到（服务账户 / 极简环境）→ 当前目录，保证测试自检照常可跑
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        return (ec ? std::filesystem::path{} : cwd) / "projects.json";
    }
    return std::filesystem::path{std::wstring{buffer, n}} / "ShineTVStudio" / "projects.json";
}

// ————————————————————————————————————————————————————— S2 自检

bool IndexSelfTest(std::string* report) {
    bool ok = true;
    std::string text;
    auto row = [&text, &ok](std::string_view item, bool pass, std::string_view detail) {
        text += "[S2] ";
        text += item;
        text += pass ? ": PASS" : ": FAIL";
        if (!detail.empty()) {
            text += ' ';
            text += detail;
        }
        text += '\n';
        ok = ok && pass;
    };

    std::error_code ec;
    const std::filesystem::path base =
        std::filesystem::temp_directory_path(ec) / ("shinetv-p03s2-" + util::RandomHex(8));
    if (ec) {
        row("临时目录", false, "拿不到系统临时目录，自检无法进行");
        if (report != nullptr) {
            *report = text;
        }
        return false;
    }
    std::filesystem::create_directories(base, ec);

    const std::filesystem::path indexFile = base / "projects.json";
    ProjectIndex index(indexFile);
    const auto t0 = std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};

    auto makeEntry = [&base](int i, std::chrono::system_clock::time_point tp) {
        RecentEntry e;
        e.id = "prj_self" + util::FromInt(i);
        e.name = "灯语回声-" + util::FromInt(i);
        e.rootDir = base / ("项目-" + util::FromInt(i)); // 中文 rootDir：顺带验证 UTF-8 往返
        e.coverThumb = (i % 5 == 0) ? (base / ("封面-" + util::FromInt(i) + ".png")) : std::filesystem::path{};
        e.lastOpened = tp;
        return e;
    };

    // ── (a) 连续 Upsert 12 条不同 id（lastOpened 递增）→ 截 10 条、降序 ──
    for (int i = 1; i <= 12; ++i) {
        index.Recent().Upsert(makeEntry(i, t0 + std::chrono::seconds{i}));
    }
    {
        const std::vector<RecentEntry>& es = index.Recent().Entries();
        // 12 条只留最新 10 条：首条=第 12 个（最新），尾条=第 3 个插入的（12-10+1，即"倒数第 3 新"那条被截掉的边界）
        const bool pass = es.size() == 10 && es.front().id == "prj_self12" && es.back().id == "prj_self3";
        row("上限与降序", pass, "12 条 → 10 条，首=最新、尾=第 3 个插入的（10 条上限 + 降序）");
    }

    // ── (b) Upsert 已存在 id（新时间）→ 移到首位且不增条数 ──
    {
        index.Recent().Upsert(makeEntry(7, t0 + std::chrono::seconds{999}));
        const std::vector<RecentEntry>& es = index.Recent().Entries();
        const bool pass = es.size() == 10 && es.front().id == "prj_self7";
        row("去重置顶", pass, "同 id 更新后置顶、条数不变");
    }

    // ── (c) Remove 一条 → 少一条且查不到 ──
    {
        const bool removed = index.Recent().Remove("prj_self7");
        const std::vector<RecentEntry>& es = index.Recent().Entries();
        const bool pass = removed && es.size() == 9 && !index.Recent().Find("prj_self7").has_value();
        row("移除", pass, "Remove 后 9 条且按 id 查不到");
    }

    // ── (d) Save → 新实例 Load → 逐条相等 ──
    {
        const std::vector<RecentEntry> before = index.Recent().Entries(); // 快照
        bool pass = true;
        std::string detail;
        if (std::expected<void, Error> r = index.Save(); !r) {
            pass = false;
            detail = r.error().message;
        } else {
            ProjectIndex again(indexFile);
            const bool loaded = again.Load();
            const std::vector<RecentEntry>& after = again.Recent().Entries();
            pass = loaded && after.size() == before.size();
            for (std::size_t i = 0; pass && i < before.size(); ++i) {
                const RecentEntry& a = before[i];
                const RecentEntry& b = after[i];
                // 逐条比：id / name / rootDir(UTF-8 串) / lastOpened(ISO 串)（外加封面路径）
                if (a.id != b.id || a.name != b.name || util::PathToUtf8(a.rootDir) != util::PathToUtf8(b.rootDir) ||
                    util::PathToUtf8(a.coverThumb) != util::PathToUtf8(b.coverThumb) ||
                    Iso8601Utc(a.lastOpened) != Iso8601Utc(b.lastOpened)) {
                    pass = false;
                    detail = "第 " + util::FromInt(i + 1) + " 条不一致";
                }
            }
            if (pass) {
                detail = util::FromInt(before.size()) + " 条逐条一致（含中文 rootDir 的 UTF-8 往返）";
            }
        }
        row("落盘读回", pass, detail);
    }

    // ── (e) 坏 JSON → Load=false 且列表清空（宽容重建语义）──
    {
        (void)util::WriteFileBytes(indexFile, "{ 坏");
        ProjectIndex bad(indexFile);
        const bool loaded = bad.Load();
        const bool pass = !loaded && bad.Recent().Entries().empty();
        row("坏文件重建", pass, "坏 JSON → Load=false 且列表清空");
    }

    std::filesystem::remove_all(base, ec); // 清场（失败忽略）
    if (report != nullptr) {
        *report = text;
    }
    return ok;
}

} // namespace shine::project
