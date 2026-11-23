// shine::pages —— 节点式视频工作流（第 8 个工作区，P5.7）实现
//
// 类声明在 WorkspacePages.h（与其它六个页面同一约定：页面类集中声明，
// 这里只写实现）。设计依据 refactor/workflow-ui-plan.md §4。
#include "ui/imgui/pages/WorkspacePages.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// 与其它页面同约定：Rect 直接用（外壳布局是 CSS 绝对定位式，见 kit/Widgets.h 顶部）。
// ⚠️ 必须写全 `shine::kit::Rect`：本文件在全局作用域，`kit` 这个名字在这里不存在
//    （`kit::Rect` 只在 shine:: 命名空间内部才能靠兄弟查找解析）。
using shine::kit::Rect;

namespace shine::pages {
namespace {

// ============================================================
// 节点模型
// ============================================================

enum WfState { WfTodo = 0, WfBlocked, WfQueued, WfRunning, WfDone, WfFailed, WfSkipped };
enum WfDataType { DTText = 0, DTShots, DTChar, DTImage, DTVideo, DTAudio, DTSrt, DTTimeline };

// 节点尺寸（webui 的 .fnode 是 184×88，比既有 .fnode 的 150×76 大 —— 因为要多端口）
constexpr float kNodeW = 184.0f;
constexpr float kNodeH = 88.0f;
constexpr float kHeadH = 30.0f;
constexpr float kPanelW = 348.0f;
constexpr float kPortR = 5.5f;
constexpr float kLibraryItemHeight = 22.0f;
constexpr float kLibraryGap = 4.0f;
constexpr float kLibraryPadding = 10.0f;
constexpr float kLibraryHeader = 26.0f;

struct PortDef {
    std::string label;
    int type = DTText;
};

struct FieldDef {
    std::string label;
    int kind = 0; // 0 input · 1 textarea · 2 select · 3 slider · 4 switch · 5 readonly
    std::vector<std::string> options;
    std::string def;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    int rows = 1;
};

struct TypeDef {
    std::string name;
    std::string icon;
    int accent = 0; // 0 accent · 1 ok · 2 busy · 3 warn · 4 info · 5 accent2
    std::vector<PortDef> in;
    std::vector<PortDef> out;
    std::vector<FieldDef> fields;
};

FieldDef FInput(std::string label, std::string def) {
    FieldDef f;
    f.label = std::move(label);
    f.def = std::move(def);
    return f;
}
FieldDef FArea(std::string label, std::string def, int rows) {
    FieldDef f;
    f.label = std::move(label);
    f.kind = 1;
    f.def = std::move(def);
    f.rows = rows;
    return f;
}
FieldDef FSelect(std::string label, std::vector<std::string> options, std::string def) {
    FieldDef f;
    f.label = std::move(label);
    f.kind = 2;
    f.options = std::move(options);
    f.def = std::move(def);
    return f;
}
FieldDef FSlider(std::string label, float mn, float mx, float st, std::string def) {
    FieldDef f;
    f.label = std::move(label);
    f.kind = 3;
    f.min = mn;
    f.max = mx;
    f.step = st;
    f.def = std::move(def);
    return f;
}
FieldDef FSwitch(std::string label, std::string def) {
    FieldDef f;
    f.label = std::move(label);
    f.kind = 4;
    f.def = std::move(def);
    return f;
}
FieldDef FReadonly(std::string label, std::string def) {
    FieldDef f;
    f.label = std::move(label);
    f.kind = 5;
    f.def = std::move(def);
    return f;
}

// 11 种节点类型。顺序与 webui/src/data/workflow.js 的 NODE_TYPES 一致，
// 逐条对照着改（改一处不改另一处 = 设计稿与真机不一致）。
const std::vector<TypeDef>& Types() {
    static const std::vector<TypeDef> kTypes = [] {
        std::vector<TypeDef> v;
        TypeDef t;

        // ① 剧情梗概
        t = TypeDef{"剧情梗概", "sparkles", 0, {},
                    {{"梗概", DTText}},
                    {FInput("作品标题", "灯语回声"),
                     FSelect("题材", {"都市奇幻", "悬疑", "治愈", "科幻", "古装"}, "都市奇幻"),
                     FArea("一句话梗概", "一盏能记住梦的灯，替城市收留失眠的人。", 3),
                     FSelect("目标时长", {"15s", "30s", "60s", "自定义"}, "60s"),
                     FSelect("画幅", {"9:16", "16:9", "1:1"}, "9:16"),
                     FSelect("风格参考", {"治愈系", "赛博", "水墨", "胶片", "暗黑"}, "治愈系"),
                     FInput("受众", "18–30 都市青年")}};
        v.push_back(t);

        // ② 剧本 / 章节
        t = TypeDef{"剧本 / 章节", "book", 0,
                    {{"梗概", DTText}},
                    {{"台词", DTText}},
                    {FSelect("源章节", {"第 1 章", "第 2 章", "第 3 章", "第 4 章"}, "第 3 章"),
                     FSelect("改编方式", {"忠实", "压缩", "扩写"}, "压缩"),
                     FInput("场次数量", "5"),
                     FSlider("对白密度", 0.0f, 1.0f, 0.05f, "0.60"),
                     FSwitch("保留旁白", "1"),
                     FArea("台词稿", "【旁白】那盏灯又亮了。\n林晓：今晚，谁会来？", 4)}};
        v.push_back(t);

        // ③ 分镜设计
        t = TypeDef{"分镜设计", "clapper", 0,
                    {{"剧本", DTText}},
                    {{"镜头表", DTShots}},
                    {FInput("镜头数", "6"),
                     FInput("平均时长", "4.5s"),
                     FSelect("运镜", {"推/摇/移", "固定机位", "手持跟拍", "环绕"}, "推/摇/移"),
                     FSelect("景别策略", {"自动", "远景开场", "近景为主"}, "自动"),
                     FSelect("转场", {"硬切", "叠化", "淡入淡出"}, "硬切"),
                     FReadonly("镜头表", "镜号 / 景别 / 运镜 / 时长")}};
        v.push_back(t);

        // ④ 角色设定（角色一致性源头）
        t = TypeDef{"角色设定", "masks", 5, {},
                    {{"角色锁", DTChar}},
                    {FInput("角色名", "林晓"),
                     FReadonly("三视图", "正视图 · 侧视图 · 背视图（3/3 已就绪）"),
                     FReadonly("多角度", "仰 · 俯 · 左45° · 右45°（4/4 已就绪）"),
                     FInput("锁定 seed", "771204"),
                     FSlider("锁定强度", 0.0f, 1.0f, 0.01f, "0.82"),
                     FSlider("允许漂移", 0.0f, 0.5f, 0.01f, "0.12"),
                     FSwitch("服装 / 发型锁", "1"),
                     FReadonly("参考图集", "3 张参考（linwan_front / side / back）")}};
        v.push_back(t);

        // ⑤ 画面关键帧
        t = TypeDef{"画面关键帧", "image", 1,
                    {{"镜头", DTShots}, {"角色锁", DTChar}},
                    {{"帧图", DTImage}},
                    {FSelect("采样器", {"sdXL_light_04", "sdXL_base", "flux_dev"}, "sdXL_light_04"),
                     FInput("步数", "28"),
                     FSlider("CFG", 1.0f, 15.0f, 0.5f, "6.5"),
                     FSelect("分辨率", {"720×1280", "1280×720", "1024²"}, "720×1280"),
                     FInput("重生成次数", "3"),
                     FArea("Prompt", "林晓提灯站在巷口，暖光，浅景深，治愈系", 3)}};
        v.push_back(t);

        // ⑥ 视频片段
        t = TypeDef{"视频片段", "film", 2,
                    {{"首帧", DTImage}},
                    {{"片段", DTVideo}},
                    {FInput("时长", "4.0s"),
                     FSelect("帧率", {"24", "30", "48"}, "24"),
                     FSelect("补帧", {"关", "RIFE ×2", "FILM"}, "RIFE ×2"),
                     FSlider("运动强度", 0.0f, 1.0f, 0.05f, "0.50"),
                     FReadonly("角色锁校验", "✔ 与上游 image 一致")}};
        v.push_back(t);

        // ⑦ 配音
        t = TypeDef{"配音", "wave", 3,
                    {{"台词", DTText}, {"音色", DTChar}},
                    {{"音轨", DTAudio}},
                    {FSelect("音色", {"温柔女声", "低沉男声", "少年音", "旁白·中性"}, "温柔女声"),
                     FSlider("语速", 0.5f, 2.0f, 0.05f, "1.00"),
                     FSlider("音调", -12.0f, 12.0f, 1.0f, "0"),
                     FSelect("情感", {"平静", "欢快", "悲伤", "紧张"}, "平静"),
                     FSwitch("逐句对齐", "1")}};
        v.push_back(t);

        // ⑧ 字幕
        t = TypeDef{"字幕", "text", 6,
                    {{"音轨", DTAudio}, {"台词", DTText}},
                    {{"字幕", DTSrt}},
                    {FSelect("字号", {"小", "中", "大", "特大"}, "中"),
                     FSelect("位置", {"上中", "下中", "下左"}, "下中"),
                     FSwitch("描边", "1"),
                     FSwitch("双语", "0"),
                     FSelect("样式预设", {"简洁白", "描边黄", "渐变"}, "简洁白")}};
        v.push_back(t);

        // ⑨ 音乐音效
        t = TypeDef{"音乐音效", "music", 3, {},
                    {{"音乐", DTAudio}},
                    {FSelect("曲目", {"夜航", "微光", "潮汐", "无"}, "夜航"),
                     FSlider("音量", -40.0f, 0.0f, 1.0f, "-18"),
                     FSwitch("淡入淡出", "1"),
                     FSwitch("卡点对齐", "0")}};
        v.push_back(t);

        // ⑩ 剪辑合成
        t = TypeDef{"剪辑合成", "layers", 4,
                    {{"视频轨 1", DTVideo}, {"视频轨 2", DTVideo}, {"音频轨", DTAudio}, {"字幕轨", DTSrt}},
                    {{"时间线", DTTimeline}},
                    {FReadonly("时间线", "视频轨 1 / 视频轨 2 / 音频轨 / 字幕轨"),
                     FSelect("转场", {"硬切", "叠化", "淡入淡出"}, "硬切"),
                     FSelect("调色", {"原片", "暖调", "冷调", "电影感"}, "原片"),
                     FSelect("输出分辨率", {"1080×1920", "1920×1080", "1080²"}, "1080×1920"),
                     FSelect("码率", {"4M", "8M", "12M"}, "8M"),
                     FSwitch("响度归一", "1")}};
        v.push_back(t);

        // ⑪ 导出成片
        t = TypeDef{"导出成片", "upload", 4,
                    {{"时间线", DTTimeline}}, {},
                    {FInput("文件名", "灯语回声_ep03.mp4"),
                     FSelect("容器", {"MP4", "MOV"}, "MP4"),
                     FSelect("编码", {"H.264", "H.265"}, "H.264"),
                     FInput("命名模板", "{project}_ep{ep}.mp4")}};
        v.push_back(t);

        return v;
    }();
    return kTypes;
}

// ---------- 颜色 ----------

ImU32 TypeAccent(int i) {
    switch (i) {
    case 1: return theme::ToImU32(theme::Current().statusOk);
    case 2: return theme::ToImU32(theme::Current().statusBusy);
    case 3: return theme::ToImU32(theme::Current().statusWarn);
    case 4: return theme::ToImU32(theme::Current().accentInfo);
    case 5: return theme::ToImU32(theme::Current().accentSecondary);
    default: return kit::ColorAccent();
    }
}

ImU32 DataColor(int t) {
    switch (t) {
    case DTShots: return kit::ColorAccent();
    case DTChar: return theme::ToImU32(theme::Current().accentSecondary);
    case DTImage: return theme::ToImU32(theme::Current().statusOk);
    case DTVideo: return theme::ToImU32(theme::Current().statusBusy);
    case DTAudio: return theme::ToImU32(theme::Current().statusWarn);
    case DTSrt: return theme::ToImU32(theme::Current().statusPending);
    case DTTimeline: return theme::ToImU32(theme::Current().statusDanger);
    default: return theme::ToImU32(theme::Current().accentInfo);
    }
}

const char* DataLabel(int t) {
    switch (t) {
    case DTText: return "文本";
    case DTShots: return "镜头表";
    case DTChar: return "角色锁";
    case DTImage: return "帧图";
    case DTVideo: return "片段";
    case DTAudio: return "音频";
    case DTSrt: return "字幕";
    default: return "时间线";
    }
}

ImU32 StateColor(int s) {
    switch (s) {
    case WfBlocked: return theme::ToImU32(theme::Current().statusWarn);
    case WfQueued: return theme::ToImU32(theme::Current().statusPending);
    case WfRunning: return theme::ToImU32(theme::Current().statusBusy);
    case WfDone: return theme::ToImU32(theme::Current().statusOk);
    case WfFailed: return theme::ToImU32(theme::Current().statusDanger);
    case WfSkipped: return theme::ToImU32(theme::Current().statusIdle);
    default: return kit::ColorTextMuted();
    }
}

theme::Tone StateTone(int s) {
    switch (s) {
    case WfBlocked: return theme::Tone::Warn;
    case WfQueued: return theme::Tone::Info;
    case WfRunning: return theme::Tone::Busy;
    case WfDone: return theme::Tone::Ok;
    case WfFailed: return theme::Tone::Danger;
    default: return theme::Tone::Idle;
    }
}

const char* StateLabel(int s) {
    switch (s) {
    case WfBlocked: return "等待依赖";
    case WfQueued: return "已排队";
    case WfRunning: return "运行中";
    case WfDone: return "已完成";
    case WfFailed: return "失败";
    case WfSkipped: return "已跳过";
    default: return "待运行";
    }
}

// ---------- 连线兼容矩阵（webui 的 canConnect） ----------
bool CanConnect(int from, int to) {
    switch (from) {
    case DTText: return to == DTText || to == DTAudio || to == DTSrt;
    case DTShots: return to == DTShots || to == DTImage;
    case DTChar: return to == DTChar || to == DTImage || to == DTAudio;
    case DTImage: return to == DTImage || to == DTVideo;
    case DTVideo: return to == DTVideo || to == DTTimeline;
    case DTAudio: return to == DTAudio || to == DTTimeline;
    case DTSrt: return to == DTSrt || to == DTTimeline;
    case DTTimeline: return to == DTTimeline;
    default: return false;
    }
}

// ---------- 贝塞尔（与 kit/View_FlowCanvas.cpp 同公式） ----------
ImVec2 BezierAt(ImVec2 a, ImVec2 b, float dx, float t) {
    const float u = 1.0f - t;
    const float w0 = u * u * u;
    const float w1 = 3.0f * u * u * t;
    const float w2 = 3.0f * u * t * t;
    const float w3 = t * t * t;
    return ImVec2(w0 * a.x + w1 * (a.x + dx) + w2 * (b.x - dx) + w3 * b.x,
                  w0 * a.y + w1 * a.y + w2 * b.y + w3 * b.y);
}

void DrawLink(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, float thickness, bool dashed,
              float dashOffset) {
    const float dx = std::max(36.0f, std::fabs(b.x - a.x) * 0.55f);
    constexpr int kSegments = 24;
    constexpr float kDash = 6.0f;
    const float chain = std::fabs(b.x - a.x) + std::fabs(b.y - a.y);
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = static_cast<float>(i) / kSegments;
        const float t1 = static_cast<float>(i + 1) / kSegments;
        if (dashed) {
            const float s0 = chain * t0 + dashOffset;
            if (std::fmod(s0, kDash * 2.0f) > kDash) {
                continue;
            }
        }
        draw->PathLineTo(BezierAt(a, b, dx, t0));
        draw->PathLineTo(BezierAt(a, b, dx, t1));
    }
    draw->PathStroke(color, 0, thickness);
}

} // namespace

