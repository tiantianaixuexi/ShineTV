#pragma once
// ui/pages/videoflow/VideoFlowVisualData —— 出片页「画布图 / 首尾帧链 / 视频任务 /
// 成片 / 胶片条」五块的**只读取数**与数据契约（QML 迁移后由 VideoFlowPageModel 使用）。
//
// 为什么抽出来：这五块原先各有一个 QWidget 类（VideoFlowWorkspace / ChainView /
// VideoTaskView / FinalCutView / FilmStrip），**行 → 字段的映射与词表**都写死在各自的
// Rebuild() 里。迁移到 QML 后视图重建，如果照着 Rebuild 再抄一遍映射，立刻就会长出
// 第二份真值（状态词、tone、镜号格式化、链式提示语、导出清单格式各写一份）。
// 收敛到这一份后：真值只在这里算一次，QML 侧只是渲染层。
// （那五个类已随 QML 迁移退役删除，本头是它们留下的唯一真值。）
//
// 纪律（与 AssetVisualData.h 一致）：
//   * 状态词表、tone、镜号/策略的拼装都在这里，**别在 C++ 或 QML 侧各写一份**；
//   * 只读内存里的 flow 真值（VideoChain / BatchRenderQueue）与文件系统存在性，
//     **不造假数据** —— 没有首帧就显示「首帧缺失 / 待出片」，绝不复用上一镜的图；
//   * **不解码像素**：胶片条只回「就绪与否 + 视频路径」，缩略图解码留给渲染层。
#include "flow/BatchRender.h"
#include "flow/VideoChain.h"
#include "util/File.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::app {

// ===================================================================
// ① 画布图：节点 / 连线（webui views.css:1017-1113 的 .fnode / .port / 连线）
// ===================================================================
// ⚠️ 节点几何取自设计稿 `webui/src/data/mock.js` 的 VIDEO_NODES（x 步进 205 /
// y 90 / 150 宽），**不是**迁移前 VideoFlowWorkspace::LoadMock 里那套 180/190 宽的
// 自造坐标 —— 后者是 Widgets 侧为了让 fit() 好看随手排的，与设计稿不符。
// 设计稿出图页与出片页共用同一个 .fnode 组件，所以 QML 侧同样复用同一份组件。
struct VideoGraphNode {
    QString id;
    QString title;
    QString sub;
    QString icon;   // 字符图标（与 kit 的 ◈ / ▤ 同一套字形约定）
    double x = 0;
    double y = 0;
    QString state;  // todo / run / done / fail（FlowCanvas.jsx 的 STATE_COLOR）
};

struct VideoGraphLink {
    QString from;
    QString to;
};

struct VideoGraphFacts {
    std::vector<VideoGraphNode> nodes;
    std::vector<VideoGraphLink> links;
    // 画布是否有图。空态与有图必须**结构上**不同，否则取证的
    // 「空态 / 有图」两张图会逐字节相同（等于少一张证据）。
    [[nodiscard]] bool Empty() const noexcept { return nodes.empty(); }
};

// ===================================================================
// ② 首尾帧链：flow::VideoChain → 行（webui ChainList 的 .dlist/.drow）
// ===================================================================
struct VideoChainRowFact {
    QString fromCode;   // "S01"
    QString toCode;     // "S03"
    QString policy;    // 策略 + 断链时的建议，拼装口径在这里定死
    QString tagText;    // ✔ 已连接 / ⚠ 断链
    QString tagTone;    // ok / warn
    QString detail;     // flow::VideoChainLink::detail（tooltip）
};

[[nodiscard]] inline VideoChainRowFact VideoChainRow(const flow::VideoChainLink& link) {
    VideoChainRowFact row;
    row.fromCode = QStringLiteral("S%1").arg(link.from_shot);
    row.toCode = QStringLiteral("S%1").arg(link.to_shot);
    // connected 时只给策略；断链时补一句「建议按上一镜末帧重生成首帧」。
    row.policy = QString::fromStdString(link.strategy) +
                 (link.connected ? QString()
                                 : QStringLiteral(" · 建议按上一镜末帧重生成首帧"));
    row.tagText = link.connected ? QStringLiteral("✔ 已连接") : QStringLiteral("⚠ 断链");
    row.tagTone = link.connected ? QStringLiteral("ok") : QStringLiteral("warn");
    row.detail = QString::fromStdString(link.detail);
    return row;
}

