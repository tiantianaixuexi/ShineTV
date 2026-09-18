// shine::video —— 视频任务执行器（P5.5 S1–S5）
//
// 线程模型（照 `MEMORY.md`「异步任务规范」）：
//   * `Start()` / `Tick()` / 所有状态改动都在 **UI 线程**；
//   * "解析 → 读素材 → 上传 → 编译" 在**一个 worker** 里顺序跑完，中途用 `PostToUi` 上报阶段；
//   * 提交用 `ComfySession::SubmitPromptJson`（回调在 UI 线程）→ 进 `Running`；
//   * `Running` 由 `Tick()` 轮询 `QueueModel`（WS 驱动的既有模型），**不自己解析 WS**；
//   * `/history` 与 `/view` 下载都在 worker，结果 `PostToUi` 回投。
#include "video/VideoTaskRunner.h"

#include "comfy/ComfyHttp.h"
#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Strings.h"
#include "util/Time.h"
#include "video/ApiGraphValidator.h"
#include "video/CharacterAsset.h"
#include "video/MentionResolver.h"

#include <yyjson.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <expected>
#include <map>
#include <system_error>
#include <utility>

namespace shine::video {
namespace {

using util::FromInt;

constexpr std::int64_t kQueueGraceMs = 8000;      // 刚提交后 /queue 可能还没带上该 prompt
constexpr std::int64_t kSilenceMs = 20000;        // 静默 20s（S4）→ 退回 /history
constexpr std::int64_t kHistoryTimeoutMs = 60000; // /history 请求自身超时
constexpr std::int64_t kTotalTimeoutMs = 120 * 60 * 1000; // 总超时（视频天生慢，给到 2 小时）

[[nodiscard]] std::string Pad(std::int64_t value, std::size_t width) {
    std::string text = FromInt(value);
    while (text.size() < width) {
        text.insert(text.begin(), '0');
    }
    return text;
}

// 上传改名用的时间戳后缀：`20260917_013045_123`
[[nodiscard]] std::string StampSuffix() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::floor<std::chrono::seconds>(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &raw);
#else
    local = *std::localtime(&raw);
#endif
    return FromInt(local.tm_year + 1900) + Pad(local.tm_mon + 1, 2) + Pad(local.tm_mday, 2) + "_" +
           Pad(local.tm_hour, 2) + Pad(local.tm_min, 2) + Pad(local.tm_sec, 2) + "_" + Pad(millis, 3);
}

// 节点定义查询（UI 线程调用：`objectInfoNodes_` 只在 UI 线程重建）
[[nodiscard]] const comfy::NodeTypeDef* LookupNodeDef(std::string_view className) {
    return comfy::ComfySession::Instance().FindNodeDef(className);
}

// ComfyUI 的 `/prompt` 400 只给英文原文 → 按关键词补一条**中文可操作建议**（`Doc/RULES-COMFY.md` §12）
[[nodiscard]] std::string Prompt400Hint(std::string_view message) {
    if (message.find("not found") != std::string_view::npos) {
        return "节点类名在当前 ComfyUI 里不存在：检查是否缺自定义节点，或核心版本是否支持该节点";
    }
    if (message.find("value not in list") != std::string_view::npos) {
        return "有输入值不在允许列表里：检查模型名 / 采样器名 / 调度器名的拼写";
    }
    if (message.find("Required input") != std::string_view::npos ||
        message.find("is required") != std::string_view::npos) {
        return "有必填输入没有给值：检查工作流节点的输入名";
    }
    if (message.find("validation") != std::string_view::npos) {
        return "工作流校验失败：检查节点输入名与类型是否与当前 ComfyUI 一致";
    }
    return "提交被拒绝：请检查工作流与当前 ComfyUI 版本是否匹配";
}

} // namespace

