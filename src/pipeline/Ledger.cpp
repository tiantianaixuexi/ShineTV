#include "pipeline/Ledger.h"

#include "util/File.h"

#include <fstream>
#include <sstream>

namespace shine::pipeline {

void Ledger::Record(StageId stage, std::string input_hash, std::string output_path,
                   std::string degradation) {
    entries_.push_back({stage, std::move(input_hash), std::move(output_path), std::move(degradation)});
}

bool Ledger::Flush(const std::filesystem::path& root, const Budget& budget) const {
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) return false;
    std::ostringstream manifest;
    manifest << "{\n  \"entries\": [\n";
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        manifest << "    {\"stage\":\"" << StageCode(entry.stage) << "\",\"input_hash\":\""
                 << entry.input_hash << "\",\"output\":\"" << entry.output_path << "\"}"
                 << (i + 1 == entries_.size() ? "\n" : ",\n");
    }
    manifest << "  ]\n}\n";
    std::ostringstream cost;
    cost << "{\"llm_calls\":" << budget.llm_calls << ",\"high_quality_calls\":"
         << budget.high_quality_calls << ",\"shots\":" << budget.shots << ",\"estimated_cost\":"
         << budget.cost << "}\n";
    std::ostringstream audit;
    for (const auto& entry : entries_) audit << StageCode(entry.stage) << " " << entry.input_hash << "\n";
    std::ostringstream degradations;
    for (const auto& entry : entries_) {
        if (!entry.degradation.empty()) degradations << entry.degradation << "\n";
    }
    return util::WriteFileBytes(root / "_manifest.json", manifest.str()) &&
           util::WriteFileBytes(root / "cost_report.json", cost.str()) &&
           util::WriteFileBytes(root / "audit_logs.txt", audit.str()) &&
           util::WriteFileBytes(root / "degradations.jsonl", degradations.str());
}

} // namespace shine::pipeline
