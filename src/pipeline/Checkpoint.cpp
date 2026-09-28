#include "pipeline/Checkpoint.h"

#include "util/File.h"

#include <yyjson.h>

namespace shine::pipeline {

bool Checkpoint::Save(const std::filesystem::path& path) const {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "chapter", chapter);
    yyjson_mut_obj_add_strcpy(doc, root, "next_stage", StageCode(next_stage).c_str());
    yyjson_mut_obj_add_strcpy(doc, root, "input_state_hash", input_state_hash.c_str());
    yyjson_mut_obj_add_strcpy(doc, root, "manifest_path", manifest_path.c_str());
    char* text = yyjson_mut_write(doc, 0, nullptr);
    const std::string json = text == nullptr ? "{}" : text;
    if (text != nullptr) free(text);
    yyjson_mut_doc_free(doc);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return util::WriteFileBytes(path, json);
}

Checkpoint Checkpoint::Load(const std::filesystem::path& path) {
    Checkpoint out;
    const auto bytes = util::ReadFileBytes(path);
    if (!bytes) return out;
    yyjson_doc* doc = yyjson_read(bytes->data(), bytes->size(), 0);
    if (doc == nullptr) return out;
    yyjson_val* root = yyjson_doc_get_root(doc);
    out.chapter = static_cast<int>(yyjson_get_sint(yyjson_obj_get(root, "chapter")));
    const char* stage = yyjson_get_str(yyjson_obj_get(root, "next_stage"));
    if (stage != nullptr) out.next_stage = StageFromCode(stage);
    const char* hash = yyjson_get_str(yyjson_obj_get(root, "input_state_hash"));
    if (hash != nullptr) out.input_state_hash = hash;
    const char* manifest = yyjson_get_str(yyjson_obj_get(root, "manifest_path"));
    if (manifest != nullptr) out.manifest_path = manifest;
    yyjson_doc_free(doc);
    return out;
}

} // namespace shine::pipeline