const char* VideoTaskPhaseLabel(VideoTaskPhase phase) noexcept {
    switch (phase) {
    case VideoTaskPhase::Idle: return "空闲";
    case VideoTaskPhase::Resolving: return "解析引用";
    case VideoTaskPhase::Uploading: return "上传素材";
    case VideoTaskPhase::Compiling: return "编译工作流";
    case VideoTaskPhase::Submitting: return "提交任务";
    case VideoTaskPhase::Running: return "生成中";
    case VideoTaskPhase::ReadingHistory: return "读取结果";
    case VideoTaskPhase::SavingMedia: return "落盘";
    case VideoTaskPhase::Done: return "完成";
    case VideoTaskPhase::Failed: return "失败";
    }
    return "未知";
}

VideoTaskRunner& VideoTaskRunner::Instance() {
    static VideoTaskRunner runner;
    return runner;
}

GraphCheckResult VideoTaskRunner::CheckAgainstComfyUI(std::string_view apiJson) {
    if (comfy::ComfySession::Instance().ObjectInfoNodeCount() <= 0) {
        log::Warn("尚未拿到 /object_info：跳过图校验（连接 ComfyUI 后会自动带上）");
        GraphCheckResult skipped;
        skipped.ok = true;
        return skipped;
    }
    return ValidateApiGraph(apiJson, &LookupNodeDef);
}

void VideoTaskRunner::Init() { state_ = VideoTaskState{}; }

void VideoTaskRunner::Shutdown() { state_ = VideoTaskState{}; }

