// shine_core::project —— 项目模型实现（P03-S1）：project.json 规范序列化 + schemaVersion 平滑迁移 + 项目服务。
//
// 为什么 Serialize 手写拼装而不走 yyjson_mut 写出：project.json 是用户会打开看、拿去 diff / 备份的文件，
// 契约要求「同值同字节」（S1 判据：逐字节一致）。手写拼装能把 2 空格缩进、固定键序、空串也写出键、
// 文件尾恰好一个 '\n' 这些细节逐字节钉死，不受三方序列化器排版选项（版本升级一变就全变）影响。
// 但**字符串转义必须走 util::json::JsonQuote**（全仓库唯一来源）：手写转义漏掉 <0x20 的控制字符
// 就是非法 JSON，这类 bug 本仓库踩过（见 util/Json.h 里 S42 的记录），正确性优先于那点性能。
//
// 文本/路径三条硬规则见 util/Encoding.h：进 JSON 一律 UTF-8；路径一律 std::filesystem::path，
// 进出字符串只走 PathFromUtf8 / PathToUtf8；文件读写只走 util::File（禁窄字符 fopen/ifstream ——
// Windows 下走 ANSI 代码页，中文路径必打不开）。
#include "project/Project.h"

#include "project/ProjectIndex.h"    // ProjectService：按 indexFile_ 现场构造临时索引对象（头文件不加成员）
#include "project/ProjectTemplate.h" // ProjectService::Create → Materialize 生成骨架
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Random.h"
#include "util/Strings.h"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace shine::project {
namespace {

constexpr std::string_view kDefaultComfyBaseUrl = "http://127.0.0.1:8188";

// 模板白名单（API.md §4）。不在表里的一律纠回 "blank"，别让模板 id 带着脏值进盘。
[[nodiscard]] bool IsTemplateId(std::string_view id) noexcept {
    return id == "blank" || id == "novel" || id == "film";
}

// 流水线模式白名单（API.md §3）
[[nodiscard]] bool IsRunMode(std::string_view mode) noexcept {
    return mode == "manual" || mode == "semi" || mode == "auto";
}

// std::error_code::message() 在 Windows 上返回 **ANSI 代码页**窄字符（util/Encoding.h 记录的坑），
// 直接进 UI 会被渲染成一串 '?'；只能拿 A 版文本时按规矩转一次 AcpToUtf8。
[[nodiscard]] std::string ErrText(const std::error_code& ec) { return util::AcpToUtf8(ec.message()); }

// JSON 整数读取（宽容三件套）：键缺失 / 类型不是数字 → 取默认；负数或超 int 范围 → 取默认
//（"非法值自动纠正"）。数值转换不走 atoi/sprintf（仓库禁令）；比较放在 int64 上做，
// 免得超大整数截断进 int 后反而"看起来合法"。
[[nodiscard]] int ReadNonNegInt(yyjson_val* obj, std::string_view key, int def) {
    yyjson_val* v = util::json::Get(obj, key);
    if (v == nullptr || !yyjson_is_num(v)) {
        return def;
    }
    const std::int64_t n = yyjson_get_sint(v);
    if (n < 0 || n > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        return def;
    }
    return static_cast<int>(n);
}

// 字符串字段读取：键在且值是字符串 → 原样取（**空串是合法值，不当缺失处理**，否则往返会变值）；
// 键缺失或类型不符 → 取默认。
[[nodiscard]] std::string ReadStr(yyjson_val* obj, std::string_view key, std::string_view def = {}) {
    yyjson_val* v = util::json::Get(obj, key);
    if (v != nullptr && yyjson_is_str(v)) {
        return std::string{yyjson_get_str(v), yyjson_get_len(v)};
    }
    return std::string{def};
}

} // namespace

// ————————————————————————————————————————————————————— 相等比较（S1 判据依赖）

bool operator==(const Budget& a, const Budget& b) noexcept {
    return a.maxCallsPerChapter == b.maxCallsPerChapter && a.maxHighTierPerChapter == b.maxHighTierPerChapter &&
           a.maxShotsPerChapter == b.maxShotsPerChapter;
}

