// shine_core::project —— 项目模板实现（P03-S3）：空白 / 小说 / 影视化 三套骨架。
//
// 目录契约（API.md §2）三套模板共用；模板差异见 API.md §4。两条实现纪律：
//   1. **project.json 最后写**：先建目录与预置文件，全部成功才落 project.json。中途失败宁可留下
//      目录骨架（无害、可整目录删掉），也不留半成品 project.json —— 后者会被误认成"是个项目"，
//      比留一堆空目录危险得多（不做回滚，但不留半成品元数据）。
//   2. **PreviewTree 与 Materialize 共用同一张清单**（kContractDirs / kDocPresets / kFlowPresets），
//      顺序即创建顺序；自检会做实际盘点与预览的双向差集，两处漂移当场暴露。
//
// 中文名 / 中文路径全链路 UTF-8：文件读写只走 util::File（禁窄字符 fopen/ifstream），
// 路径进出字符串只走 util::PathFromUtf8 / util::PathToUtf8（util/Encoding.h 三条硬规则）。
#include "project/ProjectTemplate.h"

#include "db/sqlite/SqliteDb.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Random.h"
#include "util/Strings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::project {
namespace {

// —— 骨架清单（顺序 = 创建顺序 = PreviewTree 顺序，Materialize / PreviewTree / 自检三处共用）——

// API.md §2 契约目录（三套模板共用）。目录带尾 '/'，与 PreviewTree 的输出格式一致。
constexpr const char* kContractDirs[] = {
    "db/",            //
    "work/",          //
    "visual/",        //
    "visual/assets/", //
    "visual/scenes/", //
    "visual/shots/",  //
    "assets/",        //
    "assets/refs/",   //
    "assets/docs/",   //
    "assets/fonts/",  //
    "output/",        //
    "output/images/", //
    "output/videos/", //
    "output/final/",  //
    "flows/",         //
    "cache/",         //
    "logs/",          //
};

// novel/film 的预置设定模板（API.md §4）。内容克制：只给小节骨架与一行填写提示，
// 不替用户写设定 —— 预置内容一旦"有想法"，用户要么删要么改，反而碍事。
constexpr std::string_view kWorldDoc = R"MD(# 世界观

## 时代
（故事发生在什么年代、什么技术水平，一句话写清）

## 地理
（主要地点与它们的空间关系，各一行）

## 力量体系
（规则、代价与上限；没有超自然力量就写「无」并说明替代约束）

## 社会
（主要阵营、阶层与核心矛盾，各一行）
)MD";

constexpr std::string_view kPeopleDoc = R"MD(# 人物

## 主角
（姓名 / 想要什么 / 害怕什么 / 致命缺陷）

## 对手
（与主角的直接冲突是什么）

## 关键配角
（与主角的关系 + 各自的目标）

## 人物关系
（一句话概括主要人物之间的张力）
)MD";

constexpr std::string_view kOutlineDoc = R"MD(# 大纲

## 一句话创意
（谁、想要什么、为什么难）

## 开端
（激励事件，一两句）

## 中段
（升级与反转，一两句）

## 结局
（最终对决与余韵，一两句）

## 分卷规划
（每卷一行）
)MD";

// 相对路径 → 预置正文
constexpr std::pair<const char*, std::string_view> kDocPresets[] = {
    {"assets/docs/世界观.md", kWorldDoc},
    {"assets/docs/人物.md", kPeopleDoc},
    {"assets/docs/大纲.md", kOutlineDoc},
};

// film 的两张预置工作流：相对路径 → 工作流名（正文由 FlowFileBytes 生成）
constexpr std::pair<const char*, std::string_view> kFlowPresets[] = {
    {"flows/分镜图.flow.json", "分镜图"},
    {"flows/镜头视频.flow.json", "镜头视频"},
};

// 预置工作流正文：最小合法 JSON 模型档 {schemaVersion, name, nodes, links}。
// 按"同样规范序列化"实现 —— 与 S1/S2 同规范：固定键序 + 2 空格缩进 + 尾随恰好一个 '\n' +
// 字符串过 util::json::JsonQuote（手写转义是仓库禁令）；空数组写 "[]"。
[[nodiscard]] std::string FlowFileBytes(std::string_view name) {
    std::string s;
    s += "{\n";
    s += "  \"schemaVersion\": 1,\n";
    s += "  \"name\": " + util::json::JsonQuote(name) + ",\n";
    s += "  \"nodes\": [],\n";
    s += "  \"links\": []\n";
    s += "}\n";
    return s;
}