bool VideoTaskRunner::Start(VideoJob job) {
    if (state_.Busy()) {
        log::Warn("已有视频任务在跑（{}），忽略新的提交", VideoTaskPhaseLabel(state_.phase));
        return false;
    }
    if (!job.collectUploads || !job.build) {
        log::Error("视频任务缺少生产函数（collectUploads / build）");
        return false;
    }
    job_ = std::move(job);
    state_ = VideoTaskState{};
    state_.phase = VideoTaskPhase::Resolving;
    state_.shotIndex = job_.shotIndex;
    state_.label = job_.label;
    state_.detail = "解析引用与素材…";
    state_.startedMs = util::MonotonicMillis();
    historyRequestedMs_ = 0;
    downloadDone_ = 0;
    pendingMedia_.clear();
    log::Info("视频任务开始：{}（分镜 #{}）", state_.label,
              job_.shotIndex == static_cast<std::size_t>(-1) ? std::string{"-"} : FromInt(job_.shotIndex + 1));

    // 两个生产函数与 baseUrl 在 UI 线程取好再进 worker（避免 worker 读 Session 的成员）
    auto collect = job_.collectUploads;
    auto build = job_.build;
    const std::string baseUrl = comfy::ComfySession::Instance().BaseUrl();
    async::RunOnWorker([this, collect, build, baseUrl]() {
        // ———— Resolving ————
        std::string error;
        std::vector<std::string> files = collect(error);
        if (!error.empty()) {
            async::PostToUi([this, error]() { Fail(error); });
            return;
        }

        // ———— Uploading（改名带时间戳，避免 ComfyUI 按文件名命中旧缓存）————
        const std::size_t total = files.size();
        async::PostToUi([this, total]() {
            state_.phase = VideoTaskPhase::Uploading;
            state_.uploadTotal = total;
            state_.uploadDone = 0;
            state_.detail = "上传素材 0/" + FromInt(total);
        });

        const std::string stamp = StampSuffix();
        std::map<std::string, std::string> uploadedNames;
        const std::string uploadUrl = comfy::BuildApiUrl(baseUrl, "/upload/image");
        std::size_t done = 0;
        for (const std::string& pathUtf8 : files) {
            const std::filesystem::path localPath = util::PathFromUtf8(pathUtf8);
            const std::string fileName = util::PathToUtf8(localPath.filename());
            const std::optional<std::string> bytes = util::ReadFileBytes(localPath);
            if (!bytes) {
                const std::string why = "素材读不出来：" + pathUtf8;
                async::PostToUi([this, why]() { Fail(why); });
                return;
            }
            const std::string wanted = stamp + "_" + fileName;
            const comfy::HttpResponse resp = comfy::HttpUploadImage(uploadUrl, wanted, *bytes, {{"type", "input"}});
            if (!resp.ok) {
                const std::string why = "上传失败（" + fileName + "）：" + (resp.error.empty() ? resp.body : resp.error);
                async::PostToUi([this, why]() { Fail(why); });
                return;
            }
            // ComfyUI 可能按自己的规则改名 → 以响应里的 name 为准，拿不到才用请求名
            std::string actual = wanted;
            if (yyjson_doc* doc = yyjson_read(resp.body.data(), resp.body.size(), 0)) {
                if (const std::string_view name = util::json::GetStr(yyjson_doc_get_root(doc), "name"); !name.empty()) {
                    actual.assign(name);
                }
                yyjson_doc_free(doc);
            }
            uploadedNames.emplace(fileName, actual);
            ++done;
            async::PostToUi([this, done, total]() {
                state_.uploadDone = done;
                state_.detail = "上传素材 " + FromInt(done) + "/" + FromInt(total);
            });
        }

        // ———— Compiling ————
        async::PostToUi([this]() {
            state_.phase = VideoTaskPhase::Compiling;
            state_.detail = "编译工作流（API JSON）…";
        });
        std::string apiJson = build(uploadedNames, error);
        if (!error.empty() || apiJson.empty()) {
            const std::string why = error.empty() ? std::string{"编译失败（内部错误）"} : error;
            async::PostToUi([this, why]() { Fail(why); });
            return;
        }

        // ———— Submitting（回 UI 线程：**先对本机 /object_info 校验**，再调既有异步接口）————
        async::PostToUi([this, apiJson = std::move(apiJson)]() {
            // 为什么必须校验：ComfyUI 的 /prompt 只查"必填缺失"，**写错的输入名会被静默忽略**
            // （`execution.py::validate_inputs`）→ 不校验就可能提交一个"能跑但结果不对"的图。
            // `FindNodeDef` 读的是 UI 线程独占的 object_info 缓存，所以校验必须在这里做。
            const GraphCheckResult check = ValidateApiGraph(apiJson, &LookupNodeDef);
            if (!check.ok) {
                std::string text = "工作流未通过本机 /object_info 校验（共 " + FromInt(check.issues.size()) + " 处）：";
                for (std::size_t i = 0; i < check.issues.size() && i < 5; ++i) {
                    text += "\n· " + check.issues[i].message;
                }
                if (check.issues.size() > 5) {
                    text += "\n· …还有 " + FromInt(check.issues.size() - 5) + " 处（见日志）";
                }
                for (const GraphCheckIssue& issue : check.issues) {
                    log::Warn("图校验失败：节点 {} ({}) 输入「{}」：{}", issue.nodeId, issue.className, issue.inputName,
                              issue.message);
                }
                Fail(text);
                return;
            }
            log::Info("工作流通过 /object_info 校验：{} 个节点、{} 个输入", check.nodeCount, check.inputCount);

            state_.phase = VideoTaskPhase::Submitting;
            state_.detail = "提交给 ComfyUI…";
            comfy::ComfySession::Instance().SubmitPromptJson(apiJson, [this](comfy::PromptSubmitResult res) {
                if (!res.ok) {
                    const std::string raw = res.error.empty() ? std::string{"（服务端没有给出原因）"} : res.error;
                    std::string text = "提交被拒绝：" + raw;
                    if (!res.nodeErrors.empty()) {
                        for (const comfy::NodeError& item : res.nodeErrors) {
                            text += "；节点「" + item.nodeType + "」(id " + item.nodeId + ") 输入「" + item.inputName +
                                    "」：" + item.message + (item.hint.empty() ? std::string{} : ("（" + item.hint + "）"));
                        }
                    }
                    text += "（" + Prompt400Hint(raw) + "）";
                    Fail(text);
                    return;
                }
                AdvanceToRunning(res.promptId);
            });
        });
    });
    return true;
}

