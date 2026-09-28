#pragma once
// shine_core::project —— 项目模板（P03-S3）：空白 / 小说 / 影视化 三套骨架。
//
// 目录契约（API.md §2）三套模板共用；模板差异见 API.md §4：
//   blank：契约目录 + project.json
//   novel：blank + assets/docs/ 预置设定模板（世界观/人物/大纲）+ db/novel.db 空库
//   film ：novel + flows/ 预置两张工作流（分镜图.flow.json、镜头视频.flow.json）
#include "project/Project.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::project {

// 三套模板描述（向导第 1 步数据源）
[[nodiscard]] std::vector<ProjectTemplate> AllTemplates();
[[nodiscard]] const ProjectTemplate* FindTemplate(std::string_view id);

// 生成项目骨架（目录 + 预置文件 + project.json）。目标目录校验见 ValidateSpec。
// 中文名/中文路径全链路 UTF-8（util::File / util::Encoding，禁窄字符 fopen/ifstream）。
[[nodiscard]] std::expected<void, Error> Materialize(const ProjectSpec& spec,
                                                    const ProjectRef& ref,
                                                    const ProjectFile& file);

// 目录树预览（向导第 4 步）：按创建顺序的相对路径列表，目录带尾部 '/'，
// 如 "db/"、"db/novel.db"、"assets/docs/世界观.md"。
[[nodiscard]] std::vector<std::string> PreviewTree(std::string_view templateId);

// S3 自检：三模板骨架逐项符合 API.md §2（在临时目录真实生成后盘点）。
[[nodiscard]] bool TemplateSelfTest(std::string* report);

} // namespace shine::project
