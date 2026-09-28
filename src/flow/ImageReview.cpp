#include "flow/ImageReview.h"

#include "util/File.h"

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace shine::flow {

int ImageReviewReport::PassedCount() const {
    return static_cast<int>(std::count_if(findings.begin(), findings.end(),
                                           [](const ReviewFinding& f) { return f.passed; }));
}

int ImageReviewReport::FailedCount() const { return static_cast<int>(findings.size()) - PassedCount(); }

std::string ImageReviewReport::Describe() const {
    return "通过 " + std::to_string(PassedCount()) + " / " + std::to_string(findings.size()) +
           " 项；失败 " + std::to_string(FailedCount()) + " 项";
}

ImageReviewReport RunImageReview(const ImageReviewInput& input, const ImageReviewFn& reviewer,
                                 const std::string& report_path) {
    ImageReviewError error;
    std::error_code directory_error;
    std::filesystem::create_directories(std::filesystem::path{report_path}.parent_path(), directory_error);
    if (!reviewer) {
        return {.ok = false, .model = "unavailable",
                .findings = {}, .report_path = {}};
    }
    auto result = reviewer(input, error);
    if (!result) return {.ok = false, .model = "unavailable", .findings = {}, .report_path = {}};
    result->ok = result->FailedCount() == 0;
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "shot_id", input.shot_id);
    yyjson_mut_obj_add_strcpy(doc, root, "image", input.image_path.c_str());
    yyjson_mut_obj_add_bool(doc, root, "ok", result->ok);
    yyjson_mut_obj_add_strcpy(doc, root, "model", result->model.c_str());
    yyjson_mut_val* findings = yyjson_mut_arr(doc);
    for (const auto& finding : result->findings) {
        yyjson_mut_val* item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, item, "key", finding.key.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "label", finding.label.c_str());
        yyjson_mut_obj_add_bool(doc, item, "passed", finding.passed);
        yyjson_mut_obj_add_strcpy(doc, item, "detail", finding.detail.c_str());
        yyjson_mut_val* bbox = yyjson_mut_arr(doc);
        for (int value : finding.bbox) yyjson_mut_arr_add_int(doc, bbox, value);
        yyjson_mut_obj_add_val(doc, item, "bbox", bbox);
        yyjson_mut_arr_add_val(findings, item);
    }
    yyjson_mut_obj_add_val(doc, root, "findings", findings);
    char* text = yyjson_mut_write(doc, 0, nullptr);
    const std::string json = text == nullptr ? "{}" : text;
    if (text != nullptr) free(text);
    if (!util::WriteFileBytes(report_path, json)) {
        result->ok = false;
        result->report_path.clear();
    } else {
        result->report_path = report_path;
    }
    yyjson_mut_doc_free(doc);
    return *result;
}

} // namespace shine::flow