bool VideoTaskRunner::StartShot(const VideoProject& project, std::size_t shotIndex,
                                const std::filesystem::path& projectDir,
                                const std::filesystem::path& mediaLibraryDir,
                                std::function<void(const VideoTaskState&)> onFinish) {
    if (shotIndex >= project.shots.size()) {
        log::Warn("分镜下标越界：{}", shotIndex);
        return false;
    }
    const Shot shot = project.shots[shotIndex]; // 工作副本：只读快照，回填靠 onFinish 写回
    VideoJob job;
    job.label = shot.title.empty() ? ("分镜 #" + FromInt(shotIndex + 1)) : shot.title;
    job.shotIndex = shotIndex;
    job.onFinish = std::move(onFinish);

    // ① 解析：拿到最终参考图顺序与首帧图（**素材必须真实存在** —— 上传要读文件）
    job.collectUploads = [shot, projectDir, mediaLibraryDir](std::string& error) -> std::vector<std::string> {
        ResolveRequest req;
        req.shot = shot;
        req.characterDir = CharacterAssetDir(projectDir);
        req.mediaLibraryDir = mediaLibraryDir;
        req.requireFiles = true;
        const ResolveResult resolved = Resolve(req);
        if (!resolved.ok) {
            error = resolved.error;
            return {};
        }
        for (const std::string& warning : resolved.warnings) {
            log::Warn("分镜「{}」引用告警：{}", shot.title, warning);
        }
        std::vector<std::string> files = resolved.orderedImages;
        if (!resolved.shot.firstFramePath.empty() &&
            std::ranges::find(files, resolved.shot.firstFramePath) == files.end()) {
            files.push_back(resolved.shot.firstFramePath);
        }
        return files;
    };

    // ② 编译：**用上传后的名字**编译（原始工程字段一个字都不改）
    const VideoProject projectCopy = project;
    job.build = [projectCopy, projectDir, mediaLibraryDir, shotIndex](
                    const std::map<std::string, std::string>& uploadedNames, std::string& error) -> std::string {
        // 只编这一个分镜：其余分镜不可提交 → 编译器只会为它建链路
        VideoProject single;
        single.name = projectCopy.name;
        single.unetName = projectCopy.unetName;
        single.clipName = projectCopy.clipName;
        single.videoVaeName = projectCopy.videoVaeName;
        single.audioVaeName = projectCopy.audioVaeName;
        single.loraName = projectCopy.loraName;
        single.loraStrength = projectCopy.loraStrength;
        single.fps = projectCopy.fps;
        single.shots.push_back(projectCopy.shots[shotIndex]);

        H3BuildOptions opt;
        opt.project = std::move(single);
        opt.projectDir = projectDir;
        opt.mediaLibraryDir = mediaLibraryDir;
        opt.uploadedNames = uploadedNames;
        const H3BuildResult built = BuildH3Workflow(opt);
        if (!built.ok) {
            error = built.error;
            return {};
        }
        for (const H3BuildWarning& warning : built.warnings) {
            log::Warn("H3 编译告警：{}", warning.text);
        }
        return built.apiJson;
    };
    return Start(std::move(job));
}

void VideoTaskRunner::AdvanceToRunning(std::string promptId) {
    state_.promptId = std::move(promptId);
    state_.phase = VideoTaskPhase::Running;
    state_.progress = 0.f;
    state_.detail = "已提交，等待排队…";
    historyRequestedMs_ = 0;
    log::Info("视频任务已提交：prompt={}", state_.promptId);
}

void VideoTaskRunner::Fail(std::string error) {
    state_.phase = VideoTaskPhase::Failed;
    state_.error = std::move(error);
    state_.detail = state_.error;
    state_.progress = 0.f;
    log::Error("视频任务失败：{}", state_.error);
    FinishBackfill();
}

void VideoTaskRunner::FinishBackfill() {
    if (job_.onFinish) {
        job_.onFinish(state_);
    }
    // 结果已交给 UI，生产函数可以放掉（它们的拷贝仍在 worker 里跑完即销毁）
    job_.collectUploads = nullptr;
    job_.build = nullptr;
}