bool operator==(const ProjectFile& a, const ProjectFile& b) noexcept {
    return a.schemaVersion == b.schemaVersion && a.id == b.id && a.name == b.name && a.premise == b.premise &&
           a.templateId == b.templateId && a.createdAt == b.createdAt && a.themeId == b.themeId &&
           a.comfyBaseUrl == b.comfyBaseUrl && a.llmProfileId == b.llmProfileId && a.runMode == b.runMode &&
           a.budget == b.budget && a.lastWorkspace == b.lastWorkspace && a.lastChapter == b.lastChapter &&
           a.lastNovel == b.lastNovel;
}

// ————————————————————————————————————————————————————— 规范化序列化（纯函数：同值同字节）

std::string Serialize(const ProjectFile& file) {
    // 格式契约（API.md §3，S1 题面"精确格式"）：固定键序 + 2 空格缩进 + 尾随恰好一个 '\n'，
    // **空串也写出键** —— 键的存在性本身也是契约：缺键会走 Migrate 的"取默认"，来回一趟就可能变值，
    // 逐字节一致判据当场破功。
    std::string s;
    s.reserve(512);
    s += "{\n";
    s += "  \"schemaVersion\": " + util::FromInt(file.schemaVersion) + ",\n";
    s += "  \"id\": " + util::json::JsonQuote(file.id) + ",\n";
    s += "  \"name\": " + util::json::JsonQuote(file.name) + ",\n";
    s += "  \"premise\": " + util::json::JsonQuote(file.premise) + ",\n";
    s += "  \"templateId\": " + util::json::JsonQuote(file.templateId) + ",\n";
    s += "  \"createdAt\": " + util::json::JsonQuote(file.createdAt) + ",\n";
    s += "  \"themeId\": " + util::json::JsonQuote(file.themeId) + ",\n";
    s += "  \"links\": {\n";
    s += "    \"comfyBaseUrl\": " + util::json::JsonQuote(file.comfyBaseUrl) + ",\n";
    s += "    \"llmProfileId\": " + util::json::JsonQuote(file.llmProfileId) + "\n";
    s += "  },\n";
    s += "  \"pipeline\": {\n";
    s += "    \"mode\": " + util::json::JsonQuote(file.runMode) + ",\n";
    s += "    \"budget\": {\n";
    s += "      \"maxCallsPerChapter\": " + util::FromInt(file.budget.maxCallsPerChapter) + ",\n";
    s += "      \"maxHighTierPerChapter\": " + util::FromInt(file.budget.maxHighTierPerChapter) + ",\n";
    s += "      \"maxShotsPerChapter\": " + util::FromInt(file.budget.maxShotsPerChapter) + "\n";
    s += "    }\n";
    s += "  },\n";
    s += "  \"ui\": {\n";
    s += "    \"lastWorkspace\": " + util::json::JsonQuote(file.lastWorkspace) + ",\n";
    s += "    \"lastChapter\": " + util::FromInt(file.lastChapter) + ",\n";
    s += "    \"lastNovel\": " + util::json::JsonQuote(file.lastNovel) + "\n";
    s += "  }\n";
    s += "}\n";
    return s;
}

// ————————————————————————————————————————————————————— 平滑迁移（宽容读 + 逐版本升格）