// ============================================================
// WorkflowPage
// ============================================================

void WorkflowPage::BuildGraph() {
    nodes_.clear();
    links_.clear();
    selected_ = 0;
    fitted_ = false;

    auto add = [&](int type, float x, float y, int state, std::string preview,
                   bool lock, std::string who, int seed) {
        Node n;
        n.id = static_cast<int>(nodes_.size()) + 1;
        n.type = type;
        n.x = x;
        n.y = y;
        n.state = state;
        n.progress = state == WfDone ? 100 : state == WfRunning ? 62 : 0;
        n.preview = std::move(preview);
        n.charLock = lock;
        n.charName = std::move(who);
        n.charSeed = seed;
        nodes_.push_back(std::move(n));
    };

    add(0, 32.0f, 40.0f, WfDone, "灯语回声 · 治愈系 · 60s", false, {}, 0);              // 1 梗概
    add(1, 248.0f, 40.0f, WfDone, "第 3 章 · 5 场 · 对白 60%", false, {}, 0);           // 2 剧本
    add(2, 464.0f, 40.0f, WfDone, "6 镜 · 均 4.5s · 推/摇/移", false, {}, 0);           // 3 分镜
    add(3, 32.0f, 300.0f, WfDone, "林晓 · seed 771204 · 3/3 视图", false, {}, 0);       // 4 角色
    add(4, 680.0f, 8.0f, WfDone, "720×1280 · 第 3 版", true, "林晓", 771204);           // 5 图 A
    add(4, 680.0f, 120.0f, WfFailed, "720×1280 · 第 1 版", true, "林晓", 771204);       // 6 图 B
    add(5, 896.0f, 8.0f, WfRunning, "4.0s · 24fps · RIFE ×2", true, "林晓", 771204);    // 7 视频 A
    add(5, 896.0f, 120.0f, WfBlocked, "4.0s · 24fps · RIFE ×2", true, "林晓", 771204);  // 8 视频 B
    add(6, 464.0f, 300.0f, WfQueued, "温柔女声 · 1.0x · 平静", false, {}, 0);           // 9 配音
    add(7, 680.0f, 300.0f, WfBlocked, "中 · 下中 · 单语", false, {}, 0);                // 10 字幕
    add(9, 1112.0f, 40.0f, WfBlocked, "1080×1920 · 8M · 响度归一", false, {}, 0);      // 11 剪辑
    add(10, 1112.0f, 180.0f, WfTodo, "灯语回声_ep03.mp4", false, {}, 0);               // 12 导出

    auto link = [&](int from, int fromPort, int to, int toPort) {
        links_.push_back(Link{from, fromPort, to, toPort});
    };
    link(1, 0, 2, 0);
    link(2, 0, 3, 0);
    link(3, 0, 5, 0);
    link(3, 0, 6, 0);
    link(4, 0, 5, 1);
    link(4, 0, 6, 1);
    link(5, 0, 7, 0);
    link(6, 0, 8, 0);
    link(2, 0, 9, 0);
    link(7, 0, 11, 0);
    link(8, 0, 11, 1);
    link(9, 0, 10, 0);
    link(9, 0, 11, 2);
    link(10, 0, 11, 3);
    link(11, 0, 12, 0);

    selected_ = 3; // 默认选中「分镜设计」，与 webui 一致
}