[[nodiscard]] inline std::vector<VideoChainRowFact> VideoChainRows(const flow::VideoChain& chain) {
    std::vector<VideoChainRowFact> rows;
    rows.reserve(chain.links.size());
    for (const flow::VideoChainLink& link : chain.links) {
        rows.push_back(VideoChainRow(link));
    }
    return rows;
}

// ===================================================================
// ③ 视频任务：flow::BatchRenderQueue 的快照 → 行（webui TaskList）
// ===================================================================
// 状态词表（排队/运行中/完成/失败/降级/已中断）与 tone 的对应是**唯一真值**。
// 迁移前它写在 VideoTaskView::Rebuild 的 if 链里；QML 侧要显示同一批字，
// 就必须回到这里取，不能在 QML 里再写一张对照表。
struct VideoTaskWords {
    const char* text;
    const char* tone;
};

[[nodiscard]] inline VideoTaskWords VideoTaskWordsOf(flow::BatchState state) {
    switch (state) {
        case flow::BatchState::Running:   return {"运行中", "busy"};
        case flow::BatchState::Done:      return {"完成", "ok"};
        case flow::BatchState::Failed:    return {"失败", "danger"};
        case flow::BatchState::Degraded:  return {"降级", "warn"};
        case flow::BatchState::Cancelled: return {"已中断", "idle"};
        case flow::BatchState::Pending:
        default:                          return {"排队", "idle"};
    }
}

struct VideoTaskRowFact {
    QString code;        // 镜号
    QString frameText;   // 首帧就绪 / 首帧缺失
    QString framePath;   // 首帧路径（仅就绪时非空；tooltip 用）
    QString tagText;
    QString tagTone;
    int progress = 0;    // 0–100
    bool run = false;    // 运行中：进度条走 shimmer（设计稿 .prog.run）
    // ⚠️ **没有** barState。迁移前的 VideoTaskView 给 ProgressBar 传过
    // SetState("error"/"idle")，但设计稿 ui.css:430-453 的 .prog 只有
    // base / .run / .thin 三档，**没有** .prog.error —— 那是 Widgets 侧
    // 自己发明的变体。QML 侧不给共享 Progress 加一个设计稿没有的档：
    // 失败由那一行的 Tag（tone=danger「失败」）表达，与设计稿一致。
};

// 首帧来源：只按 (shot_id, path) 配对判断存在性，**不在 UI 线程读盘解码**。
[[nodiscard]] inline QString VideoFirstFrameText(std::int64_t shot_id,
                                                 const std::vector<std::pair<std::int64_t,
                                                                              std::filesystem::path>>& frames,
                                                 QString* path_out) {
    for (const auto& [id, path] : frames) {
        if (id != shot_id) {
            continue;
        }
        if (path_out != nullptr) {
            *path_out = QString::fromStdString(path.string());
        }
        return QStringLiteral("首帧就绪");
    }
    return QStringLiteral("首帧缺失");
}

[[nodiscard]] inline VideoTaskRowFact
VideoTaskRow(const flow::BatchJob& job,
             const std::vector<std::pair<std::int64_t, std::filesystem::path>>& frames) {
    VideoTaskRowFact row;
    row.code = QString::fromStdString(job.label);
    row.frameText = VideoFirstFrameText(job.shot_id, frames, &row.framePath);
    const VideoTaskWords words = VideoTaskWordsOf(job.state);
    // ⚠️ 必须 fromUtf8：词表是中文 UTF-8 字面量，fromLatin1 会渲染成豆腐块
    // （2026-09-30 取证里「运行中」那一格就是这么变成方框的）。
    row.tagText = QString::fromUtf8(words.text);
    row.tagTone = QString::fromLatin1(words.tone); // tone 是 ASCII
    row.progress = job.progress;
    row.run = job.state == flow::BatchState::Running;
    return row;
}

[[nodiscard]] inline std::vector<VideoTaskRowFact>
VideoTaskRows(const flow::BatchRenderQueue& queue,
              const std::vector<std::pair<std::int64_t, std::filesystem::path>>& frames) {
    const auto jobs = queue.Snapshot();
    std::vector<VideoTaskRowFact> rows;
    rows.reserve(jobs.size());
    for (const flow::BatchJob& job : jobs) {
        rows.push_back(VideoTaskRow(job, frames));
    }
    return rows;
}

// ===================================================================
// ④ 成片：镜头视频行 + 概览 KV + 导出清单
// ===================================================================
struct VideoCutRowFact {
    QString code;
    QString path;
    QString tagText;   // 已出片
    QString tagTone;   // ok
};

