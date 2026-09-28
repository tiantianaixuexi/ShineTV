#pragma once
// shine_core::project —— 项目索引与最近列表（P03-S2）。
//
// 索引文件：%APPDATA%/ShineTVStudio/projects.json（PLAN §4 落点；UTF-8、确定性序列化）。
// 职责：登记已知项目 + 最近打开列表（10 条、封面缩略图、lastOpened）。
// 损坏宽容：文件坏了 → 记一次告警语义（返回 false）+ 重建空索引，不崩（UI 给「重建索引」）。
#include "project/Project.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace shine::project {

// 最近列表：按 lastOpened 降序、按 id 去重、上限 10 条（S2 判据）。
class RecentList {
  public:
    static constexpr std::size_t kMax = 10;

    // 插入或更新（按 id 去重），置顶 + 截断到 kMax
    void Upsert(RecentEntry entry);
    // 从列表移除（**不删项目文件**）
    [[nodiscard]] bool Remove(const std::string& id);
    [[nodiscard]] const std::vector<RecentEntry>& Entries() const { return entries_; }
    [[nodiscard]] std::optional<RecentEntry> Find(const std::string& id) const;
    void Clear() { entries_.clear(); }

  private:
    std::vector<RecentEntry> entries_;
};

class ProjectIndex {
  public:
    ProjectIndex(); // 默认 DefaultIndexFile()
    explicit ProjectIndex(std::filesystem::path indexFile);

    // 读索引。false = 文件缺失或损坏（已回退空索引，调用方可提示「重建索引」）。
    [[nodiscard]] bool Load();
    [[nodiscard]] std::expected<void, Error> Save() const;

    [[nodiscard]] RecentList& Recent() { return recent_; }
    [[nodiscard]] const RecentList& Recent() const { return recent_; }

    // 新建/打开项目后登记（lastOpened = now）
    void Touch(const ProjectRef& ref, const std::filesystem::path& coverThumb = {});
    [[nodiscard]] bool Remove(const std::string& id);
    [[nodiscard]] const std::filesystem::path& IndexFile() const { return indexFile_; }

  private:
    std::filesystem::path indexFile_;
    RecentList recent_;
};

// %APPDATA%/ShineTVStudio/projects.json（APPDATA 取不到 → 当前目录，测试自检不受影响）
[[nodiscard]] std::filesystem::path DefaultIndexFile();

// S2 自检：10 条上限 + 降序 + 去重 + 落盘读回。
[[nodiscard]] bool IndexSelfTest(std::string* report);

} // namespace shine::project