void WorkflowPage::FitView(const kit::Rect& canvas) {
    if (nodes_.empty()) {
        return;
    }
    float minX = nodes_[0].x;
    float minY = nodes_[0].y;
    float maxX = nodes_[0].x + kNodeW;
    float maxY = nodes_[0].y + kNodeH;
    for (const Node& n : nodes_) {
        minX = std::min(minX, n.x);
        minY = std::min(minY, n.y);
        maxX = std::max(maxX, n.x + kNodeW);
        maxY = std::max(maxY, n.y + kNodeH);
    }
    constexpr float kPad = 20.0f;
    minX -= kPad;
    minY -= kPad;
    maxX += kPad;
    maxY += kPad;

    // 画布的矩形已经给工具条、节点库和面板留出空间。视图偏移相对画布原点。
    const float availW = std::max(1.0f, canvas.width() - 32.0f);
    const float availH = std::max(1.0f, canvas.height() - 32.0f);
    const float spanX = std::max(1.0f, maxX - minX);
    const float spanY = std::max(1.0f, maxY - minY);
    const float z = std::clamp(std::min(availW / spanX, availH / spanY), 0.35f, 1.15f);

    view_.z = z;
    view_.x = (canvas.width() - spanX * z) * 0.5f - minX * z;
    view_.y = (canvas.height() - spanY * z) * 0.5f - minY * z;
}

void WorkflowPage::RunAll() {
    // 拓扑序推进：每 0.22s 完成一个，进度用 kit::Now() 驱动。
    // ⚠️ 这里只改状态，不动坐标 —— 与 webui 的 runNodes 同语义。
    busy_ = true;
    busyUntil_ = kit::Now() + 0.1f;
    for (Node& n : nodes_) {
        if (n.state == WfDone || n.state == WfSkipped) {
            continue;
        }
        n.state = WfQueued;
        n.progress = 0;
    }
}

void WorkflowPage::SubmitSkill(const std::string& instruction) {
    if (instruction.empty()) {
        return;
    }
    skillInput_ = instruction;
    // 解析（离线确定性规划）：关键词 → 章节 / 时长 / 画幅 / 角色
    std::string whoName = instruction.find("陈拾") != std::string::npos ? "陈拾" : "林晓";
    int seed = whoName == "陈拾" ? 330915 : 771204;

    BuildGraph();
    for (Node& n : nodes_) {
        n.state = WfTodo;
        n.progress = 0;
    }
    // 角色节点同步成指令里点名的角色
    for (Node& n : nodes_) {
        if (n.type == 3) { // 角色设定
            n.preview = whoName + " · seed " + std::to_string(seed) + " · 3/3 视图";
        }
        if (n.charLock) {
            n.charName = whoName;
            n.charSeed = seed;
        }
    }

    feed_.clear();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "解析指令：%s", instruction.c_str());
    feed_.emplace_back(buf);
    std::snprintf(buf, sizeof(buf), "规划节点图：%d 类节点 → 采用 9 类 · %d 节点",
                  static_cast<int>(Types().size()), static_cast<int>(nodes_.size()));
    feed_.emplace_back(buf);
    std::snprintf(buf, sizeof(buf), "连接端口：%d 条边 · 类型校验 0 冲突",
                  static_cast<int>(links_.size()));
    feed_.emplace_back(buf);
    std::snprintf(buf, sizeof(buf), "角色锁定：%s · seed %d · 3 视图 + 4 角度", whoName.c_str(), seed);
    feed_.emplace_back(buf);
    feed_.emplace_back("依赖校验：无环 · 无孤立节点 · 关键路径 7 层");
    feed_.emplace_back("就绪：按「运行」开始执行，预计 6 分 12 秒");
    selected_ = 3;
}

// ------------------------------------------------------------