[[nodiscard]] inline VideoCutRowFact VideoCutRow(std::int64_t shot_id, const QString& path) {
    VideoCutRowFact row;
    row.code = QStringLiteral("S%1").arg(shot_id);
    row.path = path;
    row.tagText = QStringLiteral("已出片");
    row.tagTone = QStringLiteral("ok");
    return row;
}

// 概览卡三行（webui CutPanel 的 .kv）。**只写真实数据**：
//   可连播 = 已有产物的镜头数；编码 = 无来源留「—」（不猜）；
//   缺失   = 全部镜头减去已有产物的那些（这个差集是真算出来的，不是写死的文案）。
struct VideoCutKvFact {
    QString key;
    QString value;
    QString valueToken; // text.primary / text.muted / status.danger
};

using VideoShotList = std::vector<std::pair<std::int64_t, QString>>; // (shot_id, 镜号)

[[nodiscard]] inline std::vector<VideoCutKvFact> VideoCutKv(const VideoShotList& videos,
                                                            const VideoShotList& shots) {
    std::vector<VideoCutKvFact> rows;
    rows.push_back({QStringLiteral("可连播"),
                    videos.empty() ? QStringLiteral("—")
                                   : QStringLiteral("%1 个镜头").arg(videos.size()),
                    QStringLiteral("text.primary")});
    rows.push_back({QStringLiteral("编码"), QStringLiteral("—"), QStringLiteral("text.muted")});
    QStringList missing;
    for (const auto& [id, code] : shots) {
        const bool has = std::any_of(videos.begin(), videos.end(),
                                     [id](const auto& entry) { return entry.first == id; });
        if (!has) {
            missing.push_back(code);
        }
    }
    rows.push_back({QStringLiteral("缺失"),
                    missing.isEmpty() ? QStringLiteral("无")
                                      : missing.join(QStringLiteral("、")) + QStringLiteral("（待出片）"),
                    missing.isEmpty() ? QStringLiteral("text.muted") : QStringLiteral("status.danger")});
    return rows;
}

// 导出一场：写 scene_playlist.txt。**格式由本函数定死**，验收按它读。
[[nodiscard]] inline bool VideoExportScene(
    const std::vector<std::pair<std::int64_t, QString>>& videos,
    const std::filesystem::path& output_dir) {
    if (videos.empty()) {
        return false;
    }
    std::string manifest = "shots=" + std::to_string(videos.size()) + "\n";
    for (const auto& [id, path] : videos) {
        manifest += "S" + std::to_string(id) + "=" + path.toStdString() + "\n";
    }
    return shine::util::WriteFileEnsuredDir(output_dir / "scene_playlist.txt", manifest);
}

[[nodiscard]] inline QString VideoCutSummary(const std::vector<std::pair<std::int64_t, QString>>& videos) {
    return videos.empty() ? QStringLiteral("还没有镜头视频")
                          : QStringLiteral("%1 个镜头可连播").arg(videos.size());
}

// ===================================================================
// ⑤ 胶片条：按章连排的镜头格（webui float-strip 的 .film-cell）
// ===================================================================
struct VideoFilmCellFact {
    QString code;
    QString duration; // 空 = 未出片
    bool ready = false;
    QString videoPath;
    // ⚠️ **没有缩略图字段**。迁移前的 FilmStrip::Cell 有个 QImage thumb，
    // 但 LoadMock 从不填它，页面一直在显示「无首帧 / 待出片」空态。设计稿里
    // 用 <Art seed> 画程序化占位图，那是**假缩略**；成片列表最怕看错镜头，
    // 所以这里坚持：没有首帧就是没有，不拿别的镜头的图凑数。
};

// ===================================================================
// 演示数据集（P08 取证 + 验收场景用）
// ===================================================================
// ⚠️ 这是**演示数据**，不是产品真值：产品路径上只有 SetContext 绑定项目，
// 五块内容在没有真库查询前都是空的（迁移前的 VideoFlowWorkspace 也一样）。
// 取证需要一个「有内容」的确定状态，所以显式做成一份可复现的数据集，
// 并且**只有 loadMock() 动作会灌它** —— 打开页面不会自动灌，避免「看着有
// 数据其实全是编的」。这与 FlowCanvas 的既有做法同源。
struct VideoDemoSet {
    VideoGraphFacts graph;
    std::vector<flow::VideoFrameState> shots;                            // 首尾帧源
    std::vector<std::pair<std::int64_t, QString>> taskShots;            // (shot_id, 镜号)
    std::vector<std::pair<std::int64_t, QString>> videos;               // (shot_id, 产物路径)
    std::vector<VideoFilmCellFact> film;
    QString selectedNodeId;                                             // 初始选中节点
};