std::expected<ProjectFile, Error> Migrate(std::string_view json) {
    util::json::OwnedDoc doc = util::json::ParseDoc(json);
    if (!doc) {
        return std::unexpected(
            Error{"parse", "project.json 不是合法 JSON（解析失败）。文件可能被截断或手改坏："
                           "请修好 JSON，或删掉 project.json 后重新新建项目。"});
    }
    yyjson_val* root = doc.root();
    if (!yyjson_is_obj(root)) {
        return std::unexpected(Error{"parse", "project.json 顶层必须是 JSON 对象。请检查文件内容；"
                                              "若不确定来源，删掉它后重新新建项目。"});
    }

    ProjectFile out; // 结构体默认值就是"取默认"的默认

    // schemaVersion：缺失 / 非数字 → 1；0/负 → 纠正为 1；**比新 → 拒绝**（降级读取会把新文件写回旧格式，
    // 静默毁数据，宁可报错让升级程序）。比较在 int64 上做，超大数不会被截断成"合法"版本号。
    std::int64_t schemaVersion = 1;
    if (yyjson_val* v = util::json::Get(root, "schemaVersion"); v != nullptr && yyjson_is_num(v)) {
        schemaVersion = yyjson_get_sint(v);
    }
    if (schemaVersion > ProjectFile::kSchemaVersion) {
        return std::unexpected(
            Error{"schema", "project.json 的 schemaVersion=" + util::FromInt(schemaVersion) +
                                " 比本程序支持的 " + util::FromInt(static_cast<std::int64_t>(ProjectFile::kSchemaVersion)) +
                                " 更新，已拒绝读取（降级读会损坏新文件）。请升级 ShineTV Studio 后再打开。"});
    }
    out.schemaVersion = schemaVersion <= 0 ? 1 : static_cast<int>(schemaVersion);

    // id：缺失/空 → 新生成。空串按缺失处理（空 id 无法在最近列表里做主键）。
    out.id = ReadStr(root, "id");
    if (out.id.empty()) {
        out.id = "prj_" + util::RandomHex(8);
    }

    out.name = ReadStr(root, "name");
    out.premise = ReadStr(root, "premise");
    out.themeId = ReadStr(root, "themeId");

    // templateId / runMode：不在白名单 → 纠回默认（含空串）
    out.templateId = ReadStr(root, "templateId");
    if (!IsTemplateId(out.templateId)) {
        out.templateId = "blank";
    }

    // createdAt：缺失或解析不出 → 当前 UTC。注意这是**迁移里唯一非确定性的值**（老文件没有时间戳，
    // 只能补当下），所以判据"Migrate 后再 Serialize 再 Migrate 相等"成立，而 Migrate 本身不是纯函数。
    out.createdAt = ReadStr(root, "createdAt");
    if (out.createdAt.empty() || !Iso8601UtcParse(out.createdAt).has_value()) {
        out.createdAt = Iso8601UtcNow();
    }

    // links：整块缺失或类型不符 → 全默认；块内逐字段宽容（comfyBaseUrl 的默认不是空串，
    // 用 ReadStr 的 def 参数，且**显式区分"键缺失"与"值为空串"** —— 空串是合法配置值）。
    if (yyjson_val* links = util::json::GetObj(root, "links")) {
        out.comfyBaseUrl = ReadStr(links, "comfyBaseUrl", kDefaultComfyBaseUrl);
        out.llmProfileId = ReadStr(links, "llmProfileId");
    }

    // pipeline：mode 非法 → "manual"；budget 逐字段纠正（负数/类型不符 → 40/8/2）
    if (yyjson_val* pipeline = util::json::GetObj(root, "pipeline")) {
        out.runMode = ReadStr(pipeline, "mode");
        if (!IsRunMode(out.runMode)) {
            out.runMode = "manual";
        }
        if (yyjson_val* budget = util::json::GetObj(pipeline, "budget")) {
            const Budget def{};
            out.budget.maxCallsPerChapter = ReadNonNegInt(budget, "maxCallsPerChapter", def.maxCallsPerChapter);
            out.budget.maxHighTierPerChapter = ReadNonNegInt(budget, "maxHighTierPerChapter", def.maxHighTierPerChapter);
            out.budget.maxShotsPerChapter = ReadNonNegInt(budget, "maxShotsPerChapter", def.maxShotsPerChapter);
        }
    }

    // ui：lastChapter 负 → 0（结构体默认）
    if (yyjson_val* ui = util::json::GetObj(root, "ui")) {
        out.lastWorkspace = ReadStr(ui, "lastWorkspace");
        out.lastChapter = ReadNonNegInt(ui, "lastChapter", 0);
        out.lastNovel = ReadStr(ui, "lastNovel");
    }

    return out;
}

// ————————————————————————————————————————————————————— project.json 落盘 / 读回

std::filesystem::path ProjectFilePath(const std::filesystem::path& rootDir) { return rootDir / "project.json"; }

std::expected<ProjectFile, Error> LoadProjectFile(const std::filesystem::path& rootDir) {
    const std::filesystem::path path = ProjectFilePath(rootDir);
    const std::optional<std::string> bytes = util::ReadFileBytes(path);
    if (!bytes.has_value()) {
        return std::unexpected(Error{"io", "读不到项目文件：" + util::PathToUtf8(path) +
                                               "。请确认该目录是项目目录（内含 project.json），"
                                               "或用「新建项目」创建。"});
    }
    return Migrate(*bytes);
}