const WorkflowPage::Node* WorkflowPage::Selected() const {
    if (selected_ == 0) {
        return nullptr;
    }
    for (const Node& n : nodes_) {
        if (n.id == selected_) {
            return &n;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------

// ============================================================
// 画布
// ============================================================

void WorkflowPage::DrawCanvas(ImDrawList* draw, const kit::Rect& canvas, bool stacked) {
    if (nodes_.empty()) {
        return;
    }
    if (!fitted_) {
        FitView(canvas);
        fitted_ = true;
    }

    draw->PushClipRect(canvas.min, canvas.max, true);
    const ImVec2 mouse = ImGui::GetMousePos();
    const kit::Rect clip{draw->GetClipRectMin(), draw->GetClipRectMax()};

    // 画布底：fill-muted + 22px 点阵（views.css:1017 `.flow-canvas`）。
    // ⚠️ 点阵必须**跟着视图走**（间距乘 z、原点加 view 偏移），否则平移时网格
    //    会钉在屏幕上 —— 那种「网格不动、节点在动」是最容易被当成渲染 bug 的形态。
    kit::DrawRoundRect(draw, canvas.min, canvas.max, 0.0f, kit::ColorFillMuted(), kit::ColorTransparent(),
                       0.0f);
    if (const float cell = 22.0f * view_.z; cell > 4.0f) {
        const float ox = canvas.min.x + std::fmod(view_.x, cell);
        const float oy = canvas.min.y + std::fmod(view_.y, cell);
        const ImU32 dot = kit::ColorLineNormal();
        for (float gy = oy; gy < canvas.max.y; gy += cell) {
            for (float gx = ox; gx < canvas.max.x; gx += cell) {
                draw->AddCircleFilled(ImVec2(gx, gy), 1.0f, dot, 6);
            }
        }
    }
    // 工具条、节点库和配置面板各占自己的布局区域。
    const bool hovered = !kit::OverlayMouseGate() && ImGui::IsWindowHovered() &&
                         clip.contains(mouse);

    auto ToScreen = [&](float wx, float wy) {
        return ImVec2(canvas.min.x + view_.x + wx * view_.z,
                      canvas.min.y + view_.y + wy * view_.z);
    };
    auto ToWorld = [&](float sx, float sy) {
        return ImVec2((sx - canvas.min.x - view_.x) / view_.z,
                      (sy - canvas.min.y - view_.y) / view_.z);
    };

    // ---------- 端口几何 ----------
    auto PortPos = [&](const Node& n, bool out, int index, int count) {
        const TypeDef& t = Types()[n.type];
        const int total = out ? static_cast<int>(t.out.size()) : static_cast<int>(t.in.size());
        const float usable = kNodeH - kHeadH - 10.0f;
        const float dy = kHeadH + (usable * (static_cast<float>(index) + 0.5f)) /
                                       static_cast<float>(std::max(1, total));
        (void)count;
        return ImVec2(n.x + (out ? kNodeW : 0.0f), n.y + dy);
    };
    auto PortType = [&](int nodeId, bool out, int index) -> int {
        for (const Node& n : nodes_) {
            if (n.id != nodeId) {
                continue;
            }
            const TypeDef& t = Types()[n.type];
            const auto& list = out ? t.out : t.in;
            if (index < 0 || index >= static_cast<int>(list.size())) {
                return -1;
            }
            return list[index].type;
        }
        return -1;
    };

    // ---------- 命中：先端口，再节点 ----------
    int portNode = -1;
    bool portOut = false;
    int portIndex = -1;
    float portDist = 14.0f;
    for (const Node& n : nodes_) {
        for (int dir = 0; dir < 2; ++dir) {
            const bool out = dir == 1;
            const auto& list = out ? Types()[n.type].out : Types()[n.type].in;
            for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                const ImVec2 p = ToScreen(PortPos(n, out, i, 0).x, PortPos(n, out, i, 0).y);
                const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                if (d < portDist) {
                    portDist = d;
                    portNode = n.id;
                    portOut = out;
                    portIndex = i;
                }
            }
        }
    }

    int hitNode = -1;
    for (std::size_t i = nodes_.size(); i-- > 0;) {
        const Node& n = nodes_[i];
        const ImVec2 tl = ToScreen(n.x, n.y);
        if (mouse.x >= tl.x && mouse.x <= tl.x + kNodeW * view_.z && mouse.y >= tl.y &&
            mouse.y <= tl.y + kNodeH * view_.z) {
            hitNode = n.id;
            break;
        }
    }

    // ---------- 滚轮缩放（光标为锚） ----------
    if (hovered && portNode < 0 && (!stacked || ImGui::GetIO().KeyCtrl)) {
        if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f) {
            const float z2 = std::clamp(view_.z * std::pow(1.12f, wheel), 0.35f, 2.0f);
            const float wx = (mouse.x - canvas.min.x - view_.x) / view_.z;
            const float wy = (mouse.y - canvas.min.y - view_.y) / view_.z;
            view_.z = z2;
            view_.x = mouse.x - canvas.min.x - wx * z2;
            view_.y = mouse.y - canvas.min.y - wy * z2;
        }
    }

    // ---------- 交互 ----------
    static int dragId = -1;
    static bool panning = false;
    static ImVec2 dragOffset{};
    static ImVec2 panStartMouse{};
    static ImVec2 panStartView{};

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered) {
        if (portNode > 0 && portOut) {
            wiring_ = true;
            wiringFrom_ = portNode;
            wiringPort_ = portIndex;
            const ImVec2 w = ToWorld(mouse.x, mouse.y);
            wiringCursorX_ = w.x;
            wiringCursorY_ = w.y;
        } else if (portNode > 0 && !portOut) {
            // 已连的输入端口：拔线重连
            for (std::size_t i = 0; i < links_.size(); ++i) {
                const Link& l = links_[i];
                if (l.to == portNode && l.toPort == portIndex) {
                    wiring_ = true;
                    wiringFrom_ = l.from;
                    wiringPort_ = l.fromPort;
                    links_.erase(links_.begin() + static_cast<std::ptrdiff_t>(i));
                    const ImVec2 w = ToWorld(mouse.x, mouse.y);
                    wiringCursorX_ = w.x;
                    wiringCursorY_ = w.y;
                    break;
                }
            }
        } else if (hitNode > 0) {
            selected_ = hitNode;
            dragId = hitNode;
            const ImVec2 world = ToWorld(mouse.x, mouse.y);
            for (const Node& n : nodes_) {
                if (n.id == hitNode) {
                    dragOffset = ImVec2(world.x - n.x, world.y - n.y);
                }
            }
        } else {
            selected_ = 0;
            panning = true;
            panStartMouse = mouse;
            panStartView = ImVec2(view_.x, view_.y);
        }
    }

    if (wiring_) {
        const ImVec2 w = ToWorld(mouse.x, mouse.y);
        wiringCursorX_ = w.x;
        wiringCursorY_ = w.y;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            int toNode = -1;
            int toPort = -1;
            float best = 16.0f;
            for (const Node& n : nodes_) {
                const auto& list = Types()[n.type].in;
                for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                    const ImVec2 p = ToScreen(PortPos(n, false, i, 0).x, PortPos(n, false, i, 0).y);
                    const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                    if (d < best) {
                        best = d;
                        toNode = n.id;
                        toPort = i;
                    }
                }
            }
            if (hovered && toNode > 0 && toNode != wiringFrom_) {
                const int fromType = PortType(wiringFrom_, true, wiringPort_);
                const int toType = PortType(toNode, false, toPort);
                if (fromType >= 0 && toType >= 0 && CanConnect(fromType, toType)) {
                    bool exists = false;
                    for (const Link& l : links_) {
                        if (l.to == toNode && l.toPort == toPort) {
                            exists = true;
                        }
                    }
                    if (!exists) {
                        links_.push_back(Link{wiringFrom_, wiringPort_, toNode, toPort});
                    }
                }
            }
            wiring_ = false;
            wiringFrom_ = -1;
        }
    } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragId = -1;
        panning = false;
    }

    if (!wiring_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (dragId > 0) {
            const ImVec2 world = ToWorld(mouse.x, mouse.y);
            for (Node& n : nodes_) {
                if (n.id == dragId) {
                    n.x = std::max(8.0f, std::round((world.x - dragOffset.x) / 8.0f) * 8.0f);
                    n.y = std::max(8.0f, std::round((world.y - dragOffset.y) / 8.0f) * 8.0f);
                }
            }
        } else if (panning) {
            view_.x = panStartView.x + (mouse.x - panStartMouse.x);
            view_.y = panStartView.y + (mouse.y - panStartMouse.y);
        }
    }

    // ---------- 连线 ----------
    const float dashOffset = kit::Now() * 0.7f * 12.0f;
    for (const Link& l : links_) {
        const Node* a = nullptr;
        const Node* b = nullptr;
        for (const Node& n : nodes_) {
            if (n.id == l.from) a = &n;
            if (n.id == l.to) b = &n;
        }
        if (a == nullptr || b == nullptr) {
            continue;
        }
        const int ai = std::max(0, l.fromPort);
        const int bi = std::max(0, l.toPort);
        const ImVec2 pa3 = PortPos(*a, true, ai, 0);
        const ImVec2 pb3 = PortPos(*b, false, bi, 0);
        const ImVec2 pa = ToScreen(pa3.x, pa3.y);
        const ImVec2 pb = ToScreen(pb3.x, pb3.y);
        const bool live = a->state == WfDone && b->state != WfTodo;
        DrawLink(draw, pa, pb, kit::WithAlpha(kit::ColorLineNormal(), 0.4f), 5.0f * view_.z, false, 0.0f);
        DrawLink(draw, pa, pb, live ? kit::ColorAccent() : kit::ColorLineStrong(), 1.8f * view_.z, live,
                 dashOffset);
    }

    // 拖拽中的预览线
    if (wiring_ && wiringFrom_ > 0) {
        for (const Node& n : nodes_) {
            if (n.id != wiringFrom_) {
                continue;
            }
            const ImVec2 p3 = PortPos(n, true, wiringPort_, 0);
            const ImVec2 pa = ToScreen(p3.x, p3.y);
            const ImVec2 pb = ToScreen(wiringCursorX_, wiringCursorY_);
            draw->AddLine(pa, pb, kit::ColorAccent(), 2.0f);
            draw->AddCircleFilled(pb, 4.0f, kit::ColorAccent(), 12);
        }
    }

    // ---------- 节点 ----------
    for (const Node& n : nodes_) {
        const TypeDef& t = Types()[n.type];
        const ImVec2 tl = ToScreen(n.x, n.y);
        const float w = kNodeW * view_.z;
        const float h = kNodeH * view_.z;
        const kit::Rect box{tl, ImVec2(tl.x + w, tl.y + h)};
        const bool sel = n.id == selected_;
        const ImU32 accent = TypeAccent(t.accent);

        kit::DrawShadow(draw, box.min, box.max, 10.0f * view_.z, theme::ShadowTier::Card, 1.0f, view_.z);
        // 边框优先级：选中 > 悬停（views.css:1046 `.fnode:hover`）> 状态色
        const ImU32 borderColor = sel ? kit::ColorAccent()
                                      : (n.id == hitNode ? kit::ColorLineStrong() : StateColor(n.state));
        kit::DrawRoundRect(draw, box.min, box.max, 10.0f * view_.z, kit::ColorPanel(), borderColor,
                           1.5f * view_.z);
        if (sel) {
            kit::DrawRoundRect(draw, box.min - ImVec2(3, 3), box.max + ImVec2(3, 3), 12.0f * view_.z, 0,
                               kit::ColorAccentDim(), 2.0f * view_.z);
        } else if (n.state == WfRunning) {
            kit::DrawRoundRect(draw, box.min - ImVec2(3, 3), box.max + ImVec2(3, 3), 12.0f * view_.z, 0,
                               kit::WithAlpha(theme::ToImU32(theme::Current().statusBusy), 0.18f),
                               2.0f * view_.z);
        }

        // 头部
        const float padX = 10.0f * view_.z;
        const float headH = kHeadH * view_.z;
        kit::DrawIcon(draw, t.icon, ImVec2(box.min.x + padX, box.min.y + 8.0f * view_.z), 14.0f * view_.z,
                      accent);
        kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kTiny), theme::font::kTiny * view_.z,
                             ImVec2(box.min.x + padX + 20.0f * view_.z, box.min.y + 8.5f * view_.z),
                             w - padX * 2.0f - 40.0f * view_.z, kit::ColorText(), t.name);
        draw->AddLine(ImVec2(box.min.x, box.min.y + headH), ImVec2(box.max.x, box.min.y + headH),
                      kit::ColorLineSubtle(), 1.0f);

        // 状态角标（右上）
        if (n.state == WfRunning) {
            kit::Spinner(draw, ImVec2(box.max.x - 12.0f * view_.z, box.min.y + 15.0f * view_.z), true);
        } else if (n.state == WfDone) {
            kit::DrawIcon(draw, "check", ImVec2(box.max.x - 18.0f * view_.z, box.min.y + 8.0f * view_.z),
                          12.0f * view_.z, kit::ColorText());
        } else if (n.state == WfFailed) {
            kit::DrawIcon(draw, "alert", ImVec2(box.max.x - 18.0f * view_.z, box.min.y + 8.0f * view_.z),
                          12.0f * view_.z, theme::ToImU32(theme::Current().statusDanger));
        } else if (n.state == WfBlocked) {
            kit::DrawIcon(draw, "clock", ImVec2(box.max.x - 18.0f * view_.z, box.min.y + 8.0f * view_.z),
                          12.0f * view_.z, theme::ToImU32(theme::Current().statusWarn));
        }

        // 副行
        kit::DrawTextClipped(draw, kit::MonoAt(theme::font::kLabel), theme::font::kLabel * view_.z,
                             ImVec2(box.min.x + padX, box.min.y + headH + 7.0f * view_.z),
                             w - padX * 2.0f, kit::ColorTextMuted(), n.preview);

        // 角色锁徽章
        if (n.charLock) {
            std::string badge = "masks " + n.charName;
            const float bw = kit::MeasureClipped(kit::FontAt(theme::font::kFine), theme::font::kFine * view_.z,
                                                 9999.0f, badge) +
                             16.0f * view_.z;
            const kit::Rect br{box.min.x + padX, box.min.y + headH + 24.0f * view_.z,
                               box.max.x - padX, box.min.y + headH + 42.0f * view_.z};
            if (bw < w - padX * 2.0f) {
                kit::DrawRoundRect(draw, br.min, br.max, 9.0f * view_.z,
                                   kit::WithAlpha(theme::ToImU32(theme::Current().accentSecondary), 0.16f),
                                   kit::ColorTransparent(), 0.0f);
                kit::DrawTextClipped(draw, kit::FontAt(theme::font::kFine), theme::font::kFine * view_.z,
                                     ImVec2(br.min.x + 5.0f * view_.z, br.min.y + 3.0f * view_.z),
                                     br.width() - 8.0f * view_.z,
                                     theme::ToImU32(theme::Current().accentSecondary), badge);
            }
        }

        // 运行中进度条
        if (n.state == WfRunning) {
            const kit::Rect pr{box.min.x, box.max.y - 2.5f * view_.z, box.max.x, box.max.y};
            kit::DrawRoundRect(draw, pr.min, pr.max, 0.0f, kit::ColorFillMuted(), kit::ColorTransparent(), 0.0f);
            const kit::Rect pf{pr.min, ImVec2(pr.min.x + pr.width() * (n.progress / 100.0f), pr.max.y)};
            kit::DrawRoundRect(draw, pf.min, pf.max, 0.0f,
                               theme::ToImU32(theme::Current().statusBusy), kit::ColorTransparent(), 0.0f);
        }

        // 端口
        for (int dir = 0; dir < 2; ++dir) {
            const bool out = dir == 1;
            const auto& list = out ? t.out : t.in;
            for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                const ImVec2 p3 = PortPos(n, out, i, 0);
                const ImVec2 c = ToScreen(p3.x, p3.y);
                const ImU32 pc = DataColor(list[i].type);
                draw->AddCircleFilled(c, kPortR * view_.z, kit::ColorPanel(), 16);
                draw->AddCircle(c, kPortR * view_.z, pc, 16, 2.0f * view_.z);
            }
        }
    }
    draw->PopClipRect();
}