void VideoTaskRunner::PollQueue() {
    const std::vector<comfy::QueueModel::Row> rows = comfy::ComfySession::Instance().Queue().Snapshot();
    const comfy::QueueModel::Row* row = nullptr;
    for (const comfy::QueueModel::Row& item : rows) {
        if (item.promptId == state_.promptId) {
            row = &item;
            break;
        }
    }
    const std::int64_t now = util::MonotonicMillis();
    if (row == nullptr) {
        // 本地队列里已经没有它了（被 /queue 刷新清掉 / 进程内记录丢失）→ 宽限后走 /history 定论
        if (now - state_.startedMs > kQueueGraceMs) {
            RequestHistory();
        }
        return;
    }

    state_.progress = row->progress;
    switch (row->state) {
    case comfy::TaskState::Pending:
        state_.detail = "排队中（队列剩余 " + FromInt(comfy::ComfySession::Instance().Queue().QueueRemaining()) + "）";
        break;
    case comfy::TaskState::Running:
        state_.detail = row->label.empty() ? std::string{"生成中"} : row->label;
        // S4：静默 20s（WS 没再给该 prompt 事件）→ 退回 /history 定论，**不把它当卡死**
        if (row->lastEventMs > 0 && now - row->lastEventMs > kSilenceMs && now - historyRequestedMs_ > kSilenceMs) {
            log::Warn("视频任务 {} 静默 {}s：退回 /history 查询结论（执行中 ≠ 卡死）", state_.promptId,
                      kSilenceMs / 1000);
            RequestHistory();
        }
        break;
    case comfy::TaskState::Done:
        RequestHistory();
        break;
    case comfy::TaskState::Failed:
        Fail(row->hint.empty() ? (row->error.empty() ? std::string{"服务端执行失败"} : row->error)
                               : (row->error + "（" + row->hint + "）"));
        break;
    case comfy::TaskState::Cancelled:
        // §12.2：已中断 ≠ 失败
        state_.phase = VideoTaskPhase::Idle;
        state_.detail = "已中断（已上传素材不回滚）";
        state_.progress = 0.f;
        log::Info("视频任务已中断：prompt={}", state_.promptId);
        job_.collectUploads = nullptr;
        job_.build = nullptr;
        break;
    }
}

void VideoTaskRunner::RequestHistory() {
    if (state_.phase == VideoTaskPhase::ReadingHistory) {
        return;
    }
    state_.phase = VideoTaskPhase::ReadingHistory;
    state_.detail = "读取输出清单（/history）…";
    historyRequestedMs_ = util::MonotonicMillis();
    comfy::ComfySession::Instance().FetchHistory(100, [this](comfy::HistoryResult result) {
        OnHistory(std::move(result));
    });
}

void VideoTaskRunner::OnHistory(comfy::HistoryResult result) {
    if (!result.ok) {
        Fail("查询 /history 失败：" + result.error);
        return;
    }
    const comfy::HistoryEntry* entry = nullptr;
    for (const comfy::HistoryEntry& item : result.entries) {
        if (item.promptId == state_.promptId) {
            entry = &item;
            break;
        }
    }
    if (entry == nullptr) {
        Fail("任务已完成，但 /history 里找不到 prompt=" + state_.promptId + " 的记录（可能没有接 SaveVideo/SaveImage）");
        return;
    }
    if (entry->failed) {
        Fail(entry->statusText.empty() ? std::string{"服务端历史显示该任务失败"} : ("服务端历史：失败（" + entry->statusText + "）"));
        return;
    }
    if (entry->media.empty()) {
        Fail("任务完成，但历史里没有任何产物（检查工作流末尾是否接了 SaveVideo / SaveImage）");
        return;
    }

    // 视频优先；没有视频就退而取图片（分镜图等场景）
    std::vector<comfy::HistoryMedia> picked;
    for (const comfy::HistoryMedia& media : entry->media) {
        if (media.kind == "video") {
            picked.push_back(media);
        }
    }
    if (picked.empty()) {
        for (const comfy::HistoryMedia& media : entry->media) {
            if (media.kind == "image") {
                picked.push_back(media);
            }
        }
    }
    if (picked.empty()) {
        picked = entry->media; // 兜底：音频/其它也落盘
    }
    state_.phase = VideoTaskPhase::SavingMedia;
    state_.detail = "下载 " + FromInt(picked.size()) + " 个产物…";
    DownloadOutputs(std::move(picked));
}