std::expected<void, Error> SaveProjectFile(const std::filesystem::path& rootDir, const ProjectFile& file) {
    const std::filesystem::path path = ProjectFilePath(rootDir);
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec); // 父目录不存在先建（新建项目时 root 本身还没影）
        if (ec) {
            return std::unexpected(Error{"io", "无法创建项目目录：" + util::PathToUtf8(parent) + "（" + ErrText(ec) +
                                                   "）。请检查磁盘空间与写权限后重试。"});
        }
    }
    if (!util::WriteFileBytes(path, Serialize(file))) {
        return std::unexpected(Error{"io", "写入项目文件失败：" + util::PathToUtf8(path) +
                                               "。请检查磁盘空间与写权限后重试。"});
    }
    return {};
}

// ————————————————————————————————————————————————————— 运行时引用

ProjectRef MakeRef(const ProjectFile& file, const std::filesystem::path& rootDir) {
    ProjectRef ref;
    ref.id = file.id;
    ref.name = file.name;
    ref.schemaVersion = file.schemaVersion;
    // rootDir 归一化：调用方可能传来 "a/b/"、"a\\.\\b" 等写法，派生路径前先拉直，
    // 免得同一个项目两处拼出来的产物路径长得不一样。
    ref.rootDir = rootDir.lexically_normal();
    ref.dbPath = ref.rootDir / "db" / "novel.db";
    ref.workDir = ref.rootDir / "work";
    ref.assetsDir = ref.rootDir / "assets";
    ref.outputDir = ref.rootDir / "output";
    return ref;
}

// ————————————————————————————————————————————————————— 向导校验

std::expected<void, Error> ValidateSpec(const ProjectSpec& spec) {
    if (util::Trim(spec.name).empty()) {
        return std::unexpected(
            Error{"invalid", "项目名称不能为空（去掉首尾空白后为空）。请填写项目名称后重试。"});
    }
    if (!IsTemplateId(spec.templateId)) {
        return std::unexpected(Error{"invalid", "模板 id 非法：" + spec.templateId +
                                                    "。只能是 blank / novel / film，请回到向导第 1 步重新选择。"});
    }
    if (spec.rootDir.empty()) {
        return std::unexpected(Error{"invalid", "项目目录不能为空。请选择或输入项目要创建到的目录后重试。"});
    }

    const std::filesystem::path root = spec.rootDir.lexically_normal();
    std::error_code ec;

    // 父目录必须已存在：不替用户凭空 create_directories 一整条链 —— 盘符选错/手滑多打一级目录时
    // 应当当场报错，而不是悄悄建出一棵没人要的目录树。
    std::filesystem::path parent = root.parent_path();
    if (parent.empty()) {
        // 单段相对路径（如 "灯语回声"）没有 parent_path，语义上父目录就是当前目录
        parent = std::filesystem::current_path(ec);
        if (ec) {
            parent = std::filesystem::path{"."};
        }
        ec.clear();
    }
    if (!std::filesystem::is_directory(parent, ec) || ec) {
        return std::unexpected(Error{"invalid", "项目目录的父目录不存在：" + util::PathToUtf8(parent) +
                                                    "。请先创建该目录，或换一个已存在目录下的位置。"});
    }

    // 目标目录三种情况：不存在 ✓ / 已存在但空 ✓ / 其他一律拦下。
    // 已含 project.json 是**明确冲突**（同目录二次新建），单独给 conflict 码让 UI 走「打开 instead」提示。
    ec.clear();
    if (std::filesystem::exists(root / "project.json", ec) && !ec) {
        return std::unexpected(Error{"conflict", "该目录已是项目目录（project.json 已存在）"});
    }
    ec.clear();
    if (std::filesystem::exists(root, ec) && !ec) {
        ec.clear();
        if (!std::filesystem::is_directory(root, ec) || ec) {
            return std::unexpected(Error{"invalid", "目标路径已存在且不是目录：" + util::PathToUtf8(root) +
                                                        "。请换一个项目目录后重试。"});
        }
        std::filesystem::directory_iterator it(root, ec);
        if (ec) {
            return std::unexpected(Error{"io", "打不开目标目录：" + util::PathToUtf8(root) + "（" + ErrText(ec) +
                                                   "）。请检查读权限后重试。"});
        }
        if (it != std::filesystem::directory_iterator{}) {
            return std::unexpected(Error{"invalid", "目标目录已存在且不是空目录：" + util::PathToUtf8(root) +
                                                        "。请换一个空目录（或新的目录名）后重试。"});
        }
    }
    return {};
}