// ============================================================
// 顶栏（工具条 + Skill 条）
// ============================================================

void WorkflowPage::DrawTopBar(ImDrawList* draw, const kit::Rect& bar, const kit::Rect& skill) {
    const float y = bar.min.y;
    kit::DrawShadowed(draw, bar.min, bar.max, 10.0f, kit::GlassColor(), kit::ColorLineSubtle(), 1.0f,
                      theme::ShadowTier::Card);

    float x = bar.min.x + 12.0f;
    const kit::Rect iconBox = kit::RectAt(x, y + 9.0f, 26.0f, 26.0f);
    kit::DrawRoundRect(draw, iconBox.min, iconBox.max, 6.0f, kit::ColorAccentDim(), kit::ColorTransparent(), 0.0f);
    kit::DrawIcon(draw, "layers", ImVec2(iconBox.min.x + 5.0f, iconBox.min.y + 5.0f), 16.0f, kit::ColorAccent());
    x += 34.0f;

    constexpr float buttonW = 62.0f;
    constexpr float buttonGap = 4.0f;
    const float actionsX = bar.max.x - 12.0f - (4.0f * buttonW + 3.0f * buttonGap);
    const float titleW = std::max(0.0f, actionsX - x - 16.0f);
    kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kBody), theme::font::kBody,
                         ImVec2(x, y + 9.0f), titleW, kit::ColorText(), "工作流");
    char counts[96];
    std::snprintf(counts, sizeof(counts), "%d 节点 · %d 连线",
                  static_cast<int>(nodes_.size()), static_cast<int>(links_.size()));
    kit::DrawTextClipped(draw, kit::FontAt(theme::font::kFine), theme::font::kFine,
                         ImVec2(x, y + 25.0f), titleW, kit::ColorTextMuted(), counts);
    kit::Row actions{kit::RectAt(actionsX, y + 10.0f, bar.max.x - actionsX, 24.0f), buttonGap};

    kit::ButtonSpec spec;
    spec.variant = kit::ButtonVariant::Ghost;
    spec.size = kit::ButtonSize::Small;
    spec.icon = "upload";
    if (kit::Button(draw, actions.Next(buttonW, 24.0f), "导入", spec, "wf-import")) {
        // 演示：无操作（真机接 flow.json 导入）
    }
    spec.icon = "download";
    if (kit::Button(draw, actions.Next(buttonW, 24.0f), "导出", spec, "wf-export")) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "flow.json 已生成（%d 节点 / %d 连线）", static_cast<int>(nodes_.size()),
                      static_cast<int>(links_.size()));
        WorkspaceToast(msg);
    }
    kit::ButtonSpec check;
    check.variant = kit::ButtonVariant::Secondary;
    check.size = kit::ButtonSize::Small;
    check.icon = "check";
    if (kit::Button(draw, actions.Next(buttonW, 24.0f), "校验", check, "wf-validate")) {
        WorkspaceToast("提交前校验通过：无环 · 无孤立节点 · 角色锁一致");
    }
    kit::ButtonSpec run;
    run.variant = kit::ButtonVariant::Primary;
    run.size = kit::ButtonSize::Small;
    run.icon = "play";
    run.loading = busy_;
    if (kit::Button(draw, actions.Next(buttonW, 24.0f), "运行", run, "wf-run")) {
        RunAll();
    }

    // Skill 卡片只绘制背景，里面的输入框与按钮各自接收鼠标。
    kit::DrawShadowed(draw, skill.min, skill.max, 10.0f, kit::GlassColor(), kit::ColorLineSubtle(), 1.0f,
                      theme::ShadowTier::Card);
    kit::DrawIcon(draw, "sparkles", ImVec2(skill.min.x + 12.0f, skill.min.y + 13.0f), 16.0f,
                  kit::ColorAccent());
    const kit::Rect input = kit::RectAt(skill.min.x + 36.0f, skill.min.y + 8.0f,
                                        skill.width() - 132.0f, 28.0f);
    (void)kit::Input(draw, input, skillInput_, "描述你要的视频，例如：把第 3 章改编成 60 秒竖屏短片",
                     "wf-skill-input");
    kit::ButtonSpec gen;
    gen.variant = kit::ButtonVariant::Primary;
    gen.size = kit::ButtonSize::Small;
    gen.icon = "sparkles";
    gen.loading = busy_;
    if (kit::Button(draw, kit::RectAt(skill.max.x - 90.0f, skill.min.y + 9.0f, 80.0f, 26.0f), "生成",
                    gen, "wf-skill-gen")) {
        SubmitSkill(skillInput_);
    }
}

