#pragma once
// P07-S11：固定五项视觉评审清单与 generation_checks 报告。
#include <functional>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace shine::flow {

struct ImageReviewInput {
    std::int64_t shot_id = 0;
    std::string image_path;
    std::vector<std::string> reference_paths;
};

struct ReviewFinding {
    std::string key;
    std::string label;
    bool passed = false;
    std::string detail;
    std::vector<int> bbox;
};

struct ImageReviewReport {
    bool ok = false;
    std::string model;
    std::vector<ReviewFinding> findings;
    std::string report_path;
    [[nodiscard]] int PassedCount() const;
    [[nodiscard]] int FailedCount() const;
    [[nodiscard]] std::string Describe() const;
};

struct ImageReviewError {
    std::string code;
    std::string message;
};
using ImageReviewFn = std::function<std::optional<ImageReviewReport>(const ImageReviewInput&,
                                                                       ImageReviewError&)>;

[[nodiscard]] ImageReviewReport RunImageReview(const ImageReviewInput& input,
                                               const ImageReviewFn& reviewer,
                                               const std::string& report_path);

} // namespace shine::flow