// ————————————————————————————————————————————————————— UTC ISO-8601 时间

std::string Iso8601UtcNow() { return Iso8601Utc(std::chrono::system_clock::now()); }

std::string Iso8601Utc(std::chrono::system_clock::time_point tp) {
    // fmt::gmtime 走 gmtime_s/gmtime_r（比 std::gmtime 线程安全）。秒级精度足够（createdAt / lastOpened
    // 的契约就是秒级 "2026-09-21T10:00:00Z"）；to_time_t 会截掉亚秒，这正是我们要的。
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    return fmt::format("{:%Y-%m-%dT%H:%M:%S}Z", fmt::gmtime(t));
}

std::optional<std::chrono::system_clock::time_point> Iso8601UtcParse(std::string_view text) {
    std::tm tm{};
    std::istringstream in{std::string{text}};
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) {
        return std::nullopt;
    }
    // 尾部多余字符不算合法（宽容但不马虎：否则 "2026-09-21T10:00:00Z 乱写" 也会被当合法时间）
    if (in.peek() != std::char_traits<char>::eof()) {
        return std::nullopt;
    }
#if defined(_WIN32)
    const std::time_t t = _mkgmtime(&tm); // MinGW-w64 可用；timegm 是 POSIX 名字，这里不混用
#else
    const std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) {
        return std::nullopt; // 月=99 这种 get_time 不拦、_mkgmtime 会拒
    }
    return std::chrono::system_clock::from_time_t(t);
}

// ————————————————————————————————————————————————————— 项目服务

ProjectService::ProjectService() : indexFile_(DefaultIndexFile()) {}

ProjectService::ProjectService(std::filesystem::path indexFile) : indexFile_(std::move(indexFile)) {}

std::expected<ProjectRef, Error> ProjectService::Create(const ProjectSpec& spec) {
    if (std::expected<void, Error> ok = ValidateSpec(spec); !ok) {
        return std::unexpected(ok.error());
    }

    ProjectFile file; // 结构体默认值即契约默认（themeId 空=跟随应用主题、runMode manual、budget 40/8/2）
    file.schemaVersion = ProjectFile::kSchemaVersion;
    file.id = "prj_" + util::RandomHex(8);
    file.name = spec.name;
    file.premise = spec.premise;
    file.templateId = spec.templateId;
    file.createdAt = Iso8601UtcNow();

    const ProjectRef ref = MakeRef(file, spec.rootDir);
    if (std::expected<void, Error> made = Materialize(spec, ref, file); !made) {
        return std::unexpected(made.error());
    }

    TouchRecent(ref); // 索引是尽力而为，写失败已在 TouchRecent 里吞掉
    currentFile_ = file;
    currentRef_ = ref;
    return ref;
}

std::expected<ProjectRef, Error> ProjectService::Open(const std::filesystem::path& rootDir) {
    std::expected<ProjectFile, Error> loaded = LoadProjectFile(rootDir);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    const ProjectRef ref = MakeRef(*loaded, rootDir);
    currentFile_ = *loaded;
    currentRef_ = ref;
    TouchRecent(ref);
    return ref;
}

std::expected<void, Error> ProjectService::Save(const ProjectRef& ref) {
    if (!currentFile_.has_value() || !currentRef_.has_value()) {
        return std::unexpected(Error{"state", "当前没有打开的项目，无法保存。请先新建或打开一个项目。"});
    }
    if (currentRef_->id != ref.id) {
        return std::unexpected(Error{"state", "要保存的项目（id=" + ref.id + "）与当前打开的项目（id=" +
                                                  currentRef_->id + "）不一致。请重新打开该项目后再保存。"});
    }
    // 落盘路径以**当前项目**的 rootDir 为准：调用方传来的 ref 可能是旧副本，不能拿它拼路径
    return SaveProjectFile(currentRef_->rootDir, *currentFile_);
}