// 三个镜头首尾相接（S01 末帧 = S02 首帧，S02 末帧 = S03 首帧），因此链上
// **两条都是通的** —— 取证要的是「全连」与「断链」两个可判定的状态，
// 演示数据本身再带一条断链会让第一张图名不副实。
[[nodiscard]] inline VideoDemoSet VideoDemo() {
    VideoDemoSet demo;
    demo.graph.nodes = {
        {QStringLiteral("v1"), QStringLiteral("首帧图像"), QStringLiteral("S01 → H3"),
         QStringLiteral("▣"), 30, 90, QStringLiteral("done")},
        {QStringLiteral("v2"), QStringLiteral("H3 视频生成"), QStringLiteral("24fps · 112 帧"),
         QStringLiteral("◈"), 235, 90, QStringLiteral("run")},
        {QStringLiteral("v3"), QStringLiteral("RIFE 插帧"), QStringLiteral("×2 → 48fps"),
         QStringLiteral("∿"), 440, 90, QStringLiteral("todo")},
        {QStringLiteral("v4"), QStringLiteral("编码输出"), QStringLiteral("output/videos/S01.mp4"),
         QStringLiteral("▤"), 645, 90, QStringLiteral("todo")},
    };
    demo.graph.links = {{QStringLiteral("v1"), QStringLiteral("v2")},
                         {QStringLiteral("v2"), QStringLiteral("v3")},
                         {QStringLiteral("v3"), QStringLiteral("v4")}};

    demo.shots = {
        {1, "S01_first.png", "S01_last.png", ""},
        {2, "S01_last.png", "S02_last.png", ""},
        {3, "S02_last.png", "S03_last.png", ""},
    };
    demo.taskShots = {{1, QStringLiteral("S01")}, {2, QStringLiteral("S02")}, {3, QStringLiteral("S03")}};
    demo.videos = {{1, QStringLiteral("output/videos/S01.mp4")},
                   {2, QStringLiteral("output/videos/S02.mp4")}};
    // 胶片条：S01 / S02 已出片（**不塞假缩略图** —— 首帧缺失时显示空态），
    // S03 待出片。连播列表最忌讳拿别的镜头的图凑数。
    demo.film = {{QStringLiteral("S01"), QStringLiteral("48fps"), true,
                  QStringLiteral("output/videos/S01.mp4")},
                 {QStringLiteral("S02"), QStringLiteral("48fps"), true,
                  QStringLiteral("output/videos/S02.mp4")},
                 {QStringLiteral("S03"), QString(), false, QString()}};
    demo.selectedNodeId = QStringLiteral("v2");
    return demo;
}

// ===================================================================
// 探针口径（验收按这几串解析，改字段名要同步改 P08Checks / P08Review）
// ===================================================================
[[nodiscard]] inline QString VideoChainProbe(const flow::VideoChain& chain) {
    return QString::fromStdString(chain.Describe());
}

[[nodiscard]] inline QString VideoTaskProbe(const flow::BatchRenderQueue& queue, std::size_t frames) {
    return QStringLiteral("jobs=%1; frames=%2; %3")
        .arg(static_cast<int>(queue.Snapshot().size()))
        .arg(static_cast<int>(frames))
        .arg(QString::fromStdString(queue.Describe()));
}

[[nodiscard]] inline QString
VideoCutProbe(const std::vector<std::pair<std::int64_t, QString>>& videos) {
    return QStringLiteral("videos=%1; export=output/videos").arg(static_cast<int>(videos.size()));
}

[[nodiscard]] inline QString VideoFilmProbe(const std::vector<VideoFilmCellFact>& cells) {
    std::size_t ready = 0;
    for (const VideoFilmCellFact& cell : cells) {
        ready += cell.ready ? 1 : 0;
    }
    return QStringLiteral("cells=%1; ready=%2")
        .arg(static_cast<int>(cells.size()))
        .arg(static_cast<int>(ready));
}

[[nodiscard]] inline QString VideoGraphProbe(const VideoGraphFacts& graph, const QString& selected) {
    return QStringLiteral("nodes=%1; links=%2; selected=%3")
        .arg(static_cast<int>(graph.nodes.size()))
        .arg(static_cast<int>(graph.links.size()))
        .arg(selected.isEmpty() ? QStringLiteral("none") : selected);
}

} // namespace shine::app
