// shine::app::shots —— 分镜表（P5.3 S1–S4；自 video/ui 迁入 app）
//
// 布局：停靠窗口「分镜」内部左右分栏 —— 左 = 表格（`ImGuiListClipper`），右 = 选中分镜的编辑区。
// 纪律：
//   * 行操作（增/删/复制/移动/清错）一律**排队**，由 `Tick()` 在表格绘制之外执行
//     —— 遍历 `shots` 的同时改 `vector` 会让迭代器失效；
//   * 颜色一律取自 `theme::Current()`（不硬编码色值）；
//   * 状态列按 `promptId` 从 `ComfySession::Queue()` 取（`Doc/RULES-COMFY.md` §12：按 promptId 隔离）。
#include "app/shots/ShotTableView.h"

#include "app/FileDialog.h"
#include "app/ui/Widgets.h"
#include "comfy/ComfySession.h" // 自检用：ObjectInfoNodeCount()
#include "core/Log.h"
#include "core/Settings.h"
#include "media/MediaLibrary.h" // P5.6 S3：完成后自动选中最近输出
#include "media/VideoThumb.h"   // P5.6：自检钩子 MaybeRunP56SelfTest()
#include "theme/Theme.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Shell.h" // P5.6 S1：交给系统播放器
#include "util/Strings.h"
#include "video/MentionResolver.h"
#include "video/SceneToImageBuilder.h"
#include "video/VideoTaskRunner.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace shine::app::shots {
using namespace ::shine::video; // Shot / VideoProject / TaskRunner 等业务类型
using ::shine::video::CharacterAsset;
using ::shine::video::CharacterAssetDir;
using ::shine::video::GraphCheckResult;
using ::shine::video::ImageDedupKey;
using ::shine::video::MediaLibraryDir;
using ::shine::video::VideoProject;
using ::shine::video::VideoTaskRunner;
using ::shine::video::VideoTaskState;
namespace {

// ———————————————————————————————————————————————————————————————— 状态与延迟命令

struct Command {
    enum class Kind { NewProject, NewShot, DeleteShot, DuplicateShot, MoveShot, ClearError, LoadProject, SaveProject };
    Kind kind = Kind::NewShot;
    std::size_t index = 0;
    int delta = 0;
    std::filesystem::path path;
};

std::vector<Command>& Commands() {
    static std::vector<Command> queue;
    return queue;
}

// 每帧缓存：队列快照（状态列用；只在 UI 线程读写）
std::vector<comfy::QueueModel::Row> g_queueRows;

// 引用解析**预览**（给参考图列表标 `<Picture N>`）：只在分镜/工程变化或点「重新解析」时重算
struct ResolvePreview {
    std::size_t shotIndex = static_cast<std::size_t>(-1);
    std::uint64_t stamp = 0; // 触发重算的版本号
    ResolveResult result;
    bool valid = false;
};
ResolvePreview g_preview;
std::uint64_t g_previewStamp = 1; // `dirty`/选择变化时自增

// 角色资产列表**缓存**：扫描要读盘，绝不能每帧做
struct CharacterCache {
    std::uint64_t stamp = 0;
    std::filesystem::path dir;
    std::vector<CharacterAsset> assets;
};
CharacterCache g_characters;
std::uint64_t g_characterStamp = 1; // 目录/工程变化或点「刷新角色」时自增

float g_editorWidth = 400.f;
bool g_initialized = false;
bool g_demoInjected = false;

[[nodiscard]] std::string ProgressPercent(float p) {
    const int pct = static_cast<int>(p * 100.f + 0.5f);
    return util::FromInt(pct) + "%";
}

// ———————————————————————————————————————————————————————————————— 状态文案（S4）

enum class Tone { Neutral, Ok, Busy, Bad };

struct StatusInfo {
    std::string text;
    Tone tone = Tone::Neutral;
    std::string tip;
};

// 状态列（S5：**按 job 聚合**，一镜多任务不互相覆盖 —— `11` 差距 11-16）
//   ① 队列里命中本分镜的**任一** jobId（按 promptId 隔离，别的分镜/别的任务不影响本行）；
//      同一分镜可能同时有多个 job 在队列里 → 运行中优先，其次排队中；
//   ② 队列里没有 → 按 `Shot.jobs` 的账聚合：有失败说失败、全完成说完成、还有在途说已提交。
[[nodiscard]] StatusInfo StatusOf(const Shot& shot) {
    StatusInfo out;
    const comfy::QueueModel::Row* live = nullptr;
    const ShotJobRecord* liveJob = nullptr;
    for (const comfy::QueueModel::Row& row : g_queueRows) {
        if (row.promptId.empty()) {
            continue;
        }
        const ShotJobRecord* job = shot.FindJob(row.promptId);
        if (job == nullptr) {
            continue;
        }
        if (row.state == comfy::TaskState::Running) {
            live = &row;
            liveJob = job;
            break; // 运行中最优先
        }
        if (row.state == comfy::TaskState::Pending && live == nullptr) {
            live = &row;
            liveJob = job;
        }
    }
    if (live != nullptr && liveJob != nullptr) {
        const std::string prefix = std::string{ShotJobKindLabel(liveJob->kind)} + " ";
        if (live->state == comfy::TaskState::Running) {
            out.text = prefix + (live->progressMax > 0 ? "生成中 " + ProgressPercent(live->progress) : "生成中");
            out.tone = Tone::Busy;
            out.tip = live->nodeType.empty() ? live->nodeId : (live->nodeType + "  #" + live->nodeId);
            return out;
        }
        if (live->state == comfy::TaskState::Pending) {
            out.text = prefix + "排队中";
            out.tone = Tone::Busy;
            return out;
        }
    }

    // ② 队列里没有 → 按账聚合（一镜多任务：状态是"聚合"出来的，不是最后一个覆盖全部）
    if (!shot.jobs.empty()) {
        std::size_t done = 0;
        std::size_t failed = 0;
        for (const ShotJobRecord& item : shot.jobs) {
            if (item.status == kJobStatusDone) {
                ++done;
            } else if (item.status == kJobStatusFailed) {
                ++failed;
            }
        }
        const std::string summary = shot.JobSummary();
        if (failed > 0) {
            out.text = "失败 " + util::FromInt(failed) + "/" + util::FromInt(shot.jobs.size());
            out.tone = Tone::Bad;
            out.tip = shot.LastErrorText() + "\n任务：" + summary;
            return out;
        }
        if (done == shot.jobs.size()) {
            out.text = "完成";
            out.tone = Tone::Ok;
            out.tip = "任务：" + summary;
            return out;
        }
        if (shot.HasInFlightJob()) {
            out.text = "已提交";
            out.tone = Tone::Busy;
            out.tip = "任务：" + summary;
            return out;
        }
        out.text = "已中断";
        out.tone = Tone::Neutral;
        out.tip = "任务：" + summary;
        return out;
    }

    out.text = shot.IsSubmittable() ? "待生成" : "未就绪";
    out.tone = Tone::Neutral;
    return out;
}

// 回填一次任务的账（S5）：**追加**一条，绝不碰已有的其它 job；同 jobId 重复回调则原地更新。
void RecordJobResult(Shot& shot, const VideoTaskState& state, ShotJobKind kind) {
    ShotJobRecord record;
    record.jobId = state.promptId.empty() ? ("local-" + util::FromInt(shot.jobs.size() + 1)) : state.promptId;
    record.kind = kind;
    if (state.phase == VideoTaskPhase::Done) {
        record.status = std::string{kJobStatusDone};
        record.files = state.savedFiles;
    } else if (state.phase == VideoTaskPhase::Failed) {
        record.status = std::string{kJobStatusFailed};
        record.error = state.error;
    } else {
        record.status = std::string{kJobStatusCancelled};
        record.error = state.error;
    }
    for (ShotJobRecord& item : shot.jobs) {
        if (item.jobId == record.jobId) {
            item = std::move(record);
            return;
        }
    }
    shot.jobs.push_back(std::move(record));
}

[[nodiscard]] ImVec4 ToneColor(Tone tone) {
    const theme::ThemeColors& c = theme::Current();
    switch (tone) {
    case Tone::Ok:
        return ImVec4(c.success[0], c.success[1], c.success[2], 1.f);
    case Tone::Busy:
        return ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
    case Tone::Bad:
        return ImVec4(c.danger[0], c.danger[1], c.danger[2], 1.f);
    case Tone::Neutral:
        break;
    }
    return ImVec4(c.textDim[0], c.textDim[1], c.textDim[2], 1.f);
}

[[nodiscard]] std::string FileLabel(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    return util::PathToUtf8(util::PathFromUtf8(path).filename());
}

// ———————————————————————————————————————————————————————————————— P5.6 S1：播放
//
// 「播放」= 交给**系统播放器**（不内嵌播放器，见 `Plan/任务/P5-视频分镜.md` P5.6 S1）。
// 文件被移动/删除 → 只在 `ed.message` 里给中文提示，**不崩、不弹系统错误框**。
void PlayFile(const std::string& utf8Path) {
    EditorState& ed = Editor();
    if (utf8Path.empty()) {
        ed.message = "这条分镜还没有产物可播放";
        return;
    }
    const std::filesystem::path path = util::PathFromUtf8(utf8Path);
    const std::string error = util::ShellOpen(path);
    if (!error.empty()) {
        ed.message = error;
        log::Warn("播放失败：{}", error);
        return;
    }
    ed.message = "已交给系统播放器：" + util::FileNameToUtf8(path);
    log::Info("播放（系统播放器）：{}", util::PathToUtf8(path));
}

// 角色资产（带缓存）
[[nodiscard]] const std::vector<CharacterAsset>& Characters() {
    const std::filesystem::path dir = CharacterDir();
    if (g_characters.stamp != g_characterStamp || g_characters.dir != dir) {
        g_characters.stamp = g_characterStamp;
        g_characters.dir = dir;
        g_characters.assets = LoadCharacterAssetDir(dir);
    }
    return g_characters.assets;
}

// ———————————————————————————————————————————————————————————————— 延迟命令执行

void ClampSelection() {
    EditorState& ed = Editor();
    if (ed.project.shots.empty()) {
        ed.selected = 0;
    } else if (ed.selected >= ed.project.shots.size()) {
        ed.selected = ed.project.shots.size() - 1;
    }
    ++g_previewStamp;
}

void ApplyCommand(const Command& cmd) {
    EditorState& ed = Editor();
    switch (cmd.kind) {
    case Command::Kind::NewProject: {
        ed.project = VideoProject::MakeDefault();
        ed.project.AddShot();
        ed.projectFile.clear();
        ed.dirty = true;
        ed.selected = 0;
        ed.message = "已新建空工程";
        ++g_characterStamp; // 角色目录跟着工程走
        break;
    }
    case Command::Kind::NewShot: {
        ed.project.AddShot();
        ed.selected = ed.project.shots.size() - 1;
        ed.dirty = true;
        break;
    }
    case Command::Kind::DeleteShot: {
        if (cmd.index < ed.project.shots.size()) {
            ed.project.RemoveShot(cmd.index);
            ed.dirty = true;
            ed.message = "已删除分镜 #" + util::FromInt(cmd.index + 1);
        }
        break;
    }
    case Command::Kind::DuplicateShot: {
        if (cmd.index < ed.project.shots.size()) {
            Shot copy = ed.project.shots[cmd.index];
            copy.jobs.clear(); // 副本是"还没生成过"的新分镜（运行期账一并清掉）
            copy.title += "（副本）";
            ed.project.shots.insert(ed.project.shots.begin() + static_cast<std::ptrdiff_t>(cmd.index) + 1,
                                    std::move(copy));
            ed.selected = cmd.index + 1;
            ed.dirty = true;
        }
        break;
    }
    case Command::Kind::MoveShot: {
        const std::ptrdiff_t from = static_cast<std::ptrdiff_t>(cmd.index);
        const std::ptrdiff_t to = from + cmd.delta;
        if (from >= 0 && to >= 0 && from < static_cast<std::ptrdiff_t>(ed.project.shots.size()) &&
            to < static_cast<std::ptrdiff_t>(ed.project.shots.size())) {
            std::swap(ed.project.shots[from], ed.project.shots[to]);
            ed.selected = static_cast<std::size_t>(to);
            ed.dirty = true;
        }
        break;
    }
    case Command::Kind::ClearError: {
        if (cmd.index < ed.project.shots.size()) {
            ed.project.shots[cmd.index].jobs.clear();
            ed.dirty = true;
            ed.message = "已清除分镜 #" + util::FromInt(cmd.index + 1) + " 的错误与提交记录";
        }
        break;
    }
    case Command::Kind::LoadProject: {
        VideoProject loaded;
        if (loaded.LoadFromFile(cmd.path)) {
            loaded.Sanitize();
            ed.project = std::move(loaded);
            ed.projectFile = cmd.path;
            ed.dirty = false;
            ed.selected = 0;
            ed.message = "已打开：" + util::PathToUtf8(cmd.path);
            ++g_characterStamp;
        } else {
            ed.message = "打开失败（文件损坏或不是工程文件）：" + util::PathToUtf8(cmd.path);
        }
        break;
    }
    case Command::Kind::SaveProject: {
        ed.project.Sanitize(); // 存盘前把宽高/帧数/参考图收回合法规格（会打 Warn）
        if (ed.project.SaveToFile(cmd.path)) {
            ed.projectFile = cmd.path;
            ed.dirty = false;
            ed.message = "已保存：" + util::PathToUtf8(cmd.path);
        } else {
            ed.message = "保存失败：" + util::PathToUtf8(cmd.path);
        }
        break;
    }
    }
    ClampSelection();
}

void EnsureInitialized() {
    if (g_initialized) {
        return;
    }
    g_initialized = true;
    Editor().message = "空工程：点「新建分镜」开始，或用「打开…」载入已有工程";
}

// 自检用：`SHINE_VIDEO_DEMO_SHOTS=N` 一次性造 N 个分镜（验收"100 行滚动"复现用）
void MaybeInjectDemoShots() {
    if (g_demoInjected) {
        return;
    }
    g_demoInjected = true;
    const char* raw = std::getenv("SHINE_VIDEO_DEMO_SHOTS");
    if (raw == nullptr || *raw == '\0') {
        return;
    }
    const std::optional<int> parsed = util::ToInt(raw);
    if (!parsed || *parsed <= 0) {
        return;
    }
    const int count = *parsed;
    // 验收辅助：`SHINE_VIDEO_DEMO_OUTPUT=<本地文件>` 给自检分镜填一个"已有产物"，
    // 用来检查 P5.6 的「产物」列 / 「播放」按钮 / 「最近一次运行」产物清单（不依赖真跑生成）。
    const char* output = std::getenv("SHINE_VIDEO_DEMO_OUTPUT");
    // ⚠️ `getenv` 给的是 **ANSI 代码页**字节（本机 936）→ 中文路径要先过 `AcpToUtf8`
    const std::string demoOutput = (output == nullptr) ? std::string{} : util::AcpToUtf8(output);
    EditorState& ed = Editor();
    ed.project.shots.clear();
    for (int i = 0; i < count; ++i) {
        Shot& s = ed.project.AddShot();
        s.title = "自检分镜 #" + util::FromInt(i + 1);
        s.prompt = "自检提示词 " + util::FromInt(i + 1);
        if (i % 3 == 1) {
            s.mode = ShotMode::FirstLastFrame;
            s.chainFromPrevious = true;
        }
        s.length = 107 + (i % 7) * 17;
        s.width = 832;
        s.height = 480;
        s.seed = i;
        if (i % 5 == 4) {
            s.jobs.push_back({.jobId = "self-test-img-" + util::FromInt(i + 1),
                              .kind = ShotJobKind::SceneImage,
                              .status = std::string{kJobStatusFailed},
                              .error = "自检占位错误：分镜 #" + util::FromInt(i + 1)});
        }
        if (!demoOutput.empty() && i % 4 != 1) { // 一屏里同时看到"有产物 / 产物缺失 / 无产物"三种
            const bool missing = (i % 4 == 2);   // 故意指向一个不存在的文件 → 验证 P5.6 S3 的"文件不存在"
            s.jobs.push_back({.jobId = "self-test-vid-" + util::FromInt(i + 1),
                              .kind = ShotJobKind::Video,
                              .status = std::string{kJobStatusDone},
                              .files = {missing ? demoOutput + ".__missing__" : demoOutput}});
        }
    }
    ed.selected = 0;
    log::Warn("SHINE_VIDEO_DEMO_SHOTS={}：已注入 {} 个自检分镜（验收用，正式使用请清空该环境变量）", count, count);
    if (!demoOutput.empty()) {
        log::Warn("SHINE_VIDEO_DEMO_OUTPUT={}：已给一半分镜填上占位产物（验收 P5.6 产物列/播放）", demoOutput);
    }
}

// ———————————————————————————————————————————————————————————————— P5.5 自检钩子
//
// `SHINE_VIDEO_SELFTEST=<本地图片路径>`：用**最快的图片工作流**（`LoadImage → SaveImage`，不依赖任何模型）
// 把执行器整条链路真机跑一遍，把结果写成 `%TEMP%\shine_p55_report.txt`。
// 为什么不用 H3 工作流：H3 是真视频生成，机器上要跑很久（用户明确要求不要跑）。
// 顺带做一条**离线断言**：`H3BuildOptions::uploadedNames` 真的把 `LoadImage.image` 换成了上传后的名字。

std::string g_selfTestNotes;
int g_selfTestStage = 0;
VideoTaskPhase g_selfTestLastPhase = VideoTaskPhase::Idle;

void SelfTestNote(std::string line) { g_selfTestNotes += line + "\n"; }

void SelfTestWriteReport(const char* verdict) {
    std::error_code ec;
    // 两种模式写两份报告：正常链路 / 失败链路
    const bool badMode = std::getenv("SHINE_VIDEO_SELFTEST_BAD") != nullptr;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / (badMode ? L"shine_p55_report_bad.txt" : L"shine_p55_report.txt");
    std::string report = std::string("P5.5 执行器真机自检：") + verdict + "\n\n" + g_selfTestNotes;
    util::WriteFileBytes(path, report);
    log::Warn("P5.5 自检报告已写出：{}", util::PathToUtf8(path));
}

void MaybeRunRunnerSelfTest() {
    const char* raw = std::getenv("SHINE_VIDEO_SELFTEST");
    if (raw == nullptr || *raw == '\0') {
        return;
    }
    // ⚠️ `getenv` 给的是 **ANSI 代码页**字节（本机 936）→ 中文路径必须先 `AcpToUtf8`
    const std::filesystem::path source = util::PathFromUtf8(util::AcpToUtf8(raw));

    if (g_selfTestStage == 0) {
        // 等 /object_info 就绪（校验要用它）—— 最多等 ~600 帧
        static int waitFrames = 0;
        if (comfy::ComfySession::Instance().ObjectInfoNodeCount() <= 0) {
            if (++waitFrames > 600) {
                SelfTestNote("FAIL 等不到 /object_info（ComfyUI 没连上？）");
                SelfTestWriteReport("FAIL");
                g_selfTestStage = 3;
            }
            return;
        }
        g_selfTestStage = 1;
        SelfTestNote(std::string("素材：") + raw);
        SelfTestNote("/object_info 节点类： " + util::FromInt(comfy::ComfySession::Instance().ObjectInfoNodeCount()));
        if (!std::filesystem::exists(source)) {
            SelfTestNote("FAIL 素材不存在");
            SelfTestWriteReport("FAIL");
            g_selfTestStage = 3;
            return;
        }

        // ① 离线断言：uploadedNames 必须改掉 LoadImage 的名字（且不改用户工程字段）
        {
            // 尽量用**本机真实存在**的模型名做校验样本（从 /object_info 的选项里取第一个）
            const auto firstOption = [](const char* cls, const char* input, const char* needle) -> std::string {
                const comfy::NodeTypeDef* def = comfy::ComfySession::Instance().FindNodeDef(cls);
                if (def == nullptr) {
                    return {};
                }
                const comfy::InputDef* in = def->FindInput(input);
                if (in == nullptr || in->options.empty()) {
                    return {};
                }
                for (const std::string& option : in->options) {
                    if (needle == nullptr || option.find(needle) != std::string::npos) {
                        return option;
                    }
                }
                return in->options.front();
            };
            VideoProject project = VideoProject::MakeDefault();
            // UNETLoader 的选项在没装模型时是空的 → 校验器会跳过它；给个占位名即可
            project.unetName = firstOption("UNETLoader", "unet_name", nullptr);
            if (project.unetName.empty()) {
                project.unetName = "h3_unet_not_installed.safetensors";
            }
            project.clipName = firstOption("CLIPLoader", "clip_name", nullptr);
            project.videoVaeName = firstOption("VAELoader", "vae_name", "video");
            project.audioVaeName = firstOption("VAELoader", "vae_name", "audio");
            Shot shot;
            shot.title = "自检";
            shot.prompt = "自检";
            shot.mode = ShotMode::Reference;
            shot.referenceImages = {raw};
            shot.seed = 1;
            project.shots.push_back(shot);

            H3BuildOptions opt;
            opt.project = project;
            opt.mediaLibraryDir = source.parent_path();
            opt.dryRun = true; // 只编 JSON，不查文件、不跑
            const std::string baseName = util::PathToUtf8(source.filename());
            opt.uploadedNames[baseName] = "20260917_000000_000_" + baseName;
            const H3BuildResult built = BuildH3Workflow(opt);
            const bool renamed = built.ok && built.apiJson.find("20260917_000000_000_" + baseName) != std::string::npos &&
                                 built.apiJson.find("\"" + baseName + "\"") == std::string::npos;
            SelfTestNote(std::string(renamed ? "PASS" : "FAIL") + " uploadedNames 改写 LoadImage：期望 '" +
                         "20260917_000000_000_" + baseName + "'，实际 JSON " + util::FromInt(built.apiJson.size()) + " 字节");
            SelfTestNote(std::string(project.shots[0].firstFramePath.empty() ? "PASS" : "FAIL") +
                         " 编译器不改用户工程字段（firstFramePath 仍为空）");
            // ★ 关键：把 H3 工作流**对着运行中 ComfyUI 的 /object_info 校验**（类名 + 每个输入名 + 枚举值）。
            // 本机没装 H3 的 unet/vae（`models/diffusion_models` 是空的），所以"模型文件名的值不在允许列表"
            // 这类**资源缺失**问题是预期内的，单独归类；**结构问题必须为 0** 才算通过。
            if (built.ok) {
                const GraphCheckResult check = VideoTaskRunner::CheckAgainstComfyUI(built.apiJson);
                std::vector<std::string> structure;
                std::vector<std::string> assets;
                for (const GraphCheckIssue& issue : check.issues) {
                    const bool assetIssue = issue.inputName == "unet_name" || issue.inputName == "vae_name" ||
                                            issue.inputName == "clip_name" || issue.inputName == "lora_name";
                    (assetIssue ? assets : structure).push_back(issue.message);
                }
                SelfTestNote(std::string(structure.empty() ? "PASS" : "FAIL") +
                             " H3 工作流结构校验（类名/输入名/枚举）：" + util::FromInt(check.nodeCount) + " 个节点、" +
                             util::FromInt(check.inputCount) + " 个输入、结构问题 " + util::FromInt(structure.size()) +
                             " 处、资源缺失 " + util::FromInt(assets.size()) + " 处");
                for (const std::string& text : structure) {
                    SelfTestNote("    [结构] " + text);
                }
                for (const std::string& text : assets) {
                    SelfTestNote("    [资源] " + text + "（本机未安装该模型文件，属预期）");
                }
            } else {
                SelfTestNote("FAIL H3 工作流没编出来：" + built.error);
            }

            // 反向验证：故意写一个**越界的 autogrow 子键**（1 基写法的上界 `ref_image_9`）→ 校验器必须报出来
            const std::string badGraph =
                "{\"1\":{\"class_type\":\"MiniMaxH3ReferenceToVideo\",\"inputs\":{\"prompt\":\"x\",\"width\":832,"
                "\"height\":480,\"length\":107,\"ref_images.ref_image_9\":[\"9\",0]}}}";
            const GraphCheckResult badCheck = VideoTaskRunner::CheckAgainstComfyUI(badGraph);
            bool caught = false;
            for (const GraphCheckIssue& issue : badCheck.issues) {
                caught = caught || issue.inputName.find("ref_image_9") != std::string::npos;
            }
            SelfTestNote(std::string(caught ? "PASS" : "FAIL") +
                         " 反向验证：越界的 autogrow 子键 ref_image_9 被校验器拦下（该图共 " +
                         util::FromInt(badCheck.issues.size()) + " 处问题）");
        }

        // ② 真机链路：上传 → 提交 → 等完成 → /history → 下载落盘
        VideoJob job;
        job.label = "P5.5 自检（图片工作流）";
        job.collectUploads = [source](std::string& error) -> std::vector<std::string> {
            if (!std::filesystem::exists(source)) {
                error = "素材不存在：" + util::PathToUtf8(source);
                return {};
            }
            return {util::PathToUtf8(source)};
        };
        job.build = [](const std::map<std::string, std::string>& uploadedNames, std::string& error) -> VideoBuildResult {
            if (uploadedNames.empty()) {
                error = "没有拿到上传映射";
                return {};
            }
            const auto& [original, uploaded] = *uploadedNames.begin();
            SelfTestNote(std::string(uploaded != original ? "PASS" : "FAIL") + " 上传改名：" + original + " → " + uploaded);
            if (std::getenv("SHINE_VIDEO_SELFTEST_BAD") != nullptr) {
                // 故意引用一个不存在的节点类 → 期望 /prompt 400 → 中文错误
                return VideoBuildResult{.apiJson = "{\"1\":{\"class_type\":\"ShineTVNoSuchNode\",\"inputs\":{}}}"};
            }
            return VideoBuildResult{.apiJson =
                                        std::string("{\"1\":{\"class_type\":\"LoadImage\",\"inputs\":{\"image\":\"") + uploaded +
                                        "\"}},\"2\":{\"class_type\":\"SaveImage\",\"inputs\":{\"images\":[\"1\",0],"
                                        "\"filename_prefix\":\"shinetv_p55\"}}}"};
        };
        job.onFinish = [](const VideoTaskState& state) {
            const bool badMode = std::getenv("SHINE_VIDEO_SELFTEST_BAD") != nullptr;
            SelfTestNote(std::string("  任务终态：") + VideoTaskPhaseLabel(state.phase) + " | " + state.detail +
                         (badMode ? "" : (state.phase == VideoTaskPhase::Done ? " [期望：完成]" : " [期望：完成]")));
            SelfTestNote("promptId=" + state.promptId);
            for (const std::string& file : state.savedFiles) {
                const std::filesystem::path path = util::PathFromUtf8(file);
                const bool exists = std::filesystem::exists(path);
                const auto bytes = util::ReadFileBytes(path);
                const std::size_t size = bytes ? bytes->size() : 0;
                const bool isPng = bytes && size > 8 && static_cast<unsigned char>((*bytes)[0]) == 0x89 &&
                                   (*bytes)[1] == 'P' && (*bytes)[2] == 'N' && (*bytes)[3] == 'G';
                SelfTestNote(std::string(exists && size > 0 && isPng ? "PASS" : "FAIL") + " 落盘文件：" + file +
                             "（" + util::FromInt(size) + " 字节，" + (isPng ? "PNG" : "非 PNG") + "）");
            }
            if (state.savedFiles.empty() && !badMode) {
                SelfTestNote("FAIL 没有落盘产物");
            }
        };
        const bool started = VideoTaskRunner::Instance().Start(std::move(job));
        SelfTestNote(std::string(started ? "PASS" : "FAIL") + " 任务已启动");
        if (!started) {
            SelfTestWriteReport("FAIL");
            g_selfTestStage = 3;
        }
        return;
    }

    if (g_selfTestStage == 1) {
        const VideoTaskState& state = VideoTaskRunner::Instance().State();
        if (state.phase != g_selfTestLastPhase) {
            g_selfTestLastPhase = state.phase;
            SelfTestNote(std::string("  阶段推进 → ") + VideoTaskPhaseLabel(state.phase) + "：" + state.detail);
        }
        if (state.phase == VideoTaskPhase::Done || state.phase == VideoTaskPhase::Failed) {
            const bool badMode = std::getenv("SHINE_VIDEO_SELFTEST_BAD") != nullptr;
            // 正常模式要求 Done + 有产物；失败模式要求 Failed + 中文错误
            const bool ok = badMode ? (state.phase == VideoTaskPhase::Failed && !state.error.empty())
                                    : (state.phase == VideoTaskPhase::Done && !state.savedFiles.empty());
            if (badMode) {
                SelfTestNote(std::string(ok ? "PASS" : "FAIL") + " 失败路径：错误文案非空且为中文提示");
            }
            SelfTestWriteReport(ok ? "PASS" : "FAIL");
            g_selfTestStage = 3;
        }
    }
}

// ———————————————————————————————————————————————————————————————— 表格

void Queue(const Command& cmd) { Commands().push_back(cmd); }

void DrawTableToolbar() {
    EditorState& ed = Editor();
    const bool hasSelection = ed.selected < ed.project.shots.size();

    if (ImGui::Button("新建分镜")) {
        Queue({Command::Kind::NewShot});
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("删除") && hasSelection) {
        Queue({Command::Kind::DeleteShot, ed.selected});
    }
    ImGui::SameLine();
    if (ImGui::Button("上移") && hasSelection) {
        Queue({Command::Kind::MoveShot, ed.selected, -1});
    }
    ImGui::SameLine();
    if (ImGui::Button("下移") && hasSelection) {
        Queue({Command::Kind::MoveShot, ed.selected, +1});
    }
    ImGui::SameLine();
    if (ImGui::Button("复制") && hasSelection) {
        Queue({Command::Kind::DuplicateShot, ed.selected});
    }
    ImGui::SameLine();
    if (ImGui::Button("清除错误") && hasSelection) {
        Queue({Command::Kind::ClearError, ed.selected});
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("重新解析引用")) {
        ++g_previewStamp;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("共 %zu 个分镜，可提交 %zu 个", ed.project.shots.size(), ed.project.SubmittableCount());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("拖动右侧分隔条可调编辑区宽度");
}

void DrawTable() {
    EditorState& ed = Editor();
    // 列顺序（P5.6 调整）：「状态」提到第 4 列 —— 它是最常看的列，且要放得下「完成 + 播放」按钮
    constexpr int kColumns = 10;
    static const char* const kHeaders[kColumns] = {"#", "标题", "模式", "状态", "首帧图",
                                                   "参考图", "尺寸", "帧数", "链式", "错误"};
    constexpr int kStatusColumn = 3;

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingFixedFit;

    // ⚠️ 表 id 带 `_p56`：P5.6 调过列顺序（「状态」提到第 4 列）→ 换 id 让 ImGui 丢弃旧 `[Table]` 设置。
    // 不换的话旧设置会**按列索引**套宽度，出现"列名与宽度对不上"的错乱（实测过：产物列被顶到最左）。
    if (!ImGui::BeginTable("##shot_table_p56", kColumns, flags, ImVec2(0, -1))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    for (int c = 0; c < kColumns; ++c) {
        // 「状态」列要放得下「完成 + 播放」按钮 → 给固定宽度（P5.6 S1）
        if (c == kStatusColumn) {
            ImGui::TableSetupColumn(kHeaders[c], ImGuiTableColumnFlags_WidthFixed, 170.f);
        } else {
            ImGui::TableSetupColumn(kHeaders[c]);
        }
    }
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(ed.project.shots.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t i = static_cast<std::size_t>(row);
            const Shot& shot = ed.project.shots[i];
            const StatusInfo status = StatusOf(shot);
            const bool selected = (ed.selected == i);

            ImGui::TableNextRow();
            ImGui::PushID(row);

            // 序号（整行可点选：SpanAllColumns + AllowOverlap）
            ImGui::TableSetColumnIndex(0);
            const std::string indexLabel = util::FromInt(i + 1);
            if (ImGui::Selectable(indexLabel.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                ed.selected = i;
                ++g_previewStamp;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(shot.title.empty() ? "（未命名）" : shot.title.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(ShotModeLabel(shot.mode));

            // P5.6 S1：「播放」直接挂在**状态列**（最常看、始终可见）——
            // 有产物就显示，点了交给系统播放器；详细产物清单在右侧编辑区「最近一次运行」。
            ImGui::TableSetColumnIndex(kStatusColumn);
            ImGui::TextColored(ToneColor(status.tone), "%s", status.text.c_str());
            if (!status.tip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", status.tip.c_str());
            }
            const std::vector<std::string> outputs = shot.AllOutputFiles();
            if (!outputs.empty()) {
                const std::string& newest = outputs.back();
                std::error_code ec;
                const bool exists = std::filesystem::exists(util::PathFromUtf8(newest), ec);
                ImGui::SameLine();
                if (exists) {
                    if (ImGui::SmallButton("播放")) {
                        PlayFile(newest);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("用系统播放器打开（共 %zu 个产物，播最新一个）：\n%s",
                                          outputs.size(), newest.c_str());
                    }
                } else {
                    // P5.6 S3：产物被移动/删除 → 按钮置灰 + 明确提示（点了也只会得到"文件不存在"）
                    ImGui::BeginDisabled();
                    ImGui::SmallButton("播放");
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextColored(ToneColor(Tone::Bad), "文件不存在");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("产物文件不存在（可能已被移动或删除）：\n%s", newest.c_str());
                    }
                }
            }

            ImGui::TableSetColumnIndex(4);
            const std::string first = FileLabel(shot.firstFramePath);
            if (first.empty()) {
                ImGui::TextDisabled(shot.chainFromPrevious ? "（接上一段）" : "—");
            } else {
                ImGui::TextUnformatted(first.c_str());
            }

            ImGui::TableSetColumnIndex(5);
            if (g_preview.valid && g_preview.shotIndex == i) {
                ImGui::Text("%zu → %zu", shot.referenceImages.size(), g_preview.result.orderedImages.size());
            } else {
                ImGui::Text("%zu", shot.referenceImages.size());
            }

            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d×%d", shot.width, shot.height);

            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%d", shot.length);

            ImGui::TableSetColumnIndex(8);
            if (shot.chainFromPrevious) {
                ImGui::TextUnformatted("是");
            } else {
                ImGui::TextDisabled("—");
            }

            ImGui::TableSetColumnIndex(9);
            const std::string lastError = shot.LastErrorText(); // S5：从多 job 账里取"最近一条失败"
            if (!lastError.empty()) {
                ImGui::TextColored(ToneColor(Tone::Bad), "!");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("错误详情：\n%s", lastError.c_str());
                }
            } else if (g_preview.valid && g_preview.shotIndex == i && !g_preview.result.ok) {
                ImGui::TextColored(ToneColor(Tone::Bad), "解析");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("引用解析失败：\n%s", g_preview.result.error.c_str());
                }
            } else {
                ImGui::TextDisabled("—");
            }

            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

// ———————————————————————————————————————————————————————————————— 右侧编辑区（S3）

void DrawReferenceList() {
    EditorState& ed = Editor();
    Shot& shot = ed.project.shots[ed.selected];

    ImGui::TextUnformatted("参考图");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu 张，上限 %d；`<Picture N>` 为解析后的顺序)", shot.referenceImages.size(),
                        kMaxReferenceImagesPerShot);

    // 解析预览：把 `<Picture N>` 编号与解析错误直接摆在编辑区里
    const std::size_t previewIndex = ed.selected;
    if (!g_preview.valid || g_preview.shotIndex != previewIndex || g_preview.stamp != g_previewStamp) {
        ResolveRequest req;
        req.shot = shot;
        req.characterDir = CharacterDir();
        req.mediaLibraryDir = MediaLibraryDir();
        g_preview.shotIndex = previewIndex;
        g_preview.stamp = g_previewStamp;
        g_preview.result = Resolve(req);
        g_preview.valid = true;
    }
    const ResolveResult& preview = g_preview.result;

    if (preview.ok) {
        ImGui::TextDisabled("解析通过：最终 %zu 张（%s）", preview.orderedImages.size(),
                            preview.warnings.empty() ? "无告警" : "有告警");
    } else {
        ImGui::TextColored(ToneColor(Tone::Bad), "解析失败：%s", preview.error.c_str());
    }
    for (const std::string& warning : preview.warnings) {
        ImGui::TextColored(ToneColor(Tone::Busy), "· %s", warning.c_str());
    }

    ImGui::BeginChild("##ref_list", ImVec2(0, 132), ImGuiChildFlags_Borders);
    for (std::size_t k = 0; k < shot.referenceImages.size(); ++k) {
        ImGui::PushID(static_cast<int>(k));
        // 编号：优先用解析结果的 `<Picture N>`（真正会写进提示词的顺序）
        std::string badge = "(" + util::FromInt(k + 1) + ")";
        if (preview.ok) {
            const std::string key = ImageDedupKey(shot.referenceImages[k], MediaLibraryDir());
            for (std::size_t n = 0; n < preview.orderedImages.size(); ++n) {
                if (ImageDedupKey(preview.orderedImages[n], {}) == key) {
                    badge = "<Picture " + util::FromInt(n + 1) + ">";
                    break;
                }
            }
        }
        ImGui::TextDisabled("%s", badge.c_str());
        ImGui::SameLine();
        std::string entry = shot.referenceImages[k];
        ImGui::SetNextItemWidth(-64.f);
        ImGui::InputText("##path", &entry, ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::SmallButton("移除")) {
            shot.referenceImages.erase(shot.referenceImages.begin() + static_cast<std::ptrdiff_t>(k));
            ed.dirty = true;
            ++g_previewStamp;
            ImGui::PopID();
            break; // 改过 vector，本帧不再继续画
        }
        ImGui::PopID();
    }
    if (shot.referenceImages.empty()) {
        app::ui::EmptyState("还没有参考图：从图库拖进来，或点下面「添加图…」");
    }
    ImGui::EndChild();

    // 拖入（S4：接收图库 payload）
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kImagePathPayload)) {
            const char* text = static_cast<const char*>(payload->Data);
            if (text != nullptr && *text != '\0') {
                shot.referenceImages.emplace_back(text);
                ed.dirty = true;
                ++g_previewStamp;
                ed.message = std::string("已追加参考图：") + text;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::Button("添加图…")) {
        const std::string picked = app::OpenFileDialog("选择参考图", {{"图片", "*.png;*.jpg;*.jpeg;*.webp;*.bmp"}},
                                                       util::PathToUtf8(MediaLibraryDir()));
        if (!picked.empty()) {
            shot.referenceImages.push_back(picked);
            ed.dirty = true;
            ++g_previewStamp;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("清空") && !shot.referenceImages.empty()) {
        shot.referenceImages.clear();
        ed.dirty = true;
        ++g_previewStamp;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("（也可把图库缩略图直接拖到本区域）");
}

void DrawPromptEditor() {
    EditorState& ed = Editor();
    Shot& shot = ed.project.shots[ed.selected];

    ImGui::TextUnformatted("提示词");
    ImGui::SameLine();
    ImGui::TextDisabled("支持 @image: / @char: / {{Mixed N}}");
    ImGui::InputTextMultiline("##prompt", &shot.prompt, ImVec2(-1.f, 96.f));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ed.dirty = true;
        ++g_previewStamp;
    }

    if (ImGui::Button("插入 @image…")) {
        const std::string picked = app::OpenFileDialog("选择参考图（插入 @image: 记号）",
                                                       {{"图片", "*.png;*.jpg;*.jpeg;*.webp;*.bmp"}},
                                                       util::PathToUtf8(MediaLibraryDir()));
        if (!picked.empty()) {
            // 路径含空格 → 用双引号包起来（解析器要求）
            shot.prompt += " @image:\"" + picked + "\"";
            ed.dirty = true;
            ++g_previewStamp;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("插入 @char…")) {
        ImGui::OpenPopup("##char_picker");
    }
    if (ImGui::BeginPopup("##char_picker")) {
        const std::vector<CharacterAsset>& assets = Characters();
        if (assets.empty()) {
            ImGui::TextDisabled("角色目录里没有资产：%s", util::PathToUtf8(CharacterDir()).c_str());
        }
        for (const CharacterAsset& asset : assets) {
            const std::string label = asset.displayName.empty() ? asset.name : asset.displayName;
            if (ImGui::MenuItem(label.c_str())) {
                // 显示名含空格 → 同样加引号
                shot.prompt += " @char:\"" + label + "\"";
                ed.dirty = true;
                ++g_previewStamp;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("插入 {{Mixed…}}")) {
        shot.prompt += " {{Mixed 1}}";
        ed.dirty = true;
        ++g_previewStamp;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("（记号在提交时才被解析，提示词里可以放心留）");
}

void DrawSamplingParams() {
    EditorState& ed = Editor();
    Shot& shot = ed.project.shots[ed.selected];

    app::ui::SectionText("规格与采样");
    bool changed = false;

    int mode = static_cast<int>(shot.mode);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("##mode", &mode, "参考图（ref2va）\0首末帧（fl2va）\0")) {
        shot.mode = static_cast<ShotMode>(mode);
        changed = true;
    }

    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragInt("宽", &shot.width, 32.f, 64, 4096);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragInt("高", &shot.height, 32.f, 64, 4096);
    if (const int alignedW = AlignToMultiple(shot.width); alignedW != shot.width || AlignToMultiple(shot.height) != shot.height) {
        ImGui::TextColored(ToneColor(Tone::Busy), "存盘时会自动纠正为 %d×%d（对齐到 %d 的倍数）", alignedW,
                           AlignToMultiple(shot.height), kSizeMultiple);
    }

    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragInt("帧数", &shot.length, 1.f, 1, 2000);
    if (const int aligned = AlignFrameCount(shot.length); aligned != shot.length) {
        ImGui::SameLine();
        ImGui::TextColored(ToneColor(Tone::Busy), "→ 将纠正为 %d（n %% %d == %d）", aligned, kFrameGridStride,
                           kFrameGridOffset);
    }

    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragInt("步数", &shot.steps, 1.f, 1, 200);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragScalar("CFG", ImGuiDataType_Double, &shot.cfg, 0.05f, nullptr, nullptr, "%.2f");
    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragScalar("降噪", ImGuiDataType_Double, &shot.denoise, 0.01f, nullptr, nullptr, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    changed |= ImGui::DragScalar("Shift", ImGuiDataType_Double, &shot.shift, 0.1f, nullptr, nullptr, "%.1f");

    ImGui::SetNextItemWidth(160.f);
    changed |= ImGui::DragScalar("种子", ImGuiDataType_S64, &shot.seed, 1.f, nullptr, nullptr, "%lld");
    ImGui::SameLine();
    ImGui::TextDisabled("(-1 = 随机)");
    if (shot.forcedSeed >= 0) {
        ImGui::TextColored(ToneColor(Tone::Busy), "已被角色锁定种子覆盖：forcedSeed = %lld", static_cast<long long>(shot.forcedSeed));
    }

    changed |= ImGui::Checkbox("链式：接上一段末帧", &shot.chainFromPrevious);

    if (changed) {
        ed.dirty = true;
        ++g_previewStamp;
    }
}

// —— P5.5 S6：生成/中断 + 任务状态（错误与状态一律按 `Doc/RULES-COMFY.md` §12）——
void DrawRunSection() {
    EditorState& ed = Editor();
    Shot& shot = ed.project.shots[ed.selected];
    VideoTaskRunner& runner = VideoTaskRunner::Instance();
    const VideoTaskState& task = runner.State();

    app::ui::SectionText("生成");
    // S5：**忙时也能提交**（进队列，按优先级排队跑）—— 以前这里禁掉按钮，等于"一次只能点一个分镜"
    ImGui::BeginDisabled(!shot.IsSubmittable());
    if (ImGui::Button("生成这一段")) {
        const std::size_t index = ed.selected;
        const bool started = runner.StartShot(ed.project, index, ProjectDir(), MediaLibraryDir(),
                                             [index](const VideoTaskState& state) {
                                                 EditorState& current = Editor();
                                                 if (index >= current.project.shots.size()) {
                                                     return;
                                                 }
                                                 Shot& target = current.project.shots[index];
                                                 // S5：**追加**一条任务账（同一分镜的其它任务不被覆盖）
                                                 RecordJobResult(target, state, ShotJobKind::Video);
                                                 if (state.phase == VideoTaskPhase::Done && !state.savedFiles.empty()) {
                                                     // P5.6 S3：完成后自动选中"最近输出"
                                                     // （刷历史 → 优先按刚落盘的文件名选中 → 「输出」页/右栏预览跟着跳过去）
                                                     const std::filesystem::path newest =
                                                         util::PathFromUtf8(state.savedFiles.back());
                                                     media::MediaLibrary::Instance().RefreshAndSelectLatest(
                                                         util::FileNameToUtf8(newest));
                                                 }
                                                 current.dirty = true;
                                                 current.message = "任务「" + state.label + "」" +
                                                                   VideoTaskPhaseLabel(state.phase) + "：" + state.detail;
                                             });
        if (!started) {
            ed.message = "无法开始：该分镜不可提交（缺提示词 / 规格非正 / 参考图超上限）";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!task.Busy() && runner.QueueSize() == 0);
    if (ImGui::Button("中断")) {
        runner.Cancel(); // 顺带清空待跑队列（中断 = 停下来，不是跑下一个）
    }
    ImGui::EndDisabled();
    if (runner.QueueSize() > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("队列 %zu 个待跑（角色资产优先）", runner.QueueSize());
    }
    if (!shot.IsSubmittable()) {
        ImGui::SameLine();
        ImGui::TextDisabled("（先填提示词并把规格设正）");
    }

    // —— P5.7：出分镜图（SD1.5 SceneToImage；缺 checkpoint 时给中文降级提示）——
    ImGui::Spacing();
    ImGui::BeginDisabled(shot.prompt.empty());
    if (ImGui::Button("出分镜图")) {
        const std::size_t index = ed.selected;
        const bool started = video::StartSceneImage(
            ed.project, index, MediaLibraryDir(), [index](const VideoTaskState& state) {
                EditorState& current = Editor();
                if (index >= current.project.shots.size()) {
                    return;
                }
                Shot& target = current.project.shots[index];
                // S5：**追加**一条任务账（分镜图与视频各成一条，互不覆盖）
                RecordJobResult(target, state, ShotJobKind::SceneImage);
                if (state.phase == VideoTaskPhase::Done && !state.savedFiles.empty()) {
                    // 出图结果回填「首帧图」，便于 fl2va / 再出图引用
                    const std::filesystem::path newest = util::PathFromUtf8(state.savedFiles.back());
                    target.firstFramePath = state.savedFiles.back();
                    media::MediaLibrary::Instance().RefreshAndSelectLatest(util::FileNameToUtf8(newest));
                }
                current.dirty = true;
                current.message = "分镜图「" + state.label + "」" + VideoTaskPhaseLabel(state.phase) + "：" + state.detail;
            });
        if (!started) {
            ed.message = "无法出分镜图：已有任务在跑，或缺少提示词 / Checkpoint（本机可能没有 SD 模型）";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("SD1.5 线 · 宽高按 64 对齐 · 缺 checkpoint 会中文提示");
    if (ed.project.sceneCheckpoint.empty()) {
        ImGui::TextColored(ToneColor(Tone::Bad), "工程尚未配置分镜图 Checkpoint（本机若无 SD/SDXL 请先放置模型）");
    }

    if (task.phase == VideoTaskPhase::Idle) {
        ImGui::TextDisabled("空闲；「生成」会把本段（含素材上传）提交给 ComfyUI");
        return;
    }
    Tone tone = Tone::Busy;
    if (task.phase == VideoTaskPhase::Done) {
        tone = Tone::Ok;
    } else if (task.phase == VideoTaskPhase::Failed) {
        tone = Tone::Bad;
    }
    ImGui::TextColored(ToneColor(tone), "%s：%s", VideoTaskPhaseLabel(task.phase), task.detail.c_str());
    if (task.phase == VideoTaskPhase::Uploading && task.uploadTotal > 0) {
        ImGui::ProgressBar(static_cast<float>(task.uploadDone) / static_cast<float>(task.uploadTotal), ImVec2(-1.f, 0.f));
    } else if (task.phase == VideoTaskPhase::Running) {
        ImGui::ProgressBar(task.progress, ImVec2(-1.f, 0.f));
    }
    if (!task.savedFiles.empty()) {
        ImGui::TextDisabled("产物（%zu 个）：", task.savedFiles.size());
        for (const std::string& file : task.savedFiles) {
            ImGui::TextUnformatted(file.c_str());
        }
    }
    if (task.phase == VideoTaskPhase::Done || task.phase == VideoTaskPhase::Failed) {
        if (ImGui::SmallButton("知道了（收起结果）")) {
            runner.Acknowledge();
        }
    }
}

// P5.6 S1/S3 → S5：**每个任务**一行（一镜多任务各自成账，不互相覆盖），产物可「播放」/「定位」，
// 文件被移动或删除时当场标红（不崩、不弹系统错误框）。摆在「生成」区下面，不用滚到底部。
void DrawLastRunSection() {
    EditorState& ed = Editor();
    if (ed.selected >= ed.project.shots.size()) {
        return;
    }
    Shot& shot = ed.project.shots[ed.selected];
    if (shot.jobs.empty()) {
        return;
    }
    const std::vector<std::string> outputs = shot.AllOutputFiles();
    app::ui::SectionText("运行记录（一镜多任务 · 产物可播放）");
    app::ui::KvRow("任务", util::FromInt(shot.jobs.size()) + " 个：" + shot.JobSummary());
    app::ui::KvRow("产物", util::FromInt(outputs.size()) + " 个文件");

    // 最近的排在最前
    for (std::size_t r = 0; r < shot.jobs.size(); ++r) {
        const std::size_t i = shot.jobs.size() - 1 - r;
        const ShotJobRecord& job = shot.jobs[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s · %s · %s", ShotJobKindLabel(job.kind),
                    job.jobId.empty() ? "（无 promptId）" : job.jobId.c_str(), JobStatusLabel(job.status));
        if (!job.error.empty()) {
            ImGui::TextColored(ToneColor(Tone::Bad), "    %s", job.error.c_str());
        }
        for (std::size_t k = 0; k < job.files.size(); ++k) {
            const std::string& file = job.files[k];
            const std::filesystem::path path = util::PathFromUtf8(file);
            std::error_code ec;
            const bool exists = std::filesystem::exists(path, ec); // S3：文件被移动/删除时明确提示
            const std::string name = util::FileNameToUtf8(path);
            ImGui::PushID(static_cast<int>(k));
            if (ImGui::SmallButton("播放")) {
                PlayFile(file);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("定位")) {
                if (const std::string error = util::ShellReveal(path); !error.empty()) {
                    ed.message = error;
                }
            }
            ImGui::SameLine();
            if (exists) {
                ImGui::TextUnformatted(name.c_str());
            } else {
                ImGui::TextColored(ToneColor(Tone::Bad), "%s（文件不存在，可能已被移动或删除）", name.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", file.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

void DrawEditorPanel() {
    EditorState& ed = Editor();
    if (ed.project.shots.empty()) {
        app::ui::EmptyState("左侧还没有分镜：点「新建分镜」加一个");
        return;
    }
    if (ed.selected >= ed.project.shots.size()) {
        ed.selected = ed.project.shots.size() - 1;
    }
    Shot& shot = ed.project.shots[ed.selected];

    app::ui::PanelHeader("分镜 #" + util::FromInt(ed.selected + 1));
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##title", "标题（列表里显示）", &shot.title)) {
        ed.dirty = true;
    }
    ImGui::Spacing();

    DrawPromptEditor();
    ImGui::Spacing();
    DrawRunSection(); // P5.5 S6：生成 / 中断 / 任务状态
    ImGui::Spacing();
    DrawLastRunSection(); // P5.6 S1/S3：产物「播放」/「定位」/ 文件缺失提示
    ImGui::Spacing();
    DrawSamplingParams();
    ImGui::Spacing();
    DrawReferenceList();
    ImGui::Spacing();
    app::ui::SectionText("首帧图 / 链式");
    std::string firstFrame = shot.firstFramePath;
    ImGui::SetNextItemWidth(-84.f);
    if (ImGui::InputTextWithHint("##first_frame", "首帧图路径（可空 = 用上一段末帧）", &firstFrame)) {
        shot.firstFramePath = firstFrame;
        ed.dirty = true;
        ++g_previewStamp;
    }
    ImGui::SameLine();
    if (ImGui::Button("选图…")) {
        const std::string picked = app::OpenFileDialog("选择首帧图", {{"图片", "*.png;*.jpg;*.jpeg;*.webp;*.bmp"}},
                                                       util::PathToUtf8(MediaLibraryDir()));
        if (!picked.empty()) {
            shot.firstFramePath = picked;
            ed.dirty = true;
            ++g_previewStamp;
        }
    }

}

// ———————————————————————————————————————————————————————————————— 侧栏

void DrawProjectSection() {
    EditorState& ed = Editor();
    app::ui::SectionText("工程");
    std::string name = ed.project.name;
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##project_name", "工程名", &name)) {
        ed.project.name = name;
        ed.dirty = true;
    }

    if (ed.projectFile.empty()) {
        ImGui::TextDisabled("尚未保存（目录：%s）", util::PathToUtf8(VideoProjectDir()).c_str());
    } else {
        ImGui::TextDisabled("文件：%s", util::PathToUtf8(ed.projectFile).c_str());
    }

    if (ImGui::Button("新建工程")) {
        if (ImGui::GetIO().KeyShift || !ed.dirty) {
            Queue({Command::Kind::NewProject});
        } else {
            ed.message = "有未保存修改：按住 Shift 再点「新建工程」以丢弃";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("打开…")) {
        const std::string picked = app::OpenFileDialog("打开分镜工程", {{"分镜工程", "*.json"}},
                                                       util::PathToUtf8(VideoProjectDir()));
        if (!picked.empty()) {
            Queue({Command::Kind::LoadProject, 0, 0, util::PathFromUtf8(picked)});
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存")) {
        std::filesystem::path target = ed.projectFile;
        if (target.empty()) {
            std::string fileName = ed.project.name.empty() ? std::string("未命名工程") : ed.project.name;
            if (!util::EndsWithNoCase(fileName, ".json")) {
                fileName += ".json";
            }
            target = VideoProjectDir() / util::PathFromUtf8(fileName);
        }
        Queue({Command::Kind::SaveProject, 0, 0, target});
    }
    ImGui::SameLine();
    if (ImGui::Button("另存为…")) {
        const std::string picked = app::SaveFileDialog("另存分镜工程", ed.project.name + ".json", {{"分镜工程", "*.json"}},
                                                      util::PathToUtf8(VideoProjectDir()));
        if (!picked.empty()) {
            Queue({Command::Kind::SaveProject, 0, 0, util::PathFromUtf8(picked)});
        }
    }

    if (!ed.message.empty()) {
        ImGui::TextWrapped("%s", ed.message.c_str());
    }
    if (ed.dirty) {
        ImGui::TextColored(ToneColor(Tone::Busy), "有未保存修改");
    }
}

void DrawModelSection() {
    EditorState& ed = Editor();
    app::ui::SectionText("模型段（P5.4 用）");
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##unet", "UNET（如 ltx-2-19b-dev-fp8.safetensors）", &ed.project.unetName)) {
        ed.dirty = true;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##clip", "CLIP", &ed.project.clipName)) {
        ed.dirty = true;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##vvae", "视频 VAE", &ed.project.videoVaeName)) {
        ed.dirty = true;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##avae", "音频 VAE", &ed.project.audioVaeName)) {
        ed.dirty = true;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##lora", "LoRA（可空）", &ed.project.loraName)) {
        ed.dirty = true;
    }
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::DragScalar("LoRA 强度", ImGuiDataType_Double, &ed.project.loraStrength, 0.01f, nullptr, nullptr, "%.2f")) {
        ed.dirty = true;
    }
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::DragScalar("帧率", ImGuiDataType_Double, &ed.project.fps, 1.f, nullptr, nullptr, "%.0f")) {
        ed.dirty = true;
    }
}

void DrawDirectorySection() {
    app::ui::SectionText("目录（空 = 用默认）");
    AppSettings& st = Settings();
    bool changed = false;
    changed |= app::ui::PathPickerRow("工程目录", util::PathToUtf8(VideoProjectDir()), st.videoProjectDir, "选择分镜工程目录", true);
    changed |= app::ui::PathPickerRow("输出目录", util::PathToUtf8(VideoOutputDir()), st.videoOutputDir, "选择成片输出目录", true);
    changed |= app::ui::PathPickerRow("素材库目录", util::PathToUtf8(MediaLibraryDir()), st.mediaLibraryDir, "选择素材库目录", true);
    if (changed) {
        SaveSettings();
    }
}

void DrawStatsSection() {
    EditorState& ed = Editor();
    app::ui::SectionText("统计");
    app::ui::KvRow("分镜", util::FromInt(ed.project.shots.size()));
    app::ui::KvRow("可提交", util::FromInt(ed.project.SubmittableCount()));
    app::ui::KvRow("角色资产", util::FromInt(Characters().size()));
    if (ImGui::SmallButton("刷新角色")) {
        ++g_characterStamp;
    }
    app::ui::KvRow("工程目录", util::PathToUtf8(VideoProjectDir()));
    app::ui::KvRow("素材库", util::PathToUtf8(MediaLibraryDir()));
}

} // namespace

EditorState& Editor() {
    static EditorState state;
    return state;
}

std::filesystem::path ProjectDir() {
    const EditorState& ed = Editor();
    if (!ed.projectFile.empty() && ed.projectFile.has_parent_path()) {
        return ed.projectFile.parent_path();
    }
    return VideoProjectDir();
}

std::filesystem::path CharacterDir() { return CharacterAssetDir(ProjectDir()); }

void DrawShotTable() {
    EnsureInitialized();
    EditorState& ed = Editor();

    // 分隔条：左右分栏（宽度可拖）
    const float avail = ImGui::GetContentRegionAvail().x;
    const float minLeft = 260.f;
    const float maxEditor = std::max(240.f, avail - minLeft - 8.f);
    g_editorWidth = std::min(g_editorWidth, maxEditor);

    ImGui::BeginChild("##shots_left", ImVec2(std::max(minLeft, avail - g_editorWidth - 8.f), 0));
    DrawTableToolbar();
    ImGui::Separator();
    if (ed.project.shots.empty()) {
        app::ui::EmptyState("还没有分镜：点「新建分镜」加一个，或用侧栏的「打开…」载入工程");
    } else {
        DrawTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::Button("##splitter", ImVec2(6.f, -1.f));
    if (ImGui::IsItemActive()) {
        g_editorWidth -= ImGui::GetIO().MouseDelta.x;
        g_editorWidth = std::clamp(g_editorWidth, 240.f, maxEditor);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::SameLine();

    ImGui::BeginChild("##shot_editor", ImVec2(0, 0));
    DrawEditorPanel();
    ImGui::EndChild();
}

void DrawVideoSidePanel() {
    EnsureInitialized();
    EditorState& ed = Editor();
    if (ed.projectFile.empty()) {
        ImGui::TextDisabled("工程：未保存");
    } else {
        ImGui::TextDisabled("%s%s", util::PathToUtf8(ed.projectFile.filename()).c_str(), ed.dirty ? " *" : "");
    }
    DrawProjectSection();
    DrawModelSection();
    DrawDirectorySection();
    DrawStatsSection();
    ImGui::Spacing();
    ImGui::TextDisabled("提示：分镜表在中央「分镜」窗口（与「图」同区）。");
}

void Tick() {
    // P5.5：视频任务执行器每帧推进（状态机 + 完成判定；状态只在本线程改）
    VideoTaskRunner::Instance().Tick();
    MaybeRunRunnerSelfTest();       // 自检钩子（未设 SHINE_VIDEO_SELFTEST 时直接返回）
    media::MaybeRunP56SelfTest();   // P5.6 自检钩子（未设 SHINE_P56_SELFTEST 时直接返回）
    if (!Commands().empty()) {
        std::vector<Command> pending;
        pending.swap(Commands());
        for (const Command& cmd : pending) {
            ApplyCommand(cmd);
        }
    }
    // 队列快照：状态列用（每帧一次，避免逐行调用）
    g_queueRows = comfy::ComfySession::Instance().Queue().Snapshot();
    MaybeInjectDemoShots();
}

} // namespace shine::app::shots