std::expected<void, Error> ProjectService::Close() {
    currentFile_.reset();
    currentRef_.reset();
    return {};
}

std::optional<ProjectRef> ProjectService::Current() const { return currentRef_; }

std::vector<RecentEntry> ProjectService::Recent() const {
    // 索引按 indexFile_ 现场构造临时对象用（头文件只有三个成员，刻意不加索引成员）：
    // 索引文件小、读一次几微秒，换来"每次都是盘上最新状态"，多实例也不会互相覆盖。
    ProjectIndex index(indexFile_);
    (void)index.Load();
    const std::vector<RecentEntry>& entries = index.Recent().Entries();
    return std::vector<RecentEntry>{entries.begin(), entries.end()};
}

std::vector<ProjectTemplate> ProjectService::Templates() const { return AllTemplates(); }

void ProjectService::TouchRecent(const ProjectRef& ref) {
    ProjectIndex index(indexFile_);
    (void)index.Load(); // 先读：不读就 Upsert+Save 会把别的项目的条目全冲掉
    index.Touch(ref);
    (void)index.Save(); // 索引是尽力而为：写失败不报错（下次打开会再 Touch，最多丢一次 lastOpened）
}

ProjectFile* ProjectService::CurrentFile() {
    return currentFile_.has_value() ? &*currentFile_ : nullptr;
}

const ProjectFile* ProjectService::CurrentFile() const {
    return currentFile_.has_value() ? &*currentFile_ : nullptr;
}

// ————————————————————————————————————————————————————— S1 自检

std::vector<BookRef> ListBooks(const ProjectRef& ref) {
    std::vector<BookRef> out;
    std::error_code ec;
    // 默认书 = 项目根（db/novel.db + work/）—— 单书项目就停在这一层，零迁移零变化
    if (std::filesystem::exists(ref.dbPath, ec)) {
        BookRef def;
        def.title = ref.name;
        def.rootDir = ref.rootDir;
        def.dbPath = ref.dbPath;
        def.workDir = ref.workDir;
        def.isDefault = true;
        out.push_back(std::move(def));
    }
    // 系列其他书 = books/<书名>/（一部一库；只认有 db/novel.db 的书目录）
    std::vector<BookRef> extra;
    const std::filesystem::path booksDir = ref.rootDir / "books";
    if (std::filesystem::is_directory(booksDir, ec)) {
        for (std::filesystem::directory_iterator it(booksDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) {
                continue;
            }
            BookRef b;
            b.rootDir = it->path();
            b.dbPath = b.rootDir / "db" / "novel.db";
            b.workDir = b.rootDir / "work";
            if (!std::filesystem::exists(b.dbPath, ec)) {
                continue; // 没建库的目录不算书
            }
            b.title = util::PathToUtf8(b.rootDir.filename());
            extra.push_back(std::move(b));
        }
    }
    std::sort(extra.begin(), extra.end(),
              [](const BookRef& a, const BookRef& b) { return a.title < b.title; }); // 确定性输出
    for (BookRef& b : extra) {
        out.push_back(std::move(b));
    }
    return out;
}

