#include "visual/ReferenceLibrary.h"

#include "media/ExifOrientation.h"
#include "media/MetaProbe.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Random.h"
#include "util/Time.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

#include <yyjson.h>

namespace shine::visual {
namespace {

[[nodiscard]] ReferenceLibraryError Error(std::string message) { return {std::move(message)}; }

[[nodiscard]] std::string ReadString(yyjson_val* object, const char* key) {
    yyjson_val* value = yyjson_obj_get(object, key);
    const char* text = yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
    return text == nullptr ? std::string{} : std::string{text};
}

void AddStringArray(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* key,
                    const std::vector<std::string>& values) {
    yyjson_mut_val* array = yyjson_mut_arr(doc);
    for (const std::string& value : values) {
        yyjson_mut_arr_add_strncpy(doc, array, value.data(), value.size());
    }
    yyjson_mut_obj_add_val(doc, object, key, array);
}

[[nodiscard]] std::vector<std::string> ReadStringArray(yyjson_val* object, const char* key) {
    std::vector<std::string> values;
    yyjson_val* array = yyjson_obj_get(object, key);
    if (!yyjson_is_arr(array)) {
        return values;
    }
    std::size_t index = 0;
    yyjson_val* value = nullptr;
    while ((value = yyjson_arr_get(array, index++)) != nullptr) {
        if (yyjson_is_str(value)) {
            values.emplace_back(yyjson_get_str(value));
        }
    }
    return values;
}

} // namespace

ReferenceLibrary::ReferenceLibrary(std::filesystem::path project_root)
    : project_root_(std::move(project_root)) {}

std::filesystem::path ReferenceLibrary::RefsDir() const {
    return project_root_ / "assets" / "refs";
}

std::filesystem::path ReferenceLibrary::ManifestPath() const {
    return RefsDir() / "refs.json";
}

std::expected<void, ReferenceLibraryError> ReferenceLibrary::Load() {
    images_.clear();
    const std::filesystem::path manifest = ManifestPath();
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        return {};
    }
    const auto bytes = util::ReadFileBytes(manifest);
    if (!bytes) {
        return std::unexpected(Error("读取 refs.json 失败"));
    }
    yyjson_doc* doc = yyjson_read(bytes->data(), bytes->size(), 0);
    if (doc == nullptr) {
        return std::unexpected(Error("refs.json 不是合法 JSON"));
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* items = yyjson_is_obj(root) ? yyjson_obj_get(root, "items") : nullptr;
    if (!yyjson_is_arr(items)) {
        yyjson_doc_free(doc);
        return std::unexpected(Error("refs.json 缺少 items 数组"));
    }
    std::size_t index = 0;
    yyjson_val* item = nullptr;
    while ((item = yyjson_arr_get(items, index++)) != nullptr) {
        if (!yyjson_is_obj(item)) {
            continue;
        }
        ReferenceImage image;
        image.id = ReadString(item, "id");
        image.original_name = ReadString(item, "original_name");
        image.source_path = ReadString(item, "source_path");
        image.rel_path = ReadString(item, "rel_path");
        image.orientation = ReadString(item, "orientation");
        image.raw_width = static_cast<int>(yyjson_get_int(yyjson_obj_get(item, "raw_width")));
        image.raw_height = static_cast<int>(yyjson_get_int(yyjson_obj_get(item, "raw_height")));
        image.display_width = static_cast<int>(yyjson_get_int(yyjson_obj_get(item, "display_width")));
        image.display_height = static_cast<int>(yyjson_get_int(yyjson_obj_get(item, "display_height")));
        image.markers = ReadStringArray(item, "markers");
        image.entity_id = yyjson_get_int(yyjson_obj_get(item, "entity_id"));
        image.asset_id = yyjson_get_int(yyjson_obj_get(item, "asset_id"));
        image.imported_at = yyjson_get_int(yyjson_obj_get(item, "imported_at"));
        if (!image.id.empty() && !image.rel_path.empty()) {
            images_.push_back(std::move(image));
        }
    }
    yyjson_doc_free(doc);
    std::ranges::sort(images_, {}, &ReferenceImage::imported_at);
    std::ranges::reverse(images_);
    return {};
}

std::expected<void, ReferenceLibraryError> ReferenceLibrary::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(RefsDir(), ec);
    if (ec) {
        return std::unexpected(Error("创建 assets/refs 失败：" + util::PathToUtf8(RefsDir())));
    }
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return std::unexpected(Error("创建 refs.json 文档失败"));
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val* items = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "items", items);
    for (const ReferenceImage& image : images_) {
        yyjson_mut_val* item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strncpy(doc, item, "id", image.id.data(), image.id.size());
        yyjson_mut_obj_add_strncpy(doc, item, "original_name", image.original_name.data(),
                                   image.original_name.size());
        yyjson_mut_obj_add_strncpy(doc, item, "source_path", image.source_path.data(),
                                   image.source_path.size());
        yyjson_mut_obj_add_strncpy(doc, item, "rel_path", image.rel_path.data(), image.rel_path.size());
        yyjson_mut_obj_add_strncpy(doc, item, "orientation", image.orientation.data(),
                                   image.orientation.size());
        yyjson_mut_obj_add_int(doc, item, "raw_width", image.raw_width);
        yyjson_mut_obj_add_int(doc, item, "raw_height", image.raw_height);
        yyjson_mut_obj_add_int(doc, item, "display_width", image.display_width);
        yyjson_mut_obj_add_int(doc, item, "display_height", image.display_height);
        AddStringArray(doc, item, "markers", image.markers);
        yyjson_mut_obj_add_int(doc, item, "entity_id", image.entity_id);
        yyjson_mut_obj_add_int(doc, item, "asset_id", image.asset_id);
        yyjson_mut_obj_add_int(doc, item, "imported_at", image.imported_at);
        yyjson_mut_arr_add_val(items, item);
    }
    std::size_t length = 0;
    char* text = yyjson_mut_val_write(root, 0, &length);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        return std::unexpected(Error("序列化 refs.json 失败"));
    }
    const std::string json{text, length};
    std::free(text);
    if (!util::WriteFileBytes(ManifestPath(), json)) {
        return std::unexpected(Error("写入 refs.json 失败"));
    }
    return {};
}

