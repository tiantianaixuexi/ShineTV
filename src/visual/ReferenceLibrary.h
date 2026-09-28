#pragma once
// P05-S6 项目参考图库：assets/refs/ 文件 + refs.json 元数据；导入、标记、实体/资产绑定。
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>


namespace shine::visual {

struct ReferenceLibraryError {
    std::string message;
};

struct ReferenceImage {
    std::string id;
    std::string original_name;
    std::string source_path;
    std::string rel_path;
    std::string orientation;
    int raw_width = 0;
    int raw_height = 0;
    int display_width = 0;
    int display_height = 0;
    std::int64_t entity_id = 0;
    std::int64_t asset_id = 0;
    std::vector<std::string> markers;
    std::int64_t imported_at = 0;
};

class ReferenceLibrary {
  public:
    explicit ReferenceLibrary(std::filesystem::path project_root);

    [[nodiscard]] std::expected<void, ReferenceLibraryError> Load();
    [[nodiscard]] std::expected<void, ReferenceLibraryError> Save() const;
    [[nodiscard]] const std::vector<ReferenceImage>& Images() const noexcept { return images_; }
    [[nodiscard]] const ReferenceImage* Find(std::string_view id) const;
    [[nodiscard]] std::expected<ReferenceImage, ReferenceLibraryError>
    Import(const std::filesystem::path& source, std::int64_t entity_id = 0,
           std::int64_t asset_id = 0, std::vector<std::string> markers = {});
    [[nodiscard]] std::expected<void, ReferenceLibraryError>
    Bind(std::string_view id, std::int64_t entity_id, std::int64_t asset_id);
    [[nodiscard]] std::expected<void, ReferenceLibraryError>
    SetMarkers(std::string_view id, std::vector<std::string> markers);
    [[nodiscard]] std::expected<void, ReferenceLibraryError> Remove(std::string_view id);

    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept { return project_root_; }
    [[nodiscard]] std::filesystem::path Resolve(std::string_view rel_path) const;

  private:
    [[nodiscard]] std::filesystem::path ManifestPath() const;
    [[nodiscard]] std::filesystem::path RefsDir() const;

    std::filesystem::path project_root_;
    std::vector<ReferenceImage> images_;
};

} // namespace shine::visual