[[nodiscard]] std::string ErrText(const std::error_code& ec) { return util::AcpToUtf8(ec.message()); }

// 空小说库：shine::db::sqlite 开一次即关（一书一库，这里只是把库文件备好）。
[[nodiscard]] std::expected<void, Error> CreateEmptyNovelDb(const std::filesystem::path& dbPath) {
    db::sqlite::Database db;
    db::sqlite::OpenOptions opt;
    opt.path = dbPath;
    opt.create = true;
    if (std::expected<void, db::sqlite::SqliteError> opened = db.Open(opt); !opened) {
        return std::unexpected(Error{"io", "无法创建小说库 " + util::PathToUtf8(dbPath) + "：" + opened.error().message +
                                               "。请检查目录是否可写后重试。"});
    }
    // 踩坑记录（实测）：sqlite 对新库「只 Open 不写」的话，盘上是 **0 字节** —— 库头要等第一次
    // 写事务才落盘。0 字节的 novel.db 与"文件损坏"在盘上无法区分，自检判据也要求 novel.db > 0 字节。
    // 所以做一次「建表又立刻删表」的空写：落下合法库头，最终 sqlite_master 为空，仍是空库。
    if (std::expected<void, db::sqlite::SqliteError> wrote =
            db.Exec("CREATE TABLE bootstrap_tmp(x INTEGER); DROP TABLE bootstrap_tmp;");
        !wrote) {
        return std::unexpected(Error{"io", "小说库初始化写入失败 " + util::PathToUtf8(dbPath) + "：" +
                                               wrote.error().message + "。请检查磁盘空间后重试。"});
    }
    db.Close();
    return {};
}

// 实际盘点：root 下全部产物的相对路径（目录带尾 '/'、统一分隔符为 '/'），排序后与 PreviewTree 比对。
[[nodiscard]] std::vector<std::string> WalkRel(const std::filesystem::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(root, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code typeEc;
        const bool isDir = it->is_directory(typeEc);
        std::string rel = util::PathToUtf8(it->path().lexically_relative(root));
        std::ranges::replace(rel, '\\', '/'); // PathToUtf8 给的是原生分隔符，比对前统一成 '/'
        if (isDir) {
            rel += '/';
        }
        out.push_back(std::move(rel));
    }
    std::sort(out.begin(), out.end());
    return out;
}

[[nodiscard]] std::string JoinBrief(const std::vector<std::string>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size() && i < 6; ++i) {
        if (i != 0) {
            s += ", ";
        }
        s += items[i];
    }
    if (items.size() > 6) {
        s += ", …";
    }
    return s;
}

// 三套模板描述（顺序固定 blank → novel → film，即向导第 1 步的展示顺序）
[[nodiscard]] const std::array<ProjectTemplate, 3>& All() {
    static const std::array<ProjectTemplate, 3> kAll = {{
        ProjectTemplate{"blank", "空白项目", "只有目录骨架，随用随建"},
        ProjectTemplate{"novel", "小说项目", "含世界观/人物/大纲设定模板与空小说库"},
        ProjectTemplate{"film", "影视化项目", "小说项目全部 + 分镜图与镜头视频两张工作流"},
    }};
    return kAll;
}

} // namespace

std::vector<ProjectTemplate> AllTemplates() { return std::vector<ProjectTemplate>{All().begin(), All().end()}; }

