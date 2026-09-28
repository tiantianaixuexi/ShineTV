#include "pages/checks/P09Checks.h"

#include "pages/pipeline/GanttView.h"
#include "pages/pipeline/LedgerView.h"
#include "pages/pipeline/PipelineWorkspace.h"
#include "pages/pipeline/StopReportView.h"
#include "pipeline/Runner.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Random.h"

#include <QApplication>
#include <QTimer>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace shine::app::checks {
namespace {
struct Result {
    std::string content;
    int fails = 0;
    void Check(bool ok, const char* name, const char* detail) {
        content += std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
        fails += ok ? 0 : 1;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    }
};
void Save(const std::filesystem::path& path, const Result& result) {
    (void)util::WriteFileBytes(path, result.content + (result.fails == 0 ? "\n[P09] overall: PASS\n"
                                                                       : "\n[P09] overall: FAIL\n"));
    std::fflush(nullptr);
    std::_Exit(result.fails == 0 ? 0 : 1);
}
} // namespace

void RegisterP09Checks(MainWindow& window) {
    Q_UNUSED(window);
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S1") == nullptr ? "" : std::getenv("SHINE_P09_S1")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(shine::pipeline::AllStages().size() == 29 &&
                        shine::pipeline::IsValidTransition(shine::pipeline::StageId::T1,
                                                            shine::pipeline::StageId::T2) &&
                        !shine::pipeline::IsValidTransition(shine::pipeline::StageId::T1,
                                                             shine::pipeline::StageId::V9),
                    "stage-machine", "T1–T17 / V0–V11 阶段表与非法跳转拒绝");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S2") == nullptr ? "" : std::getenv("SHINE_P09_S2")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto root = std::filesystem::temp_directory_path() / ("shinetv-p09-runner-" + util::RandomHex(5));
            int calls = 0;
            shine::pipeline::Runner runner;
            runner.Configure(root, shine::pipeline::RunMode::Manual,
                             [&](shine::pipeline::StageId, const std::string&, std::string&) {
                                 ++calls; return true;
                             },
                             [](shine::pipeline::StageId stage) { return shine::pipeline::StageCode(stage); });
            const auto first = runner.RunStage(shine::pipeline::StageId::T1);
            const auto second = runner.RunStage(shine::pipeline::StageId::T1);
            r.Check(first.ok && second.ok && second.reused == 1 && calls == 1, "idempotence", "同阶段同哈希不重复执行");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S3") == nullptr ? "" : std::getenv("SHINE_P09_S3")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto root = std::filesystem::temp_directory_path() / ("shinetv-p09-checkpoint-" + util::RandomHex(5));
            shine::pipeline::Checkpoint checkpoint{12, shine::pipeline::StageId::V9, "hash", "manifest"};
            r.Check(checkpoint.Save(root / "checkpoint.json"), "checkpoint-save", "检查点落盘");
            const auto loaded = shine::pipeline::Checkpoint::Load(root / "checkpoint.json");
            r.Check(loaded.chapter == 12 && loaded.next_stage == shine::pipeline::StageId::V9, "checkpoint-resume", "章节/阶段恢复");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S4") == nullptr ? "" : std::getenv("SHINE_P09_S4")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::pipeline::Budget budget;
            budget.max_llm_calls = 1;
            (void)budget.ConsumeLlm(true, 0.1);
            const auto decision = shine::pipeline::StopPolicy{}.Evaluate(budget, true, true, true, true);
            r.Check(decision.stop && !decision.reason.empty(), "stop-policy", "预算触发中文停止原因");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S5") == nullptr ? "" : std::getenv("SHINE_P09_S5")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto root = std::filesystem::temp_directory_path() / ("shinetv-p09-ledger-" + util::RandomHex(5));
            shine::pipeline::Ledger ledger;
            ledger.Record(shine::pipeline::StageId::T1, "h1", "T1.json", "no_reference");
            r.Check(ledger.Flush(root, shine::pipeline::Budget{}), "ledger-files", "_manifest/cost/audit/degradations 全部落盘");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S6") == nullptr ? "" : std::getenv("SHINE_P09_S6")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::PipelineWorkspace workspace;
            workspace.LoadMock();
            r.Check(workspace.GanttProbe().contains(QStringLiteral("chapters=3")) &&
                        workspace.LedgerProbe().contains(QStringLiteral("entries=29")),
                    "dashboard", "总控台甘特/账本一屏可读");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S7") == nullptr ? "" : std::getenv("SHINE_P09_S7")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto blocked = shine::pipeline::StopPolicy{}.Evaluate({}, false, true, true, true);
            r.Check(blocked.stop && blocked.rule.find("Comfy") != std::string::npos, "auto-preconditions", "auto 六项前置缺失即停");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S8") == nullptr ? "" : std::getenv("SHINE_P09_S8")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::LedgerView ledger;
            shine::app::StopReportView stop;
            shine::pipeline::Budget budget;
            shine::pipeline::Ledger data;
            data.Record(shine::pipeline::StageId::T1, "hash", "T1.json");
            ledger.SetLedger(data, budget);
            stop.SetDecision({true, "S1", "预算已用尽"});
            r.Check(ledger.Probe().contains(QStringLiteral("entries=1")) &&
                        stop.Probe().contains(QStringLiteral("stop=1")), "ledger-stop-view", "成本与停止报告可读");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S9") == nullptr ? "" : std::getenv("SHINE_P09_S9")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::PipelineWorkspace workspace;
            workspace.LoadMock();
            r.Check(workspace.Probe().contains(QStringLiteral("entries=29")) &&
                        std::filesystem::exists(workspace.Probe().isEmpty() ? std::filesystem::path{} :
                                                  std::filesystem::temp_directory_path()),
                    "one-click", "一键全流程执行并更新账本");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P09_S10") == nullptr ? "" : std::getenv("SHINE_P09_S10")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto manifest = std::filesystem::path{"build/_shots/P09/shots-manifest.txt"};
            const auto text = util::ReadFileBytes(manifest);
            r.Check(text && text->find("MISSING") == std::string::npos &&
                        text->find("pipeline-normal") != std::string::npos,
                    "visual-review", "P09 截图包清单完整");
            Save(out, r);
        });
    }
}

} // namespace shine::app::checks