// ============================================================
// 节点库（底部横条）
// ============================================================

void WorkflowPage::DrawLibrary(ImDrawList* draw, const kit::Rect& box, float itemWidth) {
    kit::DrawShadowed(draw, box.min, box.max, 14.0f, kit::GlassColor(), kit::ColorLineSubtle(), 1.0f,
                      theme::ShadowTier::Card);

    kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kTiny), theme::font::kTiny,
                         ImVec2(box.min.x + 12.0f, box.min.y + 8.0f), 60.0f, kit::ColorTextMuted(), "节点库");

    kit::Flow items{kit::RectAt(box.min.x + kLibraryPadding, box.min.y + kLibraryHeader,
                               box.width() - 2.0f * kLibraryPadding, box.height() - kLibraryHeader),
                    kLibraryGap, kLibraryGap};
    for (int i = 0; i < static_cast<int>(Types().size()); ++i) {
        const TypeDef& t = Types()[i];
        const kit::Rect item = items.Place(itemWidth, kLibraryItemHeight);
        const kit::Hit itemHit = kit::HitTest(item, "wf-lib", i);
        const bool hover = itemHit.hovered;
        kit::DrawRoundRect(draw, item.min, item.max, 6.0f, hover ? kit::ColorFillHover() : kit::ColorFillMuted(),
                           hover ? kit::ColorLineNormal() : kit::ColorTransparent(), 1.0f);
        kit::DrawIcon(draw, t.icon, ImVec2(item.min.x + 5.0f, item.min.y + 4.0f), 13.0f, TypeAccent(t.accent));
        kit::DrawTextClipped(draw, kit::FontAt(theme::font::kLabel), theme::font::kLabel,
                             ImVec2(item.min.x + 21.0f, item.min.y + 4.0f), item.width() - 26.0f,
                             kit::ColorTextSecondary(), t.name);
        if (itemHit.clicked) {
            Node n;
            n.id = static_cast<int>(nodes_.size()) + 1;
            for (const Node& e : nodes_) {
                n.id = std::max(n.id, e.id + 1);
            }
            n.type = i;
            n.x = 360.0f + static_cast<float>((nodes_.size() % 5) * 32);
            n.y = 260.0f + static_cast<float>((nodes_.size() % 4) * 32);
            n.state = WfTodo;
            nodes_.push_back(n);
            selected_ = n.id;
        }
    }
}

// ============================================================
// 配置面板
// ============================================================