const ProjectTemplate* FindTemplate(std::string_view id) {
    for (const ProjectTemplate& t : All()) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

std::expected<void, Error> Materialize(const ProjectSpec& spec, const ProjectRef& ref, const ProjectFile& file) {
    const std::filesystem::path root = ref.rootDir.lexically_normal();

    // 模板差异以 project.json 记录的 templateId 为准（它是项目身份，之后一切读回都按它判定骨架）；
    // 认不出再退 spec（向导入参），再退 blank —— 三道兜底保证永远不会建出"四不像"骨架。
    std::string templateId = file.templateId;
    if (FindTemplate(templateId) == nullptr) {
        templateId = spec.templateId;
    }
    if (FindTemplate(templateId) == nullptr) {
        templateId = "blank";
    }
    const bool novelLike = (templateId == "novel" || templateId == "film");
    const bool film = (templateId == "film");

    // ① 契约目录（API.md §2）。全部 filesystem 调用走 error_code 重载：本模块错误一律
    //    std::expected + Error{io, 中文原因}，不抛异常（文件系统错误在 Windows 上还带 ANSI 乱码，
    //    直接抛出去 UI 根本没法显示）。
    std::error_code ec;
    for (const char* rel : kContractDirs) {
        std::filesystem::create_directories(root / util::PathFromUtf8(rel), ec);
        if (ec) {
            return std::unexpected(Error{"io", "无法创建目录 " + std::string{rel} + "（" + util::PathToUtf8(root) +
                                                   "，" + ErrText(ec) + "）。请检查磁盘空间与写权限后重试。"});
        }
    }

    // ② 模板差异（API.md §4）
    if (novelLike) {
        if (std::expected<void, Error> made = CreateEmptyNovelDb(root / "db" / "novel.db"); !made) {
            return made; // 已是 {"io", 中文原因}
        }
        for (const auto& preset : kDocPresets) {
            if (!util::WriteFileBytes(root / util::PathFromUtf8(preset.first), preset.second)) {
                return std::unexpected(Error{"io", "写入预置设定模板失败：" + std::string{preset.first} +
                                                       "。请检查磁盘空间与写权限后重试。"});
            }
        }
    }
    if (film) {
        for (const auto& preset : kFlowPresets) {
            if (!util::WriteFileBytes(root / util::PathFromUtf8(preset.first), FlowFileBytes(preset.second))) {
                return std::unexpected(Error{"io", "写入预置工作流失败：" + std::string{preset.first} +
                                                       "。请检查磁盘空间与写权限后重试。"});
            }
        }
    }

    // ③ project.json **最后**写：它一落盘这个目录才算"是个项目"（半成品不留元数据）
    return SaveProjectFile(root, file);
}

std::vector<std::string> PreviewTree(std::string_view templateId) {
    const ProjectTemplate* t = FindTemplate(templateId);
    if (t == nullptr) {
        return {}; // 非法 id → 空预览（向导据此把"下一步"置灰）
    }
    const bool novelLike = (t->id == "novel" || t->id == "film");
    const bool film = (t->id == "film");

    std::vector<std::string> out;
    out.reserve(24);
    for (const char* rel : kContractDirs) {
        out.emplace_back(rel);
    }
    if (novelLike) {
        out.emplace_back("db/novel.db"); // 先库、后预置文档 —— 与 Materialize 的创建顺序一致
        for (const auto& preset : kDocPresets) {
            out.emplace_back(preset.first);
        }
    }
    if (film) {
        for (const auto& preset : kFlowPresets) {
            out.emplace_back(preset.first);
        }
    }
    out.emplace_back("project.json"); // 最后写
    return out;
}

// ————————————————————————————————————————————————————— S3 自检

bool TemplateSelfTest(std::string* report) {
    bool ok = true;
    std::string text;
    auto row = [&text, &ok](std::string item, bool pass, std::string_view detail) {
        text += "[S3] ";
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
        std::filesystem::temp_directory_path(ec) / ("shinetv-p03s3-" + util::RandomHex(8));
    if (ec) {
        row("临时目录", false, "拿不到系统临时目录，自检无法进行");
        if (report != nullptr) {
            *report = text;
        }
        return false;
    }
    std::filesystem::create_directories(base, ec);

    for (const ProjectTemplate& t : All()) {
        // 路径带中文：临时根/自检项目/灯语回声-<模板id>（顺带把中文目录全链路跑一遍）
        const std::filesystem::path root = base / "自检项目" / ("灯语回声-" + t.id);
        const bool film = (t.id == "film");

        ProjectSpec spec;
        spec.name = "灯语回声";
        spec.rootDir = root;
        spec.templateId = t.id;
        spec.premise = "自检用一句话创意。";

        ProjectFile file;
        file.id = "prj_selftest_" + t.id;
        file.name = spec.name;
        file.premise = spec.premise;
        file.templateId = t.id;
        file.createdAt = "2026-09-21T10:00:00Z";

        const ProjectRef ref = MakeRef(file, spec.rootDir);
        if (std::expected<void, Error> made = Materialize(spec, ref, file); !made) {
            row(t.id + "-骨架一致", false, made.error().message);
            row(t.id + "-project.json 读回", false, "Materialize 失败，跳过");
            row(t.id + "-novel.db", false, "Materialize 失败，跳过");
            row(t.id + "-cache与logs", false, "Materialize 失败，跳过");
            row(t.id + "-预置文件", false, "Materialize 失败，跳过");
            continue;
        }

        // (a) 实际 walk 出的相对路径集合 == PreviewTree 集合（双向差集为空）
        {
            const std::vector<std::string> actual = WalkRel(root);
            std::vector<std::string> expect = PreviewTree(t.id);
            std::sort(expect.begin(), expect.end());
            std::vector<std::string> extra;
            std::vector<std::string> missing;
            std::set_difference(actual.begin(), actual.end(), expect.begin(), expect.end(),
                                std::back_inserter(extra));
            std::set_difference(expect.begin(), expect.end(), actual.begin(), actual.end(),
                                std::back_inserter(missing));
            const bool pass = extra.empty() && missing.empty();
            const std::string detail =
                pass ? util::FromInt(actual.size()) + " 项与 PreviewTree 逐项一致"
                     : "多出[" + JoinBrief(extra) + "] 缺少[" + JoinBrief(missing) + "]";
            row(t.id + "-骨架一致", pass, detail);
        }

        // (b) project.json 可读回且字段与写入一致
        {
            const std::expected<ProjectFile, Error> back = LoadProjectFile(root);
            const bool pass = back.has_value() && (*back == file);
            std::string detail;
            if (!back.has_value()) {
                detail = back.error().message;
            } else {
                detail = pass ? "字段与写入一致（operator==）" : "读回字段与写入不一致";
            }
            row(t.id + "-project.json 读回", pass, detail);
        }

        // (c) novel/film 有 db/novel.db（>0 字节）；blank 没有
        {
            const std::filesystem::path dbPath = root / "db" / "novel.db";
            std::error_code e1;
            bool pass = false;
            std::string detail;
            if (t.id == "blank") {
                const bool exists = std::filesystem::exists(dbPath, e1);
                pass = !exists;
                detail = pass ? "blank 无 novel.db（符合模板差异）" : "blank 不该有 db/novel.db";
            } else {
                const bool regular = std::filesystem::is_regular_file(dbPath, e1);
                std::error_code e2;
                // 注意 file_size 失败时返回 (uintmax_t)-1（巨大正数）——必须连错误码一起看，
                // 否则 stat 失败会被误判成">0 字节"的 PASS。
                const std::uintmax_t size = regular ? std::filesystem::file_size(dbPath, e2) : 0;
                pass = regular && !e2 && size > 0;
                detail = pass ? "db/novel.db 存在且 >0 字节" : "db/novel.db 缺失或 0 字节";
            }
            row(t.id + "-novel.db", pass, detail);
        }

        // (d) cache/ 与 logs/ 必在（契约不变式：cache 可随时删、logs 落报告）
        {
            std::error_code e1;
            std::error_code e2;
            const bool pass = std::filesystem::is_directory(root / "cache", e1) &&
                              std::filesystem::is_directory(root / "logs", e2);
            row(t.id + "-cache与logs", pass, pass ? "cache/ 与 logs/ 都在" : "缺 cache/ 或 logs/");
        }

        // (e) 模板差异预置文件：blank 全无；novel 三份 docs 齐；film 另有两张合法 JSON 的 flow
        {
            bool pass = true;
            std::string detail;
            if (t.id == "blank") {
                for (const auto& preset : kDocPresets) {
                    std::error_code e;
                    if (std::filesystem::exists(root / util::PathFromUtf8(preset.first), e)) {
                        pass = false;
                        detail = std::string{"blank 不该有 "} + preset.first;
                        break;
                    }
                }
                for (const auto& preset : kFlowPresets) {
                    std::error_code e;
                    if (std::filesystem::exists(root / util::PathFromUtf8(preset.first), e)) {
                        pass = false;
                        detail = std::string{"blank 不该有 "} + preset.first;
                        break;
                    }
                }
                if (pass) {
                    detail = "无预置文件（随用随建）";
                }
            } else {
                for (const auto& preset : kDocPresets) {
                    std::error_code e;
                    if (!std::filesystem::is_regular_file(root / util::PathFromUtf8(preset.first), e)) {
                        pass = false;
                        detail = std::string{"缺少 "} + preset.first;
                        break;
                    }
                }
                if (pass && film) {
                    for (const auto& preset : kFlowPresets) {
                        const std::optional<std::string> bytes =
                            util::ReadFileBytes(root / util::PathFromUtf8(preset.first));
                        util::json::OwnedDoc doc;
                        if (bytes.has_value()) {
                            doc = util::json::ParseDoc(*bytes);
                        }
                        if (!doc || !yyjson_is_obj(doc.root())) {
                            pass = false;
                            detail = std::string{"非法 JSON："} + preset.first;
                            break;
                        }
                    }
                }
                if (pass) {
                    detail = film ? "三份 docs + 两张 flow.json（yyjson 可解析）" : "三份 docs 齐";
                }
            }
            row(t.id + "-预置文件", pass, detail);
        }
    }

    std::filesystem::remove_all(base, ec); // 清场（失败忽略）
    if (report != nullptr) {
        *report = text;
    }
    return ok;
}

} // namespace shine::project
