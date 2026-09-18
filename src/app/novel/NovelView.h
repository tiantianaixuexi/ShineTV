#pragma once
// 小说工作区：打开工程 → 章列表 → 生成本章（P6）+ 样例数据（P7）
#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::app::novel {

void DrawNovelWindow();
void DrawNovelSidePanel();

// P7：向当前工程写入样例设定（人物/地点/伏笔/前两章空壳）
[[nodiscard]] bool SeedSampleProject(const std::filesystem::path& dbPath, std::string* err);

// P7 离线自检：样例库两章上下文一致（mock LLM）
[[nodiscard]] bool RunMvpSelfCheck();

// 前端/消费侧 JSON 数组解析自检（tools_json / identity_layers / enum_json）
[[nodiscard]] bool RunJsonArrayParseSelfCheck();

} // namespace shine::app::novel