void WorkflowPage::DrawInspector(ImDrawList* draw, const kit::Rect& panel) {
    const Node* sel = Selected();

    // 头部
    kit::Rect head = kit::RectAt(panel.min.x, panel.min.y, panel.width(), 44.0f);
    kit::DrawRoundRect(draw, head.min, head.max, 14.0f, kit::GlassColor(), kit::ColorTransparent(), 0.0f);
    draw->AddLine(ImVec2(head.min.x, head.max.y), ImVec2(head.max.x, head.max.y), kit::ColorLineSubtle(), 1.0f);

    float hx = head.min.x + 14.0f;
    if (sel != nullptr) {
        const TypeDef& t = Types()[sel->type];
        kit::DrawIcon(draw, t.icon, ImVec2(hx, head.min.y + 14.0f), 16.0f, TypeAccent(t.accent));
        hx += 22.0f;
        kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kTitle), theme::font::kTitle,
                             ImVec2(hx, head.min.y + 8.0f), 120.0f, kit::ColorText(), t.name);
        char sid[32];
        std::snprintf(sid, sizeof(sid), "#%d", sel->id);
        kit::DrawTextClipped(draw, kit::MonoAt(theme::font::kFine), theme::font::kFine,
                             ImVec2(hx + 4.0f, head.min.y + 26.0f), 80.0f, kit::ColorTextMuted(), sid);
        hx += 128.0f;
        const kit::Rect tag = kit::RectAt(hx, head.min.y + 13.0f, kit::TagWidth(StateLabel(sel->state), true, true),
                                          18.0f);
        kit::Tag(draw, tag, StateLabel(sel->state), StateTone(sel->state), true, sel->state == WfRunning);
    } else {
        kit::DrawIcon(draw, "panel", ImVec2(hx, head.min.y + 14.0f), 16.0f, kit::ColorTextMuted());
        hx += 22.0f;
        kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kBody), theme::font::kBody,
                             ImVec2(hx, head.min.y + 12.0f), 200.0f, kit::ColorText(), "节点配置");
    }

    // 折叠
    if (kit::IconButton(draw, kit::RectAt(head.max.x - 34.0f, head.min.y + 10.0f, 22.0f, 22.0f), "chevdown",
                        folded_, false, "wf-inspector-fold")) {
        folded_ = !folded_;
    }

    if (folded_) {
        return;
    }

    // 页签
    static const char* kTabs[] = {"参数", "端口", "角色", "日志"};
    const float tabY = head.max.y;
    const kit::Rect tabRect = kit::RectAt(panel.min.x, tabY, panel.width(), 30.0f);
    draw->AddLine(ImVec2(tabRect.min.x, tabRect.max.y), ImVec2(tabRect.max.x, tabRect.max.y),
                  kit::ColorLineSubtle(), 1.0f);
    const float tabW = panel.width() / 4.0f;
    for (int i = 0; i < 4; ++i) {
        const kit::Rect t = kit::RectAt(tabRect.min.x + tabW * i, tabRect.min.y, tabW, tabRect.height());
        const bool on = panelTab_ == i;
        if (kit::Clicked(t, "wf-tab", i)) {
            panelTab_ = i;
        }
        kit::DrawTextClipped(draw, on ? kit::FontBoldAt(theme::font::kLabel) : kit::FontAt(theme::font::kLabel),
                             theme::font::kLabel,
                             ImVec2(t.min.x, t.min.y + 9.0f), t.width(), on ? kit::ColorAccent() : kit::ColorTextMuted(),
                             kTabs[i]);
        if (on) {
            draw->AddLine(ImVec2(t.min.x + 10.0f, t.max.y), ImVec2(t.max.x - 10.0f, t.max.y), kit::ColorAccent(),
                          2.0f);
        }
    }

    // 内容区
    const float footH = 44.0f;
    const kit::Rect body = kit::RectAt(panel.min.x + 1.0f, tabRect.max.y + 1.0f, panel.width() - 2.0f,
                                       panel.max.y - footH - tabRect.max.y - 1.0f);
    if (sel == nullptr) {
        kit::Empty(draw, body, "target", "选择一个节点", "点击画布上的任意节点，这里显示它的参数、端口、角色锁与日志。");
        return;
    }

    kit::Rect content = kit::RectAt(body.min.x + 12.0f, body.min.y + 10.0f, body.width() - 24.0f,
                                    body.height() - 20.0f);
    kit::Stack stack{content, 8.0f};

    if (panelTab_ == 0) {
        const TypeDef& t = Types()[sel->type];
        // 逐字段渲染；字段值用「节点下标 → 字段串」的静态缓存（跨帧保持）。
        static std::vector<std::vector<std::string>> values;
        for (int i = 0; i < static_cast<int>(t.fields.size()); ++i) {
            const FieldDef& f = t.fields[i];
            if (stack.Remain() < 40.0f) {
                break;
            }
            std::string help;
            Rect rowRect = kit::Field(draw, stack.Next(52.0f), f.label, help);
            const float rh = 28.0f;
            const Rect ctrl = kit::RectAt(rowRect.min.x, rowRect.min.y, rowRect.width(), rh);
            const std::string idPrefix = "wf-f" + std::to_string(sel->id) + "-" + std::to_string(i);

            const int ni = sel->id - 1;
            if (ni >= 0 && static_cast<int>(values.size()) <= ni) {
                values.resize(static_cast<std::size_t>(ni) + 1);
            }
            if (ni < 0 || ni >= static_cast<int>(values.size())) {
                continue;
            }
            if (static_cast<int>(values[ni].size()) < static_cast<int>(t.fields.size())) {
                values[ni].resize(t.fields.size());
                for (int k = 0; k < static_cast<int>(t.fields.size()); ++k) {
                    if (values[ni][k].empty()) {
                        values[ni][k] = t.fields[k].def;
                    }
                }
            }
            std::string& v = values[ni][i];

            if (f.kind == 0) {
                kit::Input(draw, ctrl, v, f.label, idPrefix);
            } else if (f.kind == 1) {
                kit::TextArea(draw, ctrl, v, std::max(2, f.rows), idPrefix);
            } else if (f.kind == 2) {
                int idx = -1;
                for (int k = 0; k < static_cast<int>(f.options.size()); ++k) {
                    if (f.options[k] == v) idx = k;
                }
                if (kit::Select(draw, ctrl, f.options, idx, idPrefix) && idx >= 0 &&
                    idx < static_cast<int>(f.options.size())) {
                    v = f.options[idx];
                }
            } else if (f.kind == 4) {
                bool on = v == "1";
                if (kit::Switch(draw, kit::RectAt(ctrl.min.x, ctrl.min.y + 4.0f, 34.0f, 19.0f), on, idPrefix)) {
                    v = on ? "0" : "1";
                }
            } else if (f.kind == 3) {
                float val = static_cast<float>(std::atof(v.c_str()));
                const float frac = std::clamp((val - f.min) / std::max(0.0001f, f.max - f.min), 0.0f, 1.0f);
                const Rect track = kit::RectAt(ctrl.min.x, ctrl.min.y + 12.0f, std::max(40.0f, ctrl.width() - 52.0f), 4.0f);
                kit::DrawRoundRect(draw, track.min, track.max, 2.0f, kit::ColorFillMuted(),
                                   kit::ColorTransparent(), 0.0f);
                const Rect fill = kit::RectAt(track.min.x, track.min.y, track.width() * frac, track.height());
                kit::DrawRoundRect(draw, fill.min, fill.max, 2.0f, kit::ColorAccent(), kit::ColorTransparent(),
                                   0.0f);
                draw->AddCircleFilled(ImVec2(track.min.x + track.width() * frac, track.center().y), 6.5f,
                                      kit::ColorAccent(), 16);
                if (kit::HitTest(ctrl, idPrefix).clicked) {
                    const float mx = ImGui::GetMousePos().x;
                    const float fr = std::clamp((mx - ctrl.min.x) / std::max(1.0f, ctrl.width()), 0.0f, 1.0f);
                    char b[32];
                    std::snprintf(b, sizeof(b), "%.2f", f.min + fr * (f.max - f.min));
                    v = b;
                }
                kit::DrawTextClipped(draw, kit::MonoAt(theme::font::kFine), theme::font::kFine,
                                     ImVec2(ctrl.max.x - 44.0f, ctrl.min.y + 1.0f), 44.0f,
                                     kit::ColorTextMuted(), v);
            } else {
                kit::DrawTextClipped(draw, kit::FontAt(theme::font::kLabel), theme::font::kLabel,
                                     ImVec2(ctrl.min.x, ctrl.min.y + 8.0f), ctrl.width(),
                                     kit::ColorTextMuted(), v);
            }
        }
    } else if (panelTab_ == 1) {
        const TypeDef& t = Types()[sel->type];
        auto portRow = [&](const PortDef& p, bool isIn, int index) {
            if (stack.Remain() < 26.0f) {
                return;
            }
            const Rect r = stack.Next(22.0f);
            bool wired = false;
            for (const Link& l : links_) {
                if (isIn && l.to == sel->id && l.toPort == index) wired = true;
                if (!isIn && l.from == sel->id && l.fromPort == index) wired = true;
            }
            const ImU32 pc = DataColor(p.type);
            draw->AddCircleFilled(ImVec2(r.min.x + 4.0f, r.center().y), 4.0f, wired ? pc : kit::ColorPanel(), 12);
            draw->AddCircle(ImVec2(r.min.x + 4.0f, r.center().y), 4.0f, pc, 12, 1.5f);
            kit::DrawTextClipped(draw, kit::FontAt(theme::font::kLabel), theme::font::kLabel,
                                 ImVec2(r.min.x + 14.0f, r.min.y + 4.0f), r.width() - 120.0f,
                                 kit::ColorTextSecondary(), p.label);
            const char* dl = DataLabel(p.type);
            const float dw = kit::MeasureClipped(kit::FontAt(theme::font::kFine), theme::font::kFine, 9999.0f, dl);
            kit::DrawTextClipped(draw, kit::FontAt(theme::font::kFine), theme::font::kFine,
                                 ImVec2(r.max.x - dw - 4.0f, r.min.y + 5.0f), dw + 4.0f, kit::ColorTextMuted(), dl);
        };
        kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kFine), theme::font::kFine,
                             ImVec2(content.min.x, stack.Cur()), 200.0f, kit::ColorTextMuted(), "输入端口");
        stack.Advance(18.0f);
        for (int i = 0; i < static_cast<int>(t.in.size()); ++i) {
            portRow(t.in[i], true, i);
        }
        stack.Advance(6.0f);
        kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kFine), theme::font::kFine,
                             ImVec2(content.min.x, stack.Cur()), 200.0f, kit::ColorTextMuted(), "输出端口");
        stack.Advance(18.0f);
        for (int i = 0; i < static_cast<int>(t.out.size()); ++i) {
            portRow(t.out[i], false, i);
        }
    } else if (panelTab_ == 2) {
        if (!sel->charLock) {
            kit::Empty(draw, content, "masks", "未锁定角色",
                       "从「角色设定」节点连一条角色锁线到这个节点，角色才会在生成时保持一致。");
        } else {
            const kit::Rect hero = stack.Next(46.0f);
            kit::DrawRoundRect(draw, hero.min, hero.max, 10.0f, kit::ColorFillMuted(), kit::ColorLineSubtle(),
                               1.0f);
            kit::DrawIcon(draw, "masks", ImVec2(hero.min.x + 10.0f, hero.min.y + 12.0f), 20.0f,
                          theme::ToImU32(theme::Current().accentSecondary));
            kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kTitle), theme::font::kTitle,
                                 ImVec2(hero.min.x + 40.0f, hero.min.y + 9.0f), 160.0f, kit::ColorText(),
                                 sel->charName);
            char sub[96];
            std::snprintf(sub, sizeof(sub), "seed %d · 三视图 3/3 · 角度 4/4", sel->charSeed);
            kit::DrawTextClipped(draw, kit::FontAt(theme::font::kFine), theme::font::kFine,
                                 ImVec2(hero.min.x + 40.0f, hero.min.y + 27.0f), hero.width() - 50.0f,
                                 kit::ColorTextMuted(), sub);

            struct KV {
                const char* k;
                char v[32];
            };
            KV kvs[4] = {{"锁定强度", {}}, {"允许漂移", {}}, {"服装 / 发型", {}}, {"参考图", {}}};
            std::snprintf(kvs[0].v, sizeof(kvs[0].v), "%d%%", static_cast<int>(0.82f * 100.0f));
            std::snprintf(kvs[1].v, sizeof(kvs[1].v), "%d%%", static_cast<int>(0.12f * 100.0f));
            std::snprintf(kvs[2].v, sizeof(kvs[2].v), "%s", "锁");
            std::snprintf(kvs[3].v, sizeof(kvs[3].v), "%s", "3 张");
            kit::KVRow rows[4];
            for (int i = 0; i < 4; ++i) {
                rows[i].key = kvs[i].k;
                rows[i].value = kvs[i].v;
            }
            kit::KeyValues(draw, kit::RectAt(content.min.x, stack.Cur(), content.width(), kit::KeyValuesHeight(4)),
                           rows, 4);
            stack.Advance(kit::KeyValuesHeight(4) + 8.0f);
            kit::DrawTextClipped(draw, kit::FontAt(theme::font::kLabel), theme::font::kLabel,
                                 ImVec2(content.min.x, stack.Cur()), content.width(),
                                 kit::ColorTextMuted(), "锁定信息随连线传递；下游节点不再单独设 seed。");
        }
    } else {
        struct LogRow {
            const char* t;
            const char* m;
            ImU32 c;
        };
        std::vector<LogRow> rows;
        if (sel->state == WfFailed) {
            rows.push_back({"00:01.2", "任务已入队 · 第 2/3 次", kit::ColorTextSecondary()});
            rows.push_back({"00:04.8", "显存不足（OOM），stage 2/3 失败",
                            theme::ToImU32(theme::Current().statusDanger)});
        } else if (sel->state == WfRunning) {
            rows.push_back({"00:00.0", "开始执行", theme::ToImU32(theme::Current().accentInfo)});
            char b[64];
            std::snprintf(b, sizeof(b), "进度 %d%%", sel->progress);
            rows.push_back({"00:00.6", b, kit::ColorTextSecondary()});
        } else if (sel->state == WfDone) {
            rows.push_back({"00:00.8", "节点执行完成", theme::ToImU32(theme::Current().statusOk)});
        } else {
            kit::Empty(draw, content, "terminal", "暂无日志", "节点运行后，这里会逐条显示执行日志。");
        }
        for (const LogRow& r : rows) {
            if (stack.Remain() < 22.0f) {
                break;
            }
            const Rect rr = stack.Next(20.0f);
            kit::DrawTextClipped(draw, kit::MonoAt(theme::font::kFine), theme::font::kFine,
                                 ImVec2(rr.min.x, rr.min.y + 4.0f), 44.0f, kit::ColorTextMuted(), r.t);
            kit::DrawTextClipped(draw, kit::FontAt(theme::font::kLabel), theme::font::kLabel,
                                 ImVec2(rr.min.x + 48.0f, rr.min.y + 3.0f), rr.width() - 48.0f, r.c, r.m);
        }
    }

    // 底部动作
    const kit::Rect foot = kit::RectAt(panel.min.x, panel.max.y - footH, panel.width(), footH);
    draw->AddLine(ImVec2(foot.min.x, foot.min.y), ImVec2(foot.max.x, foot.min.y), kit::ColorLineSubtle(), 1.0f);
    if (sel->state == WfRunning) {
        kit::Progress(draw, kit::RectAt(foot.min.x + 14.0f, foot.min.y + 6.0f, foot.width() - 28.0f, 3.0f),
                      sel->progress / 100.0f, true, true);
    }
    kit::ButtonSpec bs;
    bs.variant = kit::ButtonVariant::Primary;
    bs.size = kit::ButtonSize::Small;
    bs.icon = "play";
    bs.disabled = sel->state == WfRunning;
    if (kit::Button(draw, kit::RectAt(foot.min.x + 14.0f, foot.min.y + 14.0f, 92.0f, 24.0f),
                    sel->state == WfFailed ? "重试" : "运行本节点", bs, "wf-run-node")) {
        for (Node& n : nodes_) {
            if (n.id == sel->id) {
                n.state = WfQueued;
                n.progress = 0;
            }
        }
    }
    kit::ButtonSpec ds;
    ds.variant = kit::ButtonVariant::Ghost;
    ds.size = kit::ButtonSize::Small;
    ds.icon = "x";
    if (kit::Button(draw, kit::RectAt(foot.max.x - 76.0f, foot.min.y + 14.0f, 62.0f, 24.0f), "删除", ds,
                    "wf-del-node")) {
        const int id = sel->id;
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                    [id](const Node& n) { return n.id == id; }),
                     nodes_.end());
        links_.erase(std::remove_if(links_.begin(), links_.end(),
                                    [id](const Link& l) { return l.from == id || l.to == id; }),
                     links_.end());
        selected_ = 0;
    }
}