bool SelfTest(std::string* report) {
    bool ok = true;
    std::string text;
    auto row = [&text, &ok](std::string_view item, bool pass, std::string_view detail) {
        text += "[S1] ";
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
        std::filesystem::temp_directory_path(ec) / ("shinetv-p03s1-" + util::RandomHex(8));
    if (ec) {
        row("临时目录", false, "拿不到系统临时目录，自检无法进行");
        if (report != nullptr) {
            *report = text;
        }
        return false;
    }
    std::filesystem::create_directories(base, ec);

    // ── (a) 逐字节一致：保存 → "重启"（只认盘上字节重新 Load）→ 再保存，两份文件必须一模一样 ──
    {
        ProjectFile x;
        x.schemaVersion = ProjectFile::kSchemaVersion;
        x.id = "prj_0123456789abcdef";
        x.name = "灯语回声";
        x.premise = "一句话创意：灯语与回声，中文往返必须逐字节一致。";
        x.templateId = "film";
        x.createdAt = "2026-09-21T10:00:00Z";
        x.themeId = "deep-space";
        x.runMode = "manual";
        x.lastWorkspace = "novel";
        x.lastChapter = 12;
        x.lastNovel = "灯语回声·上";

        const std::filesystem::path rootA = base / "根甲";
        const std::filesystem::path rootB = base / "根乙";
        bool pass = true;
        std::string detail;

        if (std::expected<void, Error> r = SaveProjectFile(rootA, x); !r) {
            pass = false;
            detail = "保存失败：" + r.error().message;
        }
        const std::optional<std::string> a = util::ReadFileBytes(ProjectFilePath(rootA));
        if (pass && !a.has_value()) {
            pass = false;
            detail = "读不回刚写下的 project.json";
        }
        std::optional<std::string> b;
        if (pass) {
            std::expected<ProjectFile, Error> y = LoadProjectFile(rootA); // "重启"：只认盘上的字节
            if (!y) {
                pass = false;
                detail = "重启读回失败：" + y.error().message;
            } else if (!(*y == x)) {
                pass = false;
                detail = "重启读回值与写入值不一致（operator== 为假）";
            } else if (std::expected<void, Error> r = SaveProjectFile(rootB, *y); !r) {
                pass = false;
                detail = "第二次保存失败：" + r.error().message;
            } else {
                b = util::ReadFileBytes(ProjectFilePath(rootB));
                if (!b.has_value() || *a != *b) {
                    pass = false;
                    detail = "两次落盘字节不一致（逐字节判据失败）";
                }
            }
        }
        if (pass) {
            // 再补两刀：Serialize 是纯函数（二次序列化同字节）；Migrate(落盘字节) 还原出原值
            const std::string s1 = Serialize(x);
            const std::string s2 = Serialize(x);
            if (s1 != s2 || s1 != *a) {
                pass = false;
                detail = "Serialize 非纯函数或与落盘字节不一致";
            } else if (std::expected<ProjectFile, Error> m = Migrate(*a); !m.has_value() || !(*m == x)) {
                pass = false;
                detail = "Migrate(落盘字节) 与原值不相等";
            } else {
                detail = "A/B 与两次 Serialize 均逐字节一致（" + util::FromInt(a->size()) + " 字节）";
            }
        }
        row("逐字节一致", pass, detail);
    }

    // ── (b) 平滑迁移：缺一堆键 + 未知键 + 非法值的旧版文件，必须被补齐/纠正成默认 ──
    {
        const std::string oldJson = R"({
  "id": "prj_old0000000000ff",
  "name": "旧版项目",
  "premise": "缺一堆键、带未知键、还有非法值",
  "templateId": "weird",
  "future": { "anything": [1, 2, 3] },
  "pipeline": {
    "budget": {
      "maxCallsPerChapter": -5,
      "maxHighTierPerChapter": "很多",
      "maxShotsPerChapter": -1
    }
  },
  "ui": {
    "lastChapter": -3
  }
})";
        std::expected<ProjectFile, Error> m = Migrate(oldJson);
        bool pass = m.has_value();
        std::string detail;
        if (!pass) {
            detail = "Migrate 失败：" + m.error().code + " " + m.error().message;
        } else {
            const ProjectFile& f = *m;
            const Budget def{};
            const bool fieldsOk =
                f.schemaVersion == 1 && f.id == "prj_old0000000000ff" && f.name == "旧版项目" &&
                f.templateId == "blank" && f.runMode == "manual" && f.budget == def && f.lastChapter == 0 &&
                f.lastWorkspace.empty() && f.lastNovel.empty() && f.themeId.empty() &&
                f.llmProfileId.empty() &&
                f.comfyBaseUrl == "http://127.0.0.1:8188" && !f.createdAt.empty() &&
                Iso8601UtcParse(f.createdAt).has_value();
            if (!fieldsOk) {
                pass = false;
                detail = "自动纠正结果与预期默认值不符";
            } else if (std::expected<ProjectFile, Error> m2 = Migrate(Serialize(f));
                       !m2.has_value() || !(*m2 == f)) {
                pass = false;
                detail = "Serialize→Migrate 往返不相等";
            } else {
                detail = "未知键忽略、缺失/非法值已全部补齐或纠正为默认";
            }
        }
        row("平滑迁移", pass, detail);
    }

    // ── (c) 超版本拒绝：schemaVersion 大于当前 → Error{"schema"}，绝不降级读 ──
    {
        std::expected<ProjectFile, Error> m =
            Migrate(R"({"schemaVersion": 99, "id": "prj_future", "name": "来自未来"})");
        const bool pass = !m.has_value() && m.error().code == "schema";
        row("超版本拒绝", pass, pass ? "schemaVersion=99 → Error{schema}" : "未按预期拒绝（code 应为 schema）");
    }

    // ── (d) 中文路径：中文目录下建/存/读回无乱码（读回相等即可证）──
    {
        ProjectFile z;
        z.id = "prj_cafe00000000babe";
        z.name = "灯语回声";
        z.premise = "中文路径往返：自检项目/灯语回声。";
        z.templateId = "novel";
        z.createdAt = "2026-09-21T10:00:00Z";

        const std::filesystem::path cnRoot = base / "自检项目" / "灯语回声";
        bool pass = true;
        std::string detail;
        if (std::expected<void, Error> r = SaveProjectFile(cnRoot, z); !r) {
            pass = false;
            detail = "中文目录保存失败：" + r.error().message;
        } else {
            std::expected<ProjectFile, Error> back = LoadProjectFile(cnRoot);
            if (!back.has_value() || !(*back == z)) {
                pass = false;
                detail = "中文目录读回不相等（乱码或打开失败）";
            } else {
                detail = "中文目录创建/保存/读回一致";
            }
        }
        row("中文路径", pass, detail);
    }

    // ── (e) 多书契约（一部一库；多小说与分卷-架构决策.md §4）：
    //        默认书=项目根、系列书=books/<书名>，只认有 db/novel.db 的书目录，确定性排序 ──
    {
        ProjectFile pf;
        pf.id = "prj_series00000ff";
        pf.name = "灯语回声系列";
        pf.templateId = "novel";
        pf.createdAt = "2026-09-21T10:00:00Z";
        const std::filesystem::path root = base / "系列·灯语回声";
        bool pass = true;
        std::string detail;
        auto touch = [&pass](const std::filesystem::path& db) {
            std::error_code e2;
            std::filesystem::create_directories(db.parent_path(), e2);
            pass = pass && util::WriteFileBytes(db, "placeholder-for-selftest");
        };
        if (std::expected<void, Error> r = SaveProjectFile(root, pf); !r) {
            pass = false;
            detail = "保存失败：" + r.error().message;
        } else {
            touch(root / "db" / "novel.db");                          // 默认书
            touch(root / "books" / "灯语回声·上" / "db" / "novel.db"); // 系列·上
            touch(root / "books" / "灯语回声·下" / "db" / "novel.db"); // 系列·下
            std::error_code e2;
            std::filesystem::create_directories(root / "books" / "没建库的书", e2); // 不该算一本书
            const std::vector<BookRef> books = ListBooks(MakeRef(pf, root));
            const bool shapeOk =
                books.size() == 3 && books[0].isDefault && books[0].title == "灯语回声系列" &&
                books[0].dbPath == root / "db" / "novel.db" && books[0].workDir == root / "work" &&
                !books[1].isDefault && books[1].title == "灯语回声·上" &&
                books[1].rootDir == root / "books" / "灯语回声·上" &&
                books[1].dbPath == root / "books" / "灯语回声·上" / "db" / "novel.db" &&
                books[1].workDir == root / "books" / "灯语回声·上" / "work" &&
                books[2].title == "灯语回声·下";
            if (!pass) {
                detail = "铺底文件写入失败";
            } else if (!shapeOk) {
                pass = false;
                detail = "书单形状与预期不符（默认书在前 + 系列书按书名字典序 + 无库目录不计）";
            } else {
                detail = "默认书 + 上/下两部（books/，字典序），无库目录未混入（共 " +
                         util::FromInt(static_cast<int>(books.size())) + " 本）";
            }
        }
        row("多书契约", pass, detail);
    }

    std::filesystem::remove_all(base, ec); // 结束前清场（失败忽略：临时目录留着也无害）
    if (report != nullptr) {
        *report = text;
    }
    return ok;
}

} // namespace shine::project
