#pragma once
// shine_core::project —— 项目模型（P03-S1）：project.json 契约 + schemaVersion 平滑迁移。
//
// 契约见 docs/20-contracts/project-layout.md；分层边界见 docs/00-overview/architecture.md。
// 分层约束（API.md §1）：project 只依赖 core / util / db；**不依赖** novel / visual / comfy。
// 各业务模块通过 ProjectRef 拿路径，不反向耦合。
//
// 文本约定（util/Encoding.h 三条硬规则）：进 JSON / 日志 / UI 一律 UTF-8；
// 路径一律 std::filesystem::path 传递，进出字符串走 PathFromUtf8 / PathToUtf8。
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::project {

// 统一错误：code 稳定（io / parse / schema / conflict / invalid / state），
// message 中文可读（可直接进 UI 的 Field error / ErrorState）。
struct Error {
    std::string code;
    std::string message;
};

// —— project.json 契约（API.md §3）——
// 字段顺序即序列化键序（Serialize 固定），同一内存值 → 同一字节串（S1 判据：逐字节一致）。

struct Budget {
    int maxCallsPerChapter = 40;
    int maxHighTierPerChapter = 8;
    int maxShotsPerChapter = 2;
};

struct ProjectFile {
    // 当前 schema 版本。加字段时 +1，并在 Migrate 里补一段逐版本升格。
    inline static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    std::string id;          // "prj_<hex>"
    std::string name;        // 中文名允许
    std::string premise;     // 一句话创意（可空）
    std::string templateId;  // "blank" | "novel" | "film"
    std::string createdAt;   // "2026-09-21T10:00:00Z"
    std::string themeId;     // 主题 ascii id（可空 = 跟随应用主题）

    // links
    std::string comfyBaseUrl = "http://127.0.0.1:8188";
    std::string llmProfileId;

    // pipeline
    std::string runMode = "manual"; // manual | semi | auto
    Budget budget;

    // ui
    std::string lastWorkspace; // 可空（如 "novel"）
    int lastChapter = 0;
    std::string lastNovel;     // 当前小说（多书系列：books/<书名>；空 = 默认书=项目根）
};

[[nodiscard]] bool operator==(const Budget& a, const Budget& b) noexcept;
[[nodiscard]] bool operator==(const ProjectFile& a, const ProjectFile& b) noexcept;

// 规范化序列化：固定键序 + 2 空格缩进 + 尾随换行；纯函数（同值同字节）。
[[nodiscard]] std::string Serialize(const ProjectFile& file);

// 宽容读取 + 逐版本升格（API.md §3 迁移规则；沿用 VideoProject::Sanitize 的经验）：
//   * 未知键忽略；缺失键取默认；非法值自动纠正（schemaVersion<=0 → 1、负预算 → 默认值、
//     templateId 非法 → "blank"、runMode 非法 → "manual"…）；
//   * schemaVersion > kSchemaVersion → 拒绝（code="schema"），降级读取会损坏新文件；
//   * 文本整体非法（不是 JSON 对象）→ code="parse"。
[[nodiscard]] std::expected<ProjectFile, Error> Migrate(std::string_view json);

[[nodiscard]] std::filesystem::path ProjectFilePath(const std::filesystem::path& rootDir);
[[nodiscard]] std::expected<ProjectFile, Error> LoadProjectFile(const std::filesystem::path& rootDir);
[[nodiscard]] std::expected<void, Error> SaveProjectFile(const std::filesystem::path& rootDir,
                                                        const ProjectFile& file);

// —— 运行时引用（API.md §1 ProjectRef）：所有产物路径由它派生，禁止硬编码 %APPDATA%/CWD ——
struct ProjectRef {
    std::string id;
    std::filesystem::path rootDir;
    std::filesystem::path dbPath;    // <root>/db/novel.db
    std::filesystem::path workDir;   // <root>/work
    std::filesystem::path assetsDir; // <root>/assets
    std::filesystem::path outputDir; // <root>/output
    std::string name;
    int schemaVersion = ProjectFile::kSchemaVersion;
};

[[nodiscard]] ProjectRef MakeRef(const ProjectFile& file, const std::filesystem::path& rootDir);

// —— 多书系列（一部一库；目录契约见 docs/20-contracts/project-layout.md）——
// 目录契约：默认书 = 项目根（db/novel.db + work/，单书项目零迁移零变化）；
// 系列其他书 = books/<书名>/{db/novel.db, work/}（books/ 随用随建，与 work/ch<NNN> 同口径）。
// 跨书不共享库：跨部引用走「前情导入 + 只读补查」，书内寻址 volumes.ord → chapters.ord 不变。
struct BookRef {
    std::string title;             // 书名（默认书 = 项目名）
    std::filesystem::path rootDir; // 书根：默认书=<项目根>，其余=books/<书名>
    std::filesystem::path dbPath;  // <书根>/db/novel.db
    std::filesystem::path workDir; // <书根>/work
    bool isDefault = false;
};

// 书单：只列「有 db/novel.db」的书目录；默认书在前，其余按书名字典序（确定性输出）。
[[nodiscard]] std::vector<BookRef> ListBooks(const ProjectRef& ref);

// —— 最近列表条目（API.md §1 RecentEntry）——
struct RecentEntry {
    std::string id;
    std::string name;
    std::filesystem::path rootDir;
    std::filesystem::path coverThumb; // 可空
    std::chrono::system_clock::time_point lastOpened{};
};

// 新建项目入参（API.md §1 ProjectSpec）
struct ProjectSpec {
    std::string name;       // 中文名允许
    std::filesystem::path rootDir; // 中文路径允许（项目将创建在该目录下）
    std::string templateId; // "blank" | "novel" | "film"
    std::string premise;    // 可空
};

// 模板描述（API.md §1 ProjectTemplate）
struct ProjectTemplate {
    std::string id;          // "blank" | "novel" | "film"
    std::string name;        // 空白项目 / 小说项目 / 影视化项目
    std::string description; // 一句话说明（向导第 1 步展示）
};

// 向导校验（S5）：名字非空；templateId 合法；rootDir 非空、其父目录存在、
// 目标目录不存在或为空目录（已含 project.json → code="conflict"）。
[[nodiscard]] std::expected<void, Error> ValidateSpec(const ProjectSpec& spec);

// 项目服务（API.md §1）：新建 / 打开 / 保存 / 关闭 + 当前项目 + 最近列表 + 模板。
// 非单例；前端持有自己的实例（内部持有 ProjectIndex，见 ProjectIndex.h）。
class ProjectService {
  public:
    ProjectService();                                   // 默认索引位置（%APPDATA%/ShineTVStudio/projects.json）
    explicit ProjectService(std::filesystem::path indexFile); // 测试/自检用

    [[nodiscard]] std::expected<ProjectRef, Error> Create(const ProjectSpec&);
    [[nodiscard]] std::expected<ProjectRef, Error> Open(const std::filesystem::path& rootDir);
    [[nodiscard]] std::expected<void, Error> Save(const ProjectRef&); // 把内存态 project.json 落盘
    [[nodiscard]] std::expected<void, Error> Close();
    [[nodiscard]] std::optional<ProjectRef> Current() const;
    [[nodiscard]] std::vector<RecentEntry> Recent() const;
    [[nodiscard]] std::vector<ProjectTemplate> Templates() const;
    void TouchRecent(const ProjectRef&); // 更新 lastOpened（并写索引）

    // 编辑当前项目的内存态（如 ui.lastWorkspace / premise）；Save 时落盘。
    // 无当前项目时返回 nullptr。
    [[nodiscard]] ProjectFile* CurrentFile();
    [[nodiscard]] const ProjectFile* CurrentFile() const;

  private:
    // 轻量持有：索引文件路径 + 当前项目（ProjectFile + rootDir）。
    // 索引按需构造临时 ProjectIndex（Load → Upsert/Touch/Remove → Save）——
    // 项目开关频率低，索引 I/O 成本可忽略，换来成员结构最简。
    std::filesystem::path indexFile_;
    std::optional<ProjectFile> currentFile_;
    std::optional<ProjectRef> currentRef_;
};

// —— P03 自检入口（main.cpp 环境变量开关接线）——
// S1：Serialize 往返逐字节一致；Migrate 平滑迁移（缺字段/多未知键/非法值/超版本拒绝）。
[[nodiscard]] bool SelfTest(std::string* report);

// UTC ISO-8601（"2026-09-21T10:00:00Z"）—— createdAt / lastOpened 序列化用
[[nodiscard]] std::string Iso8601UtcNow();
[[nodiscard]] std::string Iso8601Utc(std::chrono::system_clock::time_point tp);
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> Iso8601UtcParse(std::string_view text);

} // namespace shine::project