// ============================================================
// 反馈流
// ============================================================

void WorkflowPage::DrawSkillFeed(ImDrawList* draw, const kit::Rect& box) {
    kit::DrawShadowed(draw, box.min, box.max, 10.0f, kit::GlassColor(), kit::ColorLineSubtle(), 1.0f,
                      theme::ShadowTier::Card);
    kit::DrawIcon(draw, "wand", ImVec2(box.min.x + 10.0f, box.min.y + 7.0f), 13.0f, kit::ColorAccent());
    kit::DrawTextClipped(draw, kit::FontBoldAt(theme::font::kLabel), theme::font::kLabel,
                         ImVec2(box.min.x + 28.0f, box.min.y + 6.0f), 120.0f, kit::ColorText(),
                         "流程生成反馈");
    draw->AddLine(ImVec2(box.min.x, box.min.y + 24.0f), ImVec2(box.max.x, box.min.y + 24.0f),
                  kit::ColorLineSubtle(), 1.0f);

    float ry = box.min.y + 28.0f;
    for (const std::string& line : feed_) {
        draw->AddCircleFilled(ImVec2(box.min.x + 13.0f, ry + 6.0f), 2.5f,
                              theme::ToImU32(theme::Current().statusOk), 8);
        kit::DrawTextClipped(draw, kit::MonoAt(theme::font::kFine), theme::font::kFine,
                             ImVec2(box.min.x + 22.0f, ry + 1.0f), box.width() - 30.0f,
                             kit::ColorTextSecondary(), line);
        ry += 18.0f;
    }
}

// ============================================================
// Draw
// ============================================================

void WorkflowPage::Draw(kit::Rect area, ImDrawList* draw) {
    if (nodes_.empty()) {
        BuildGraph();
    }

    // 运行中：按时间推进（0.22s 一个节点）
    if (busy_) {
        const float now = kit::Now();
        if (now >= busyUntil_) {
            bool any = false;
            for (Node& n : nodes_) {
                if (n.state == WfQueued) {
                    n.state = WfRunning;
                    n.progress = 8;
                    busyUntil_ = now + 0.55f;
                    any = true;
                    break;
                }
            }
            if (!any) {
                for (Node& n : nodes_) {
                    if (n.state == WfRunning) {
                        n.progress = std::min(100, n.progress + 26);
                        if (n.progress >= 100) {
                            n.state = WfDone;
                            busyUntil_ = now + 0.16f;
                        }
                        any = true;
                        break;
                    }
                }
            }
            if (!any) {
                busy_ = false;
                WorkspaceToast("流程执行完成");
            }
        }
    }

    constexpr float padding = 16.0f;
    constexpr float gap = 12.0f;
    constexpr float toolbarHeight = 44.0f;
    const float x = area.min.x + padding;
    const float y = area.min.y + padding;
    const float width = area.width() - 2.0f * padding;
    const bool stacked = area.width() < kFlowStackWidth;
    const float toolbarWidth = std::min(484.0f, width);
    const kit::Rect toolbar = kit::RectAt(x, y, toolbarWidth, toolbarHeight);
    const bool skillOnNewRow = width - toolbarWidth - gap < 260.0f;
    const kit::Rect skill = skillOnNewRow
                                ? kit::RectAt(x, toolbar.max.y + gap, width, toolbarHeight)
                                : kit::RectAt(toolbar.max.x + gap, y, width - toolbarWidth - gap,
                                              toolbarHeight);
    DrawTopBar(draw, toolbar, skill);

    float bodyTop = skill.max.y + gap;
    if (!feed_.empty()) {
        const float feedHeight = 34.0f + static_cast<float>(feed_.size()) * 18.0f;
        const kit::Rect feed = kit::RectAt(x, bodyTop, std::min(560.0f, width), feedHeight);
        DrawSkillFeed(draw, feed);
        bodyTop = feed.max.y + gap;
    }

    const float panelWidth = stacked ? width : (folded_ ? 232.0f : kPanelW);
    const float canvasWidth = stacked ? width : width - panelWidth - gap;
    const float libraryWidth = std::min(620.0f, canvasWidth);
    const float itemSpace = libraryWidth - 2.0f * kLibraryPadding;
    float itemWidth = 0.0f;
    for (const TypeDef& type : Types()) {
        const float textWidth = kit::MeasureClipped(kit::FontAt(theme::font::kLabel), theme::font::kLabel,
                                                    itemSpace, type.name);
        itemWidth = std::max(itemWidth, std::ceil(textWidth) + 30.0f);
    }
    itemWidth = std::min(itemWidth, itemSpace);
    const int count = static_cast<int>(Types().size());
    const int columns = std::max(1, static_cast<int>((itemSpace + kLibraryGap) / (itemWidth + kLibraryGap)));
    const int rows = (count + columns - 1) / columns;
    const float libraryHeight = kLibraryHeader + rows * kLibraryItemHeight +
                                (rows - 1) * kLibraryGap + kLibraryPadding;
    const float viewportBottom = area.min.y + WorkspaceViewportHeight();
    const float canvasBottom = stacked ? viewportBottom : viewportBottom - libraryHeight - gap - padding;
    const kit::Rect canvas = kit::RectAt(x, bodyTop, canvasWidth, std::max(140.0f, canvasBottom - bodyTop));
    const kit::Rect library = kit::RectAt(x, canvas.max.y + gap, libraryWidth, libraryHeight);
    const float panelTop = stacked ? library.max.y + gap : bodyTop;
    const float panelHeight = folded_ ? toolbarHeight :
                              (stacked ? 468.0f : std::max(200.0f, library.max.y - panelTop));
    const kit::Rect panel = kit::RectAt(area.max.x - padding - panelWidth, panelTop, panelWidth, panelHeight);
    SetPageContentHeight(std::max(library.max.y, panel.max.y) + padding - area.min.y);

    DrawCanvas(draw, canvas, stacked);
    DrawLibrary(draw, library, itemWidth);

    // 检查器浮层壳
    kit::DrawShadowed(draw, panel.min, panel.max, 14.0f, kit::GlassColor(), kit::ColorLineSubtle(), 1.0f,
                      theme::ShadowTier::Card);
    DrawInspector(draw, panel);
}

} // namespace shine::pages