const ReferenceImage* ReferenceLibrary::Find(std::string_view id) const {
    const auto found = std::ranges::find_if(images_, [id](const ReferenceImage& image) {
        return image.id == id;
    });
    return found == images_.end() ? nullptr : &*found;
}

std::expected<ReferenceImage, ReferenceLibraryError>
ReferenceLibrary::Import(const std::filesystem::path& source, std::int64_t entity_id,
                         std::int64_t asset_id,
                         std::vector<std::string> markers) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        return std::unexpected(Error("参考图源文件不存在：" + util::PathToUtf8(source)));
    }
    std::filesystem::create_directories(RefsDir(), ec);
    if (ec) {
        return std::unexpected(Error("创建 assets/refs 失败"));
    }
    ReferenceImage image;
    image.id = "ref_" + util::RandomHex(8);
    image.original_name = util::FileNameToUtf8(source.filename());
    image.source_path = util::PathToUtf8(source.lexically_normal());
    image.entity_id = entity_id;
    image.asset_id = asset_id;
    image.markers = std::move(markers);
    image.imported_at = util::NowMillis() / 1000;

    std::string extension = util::PathToUtf8(source.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const std::filesystem::path destination = RefsDir() / (image.id + extension);
    std::filesystem::copy_file(source, destination, ec);
    if (ec) {
        return std::unexpected(Error("复制参考图失败：" + util::PathToUtf8(source)));
    }
    image.rel_path = util::PathToUtf8(std::filesystem::relative(destination, project_root_, ec));
    if (ec || image.rel_path.empty()) {
        image.rel_path = util::PathToUtf8(destination);
    }

    const gallery::ImageInfo info = gallery::ProbeFile(destination, 0);
    if (!info.valid()) {
        std::filesystem::remove(destination, ec);
        return std::unexpected(Error("无法识别参考图格式：" + image.original_name));
    }
    const gallery::ExifMeta exif = gallery::ReadExif(destination);
    image.orientation = gallery::OrientationLabel(exif.orientation);
    image.display_width = static_cast<int>(info.width);
    image.display_height = static_cast<int>(info.height);
    image.raw_width = gallery::SwapsAxes(exif.orientation) ? image.display_height
                                                        : image.display_width;
    image.raw_height = gallery::SwapsAxes(exif.orientation) ? image.display_width
                                                           : image.display_height;
    images_.insert(images_.begin(), image);
    if (auto saved = Save(); !saved) {
        images_.erase(images_.begin());
        std::filesystem::remove(destination, ec);
        return std::unexpected(saved.error());
    }
    return image;
}

std::expected<void, ReferenceLibraryError>
ReferenceLibrary::Bind(std::string_view id, std::int64_t entity_id, std::int64_t asset_id) {
    auto found = std::ranges::find_if(images_, [id](const ReferenceImage& image) {
        return image.id == id;
    });
    if (found == images_.end()) {
        return std::unexpected(Error("参考图不存在：" + std::string{id}));
    }
    found->entity_id = entity_id;
    found->asset_id = asset_id;
    return Save();
}

std::expected<void, ReferenceLibraryError>
ReferenceLibrary::SetMarkers(std::string_view id, std::vector<std::string> markers) {
    auto found = std::ranges::find_if(images_, [id](const ReferenceImage& image) {
        return image.id == id;
    });
    if (found == images_.end()) {
        return std::unexpected(Error("参考图不存在：" + std::string{id}));
    }
    found->markers = std::move(markers);
    return Save();
}

std::expected<void, ReferenceLibraryError> ReferenceLibrary::Remove(std::string_view id) {
    auto found = std::ranges::find_if(images_, [id](const ReferenceImage& image) {
        return image.id == id;
    });
    if (found == images_.end()) {
        return std::unexpected(Error("参考图不存在：" + std::string{id}));
    }
    const std::filesystem::path path = Resolve(found->rel_path);
    images_.erase(found);
    auto saved = Save();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return saved;
}

std::filesystem::path ReferenceLibrary::Resolve(std::string_view rel_path) const {
    const std::filesystem::path path = util::PathFromUtf8(rel_path);
    return path.is_absolute() ? path : project_root_ / path;
}

} // namespace shine::visual