void VideoTaskRunner::DownloadOutputs(std::vector<comfy::HistoryMedia> media) {
    const std::string baseUrl = comfy::ComfySession::Instance().BaseUrl();
    const std::filesystem::path dir = VideoOutputDir();
    outputDir_ = dir;
    async::RunOnWorker([this, media = std::move(media), baseUrl, dir]() {
        std::vector<std::string> saved;
        std::string error;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        for (const comfy::HistoryMedia& item : media) {
            const std::string url = comfy::BuildViewUrl(baseUrl, item.fileName, item.subfolder, item.type);
            const std::expected<std::string, comfy::HttpError> bytes = comfy::HttpDownloadBinary(url);
            if (!bytes) {
                error = "下载失败（" + item.fileName + "）：" + bytes.error().message;
                break;
            }
            const std::filesystem::path dest = dir / util::PathFromUtf8(item.fileName);
            if (!util::WriteFileBytes(dest, *bytes)) {
                error = "写文件失败：" + util::PathToUtf8(dest);
                break;
            }
            saved.push_back(util::PathToUtf8(dest));
        }
        async::PostToUi([this, saved = std::move(saved), error, dir]() {
            if (!error.empty()) {
                Fail(error);
                return;
            }
            state_.savedFiles = saved;
            state_.phase = VideoTaskPhase::Done;
            state_.progress = 1.f;
            state_.detail = "完成：" + FromInt(saved.size()) + " 个文件 → " + util::PathToUtf8(dir);
            log::Info("视频任务完成：prompt={} → {} 个文件落到 {}", state_.promptId, saved.size(),
                      util::PathToUtf8(dir));
            FinishBackfill();
        });
    });
}

void VideoTaskRunner::Cancel() {
    if (!state_.Busy()) {
        return;
    }
    if (state_.phase == VideoTaskPhase::Running) {
        comfy::ComfySession::Instance().RequestInterruptCurrent();
        state_.detail = "已请求中断，等待服务端确认…";
        return;
    }
    if (state_.phase == VideoTaskPhase::ReadingHistory || state_.phase == VideoTaskPhase::SavingMedia) {
        state_.phase = VideoTaskPhase::Idle;
        state_.detail = "已取消（已上传素材不回滚）";
        job_.collectUploads = nullptr;
        job_.build = nullptr;
        return;
    }
    // 还在 worker 里（解析/上传/编译）：尽力 interrupt + 让后续阶段自己停
    const VideoTaskPhase was = state_.phase;
    comfy::ComfySession::Instance().RequestInterruptCurrent();
    state_.phase = VideoTaskPhase::Idle;
    state_.detail = "已取消（worker 仍在收尾，已上传素材不回滚）";
    job_.collectUploads = nullptr;
    job_.build = nullptr;
    log::Warn("视频任务在「{}」阶段被取消", VideoTaskPhaseLabel(was));
}

void VideoTaskRunner::Acknowledge() {
    if (state_.phase == VideoTaskPhase::Done || state_.phase == VideoTaskPhase::Failed) {
        state_ = VideoTaskState{};
    }
}

void VideoTaskRunner::Tick() {
    if (state_.Busy()) {
        if (state_.phase == VideoTaskPhase::Running) {
            PollQueue();
        } else if (state_.phase == VideoTaskPhase::ReadingHistory &&
                   util::MonotonicMillis() - historyRequestedMs_ > kHistoryTimeoutMs) {
            Fail("查询 /history 超时");
        }
        if (state_.Busy() && util::MonotonicMillis() - state_.startedMs > kTotalTimeoutMs) {
            Fail("任务总超时（" + FromInt(kTotalTimeoutMs / 60000) + " 分钟）");
        }
    }
}

} // namespace shine::video
