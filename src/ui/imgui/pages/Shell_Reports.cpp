#include "ui/imgui/pages/Shell.h"

#include "core/Async.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <yyjson.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace shine::pages {
using namespace shine::kit;

// ---------------------------------------------------------------- P4.6d 校验报告
//
// 真值源：novelcore::RunContinuityChecks 落盘的 <project>/work/ch<NNN>/v08_continuity.json
// —— V8 连续性，也就是 pipeline T12 / T15 的产出，字段见 NovelContinuity.cpp:467。
//
// 早先这一页只给诚实空态，紧邻的 DrawReportModal 更糟：它是个**永远打不开的空壳** ——
// 驱动它的 reportDetail_ 全代码只有 `= -1` 初始化与复位，从没有任何地方把它设成 ≥0，
// 于是那个模态框一次都没出现过，Esc 逐层关闭里的那一层也永远走不到。
// 现在列表按章列真报告，点行走模态看逐项结论。

namespace {

// work/ch<NNN> → NNN。返回 0 = 不是按章组织的报告目录（跳过，不拿它当一份空报告充数）。
int ChapterOrdFromDir(const std::filesystem::path& dir) {
    const std::string name = util::PathToUtf8(dir.filename());
    if (name.size() < 3 || name.compare(0, 2, "ch") != 0) {
        return 0;
    }
    int ord = 0;
    for (std::size_t i = 2; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        ord = ord * 10 + (name[i] - '0');
    }
    return ord;
}

} // namespace

theme::Tone Shell::SeverityTone(std::string_view severity) {
    if (severity == "high") {
        return theme::Tone::Danger;
    }
    if (severity == "medium") {
        return theme::Tone::Warn;
    }
    return theme::Tone::Idle;  // low（设计稿给的是 var(--text-muted)）
}

theme::Tone Shell::ReportTone(const ChapterReport& report) {
    if (report.failed > 0) {
        return theme::Tone::Danger;
    }
    if (report.unverified > 0) {
        return theme::Tone::Warn;  // 数据不足：既不算通过，也别粉饰成失败
    }
    return theme::Tone::Ok;
}

std::string Shell::ReportSummary(const ChapterReport& report) {
    // 三态分开报，不合并成一个数：failed 与 unverified 是两回事。
    // ⚠️ failed==0 且 unverified>0 时**不能**写「全部通过」—— 那 2 条根本没核对过。
    std::string s;
    if (report.failed > 0) {
        s = std::to_string(report.failed) + " 未过 · ";
    } else if (report.unverified > 0) {
        s = "无未过项 · ";
    } else {
        s = "全部通过 · ";
    }
    if (report.unverified > 0) {
        s += std::to_string(report.unverified) + " 未核对 · ";
    }
    s += std::to_string(report.pairs) + " 镜对";
    return s;
}

std::vector<Shell::CheckRow> Shell::BuildCheckRows(const ChapterReport& report) {
    std::vector<CheckRow> rows;
    rows.reserve(report.issues.size() + report.notes.size());
    for (const ReportIssue& issue : report.issues) {
        CheckRow row;
        row.code = issue.code;
        row.level = issue.severity;
        row.levelTone = SeverityTone(issue.severity);
        // 结论口径照设计稿 Shell.jsx:292：未过且 level=low 记「提示」，否则记「未过」。
        const bool hint = issue.severity == "low";
        row.verdict = hint ? "提示" : "未过";
        row.verdictTone = hint ? theme::Tone::Warn : theme::Tone::Danger;
        row.detail = issue.detail;
        rows.push_back(std::move(row));
    }
    // notes 形如「C3：数据不足…」—— 冒号前是规则码，后面是人话。
    // ⚠️ 拆不出「码：说明」就整条丢掉：宁可少一行，也不编一个码出来顶账。
    for (const std::string& note : report.notes) {
        const std::size_t sep = note.find("：");
        if (sep == std::string::npos || sep == 0) {
            continue;
        }
        CheckRow row;
        row.code = note.substr(0, sep);
        row.level = "-";
        row.levelTone = theme::Tone::Idle;
        row.verdict = "未核对";
        row.verdictTone = theme::Tone::Info;
        row.detail = note.substr(sep + 1);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Shell::ParseContinuityReport(const std::filesystem::path& file, ChapterReport& out) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        return false;  // 损坏的报告当没有 —— 不猜、不补
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    const char* stage = yyjson_get_str(yyjson_obj_get(root, "stage"));
    if (stage == nullptr || std::string_view{stage} != "V8") {
        yyjson_doc_free(doc);
        return false;  // work/ 下别的 JSON —— 不当校验报告充数
    }
    const auto num = [&root](const char* key) {
        return static_cast<int>(yyjson_get_int(yyjson_obj_get(root, key)));
    };
    out.chapterId = num("chapter_id");
    out.shots = num("shots");
    out.pairs = num("pairs");
    out.rulesChecked = num("rules_checked");
    out.failed = num("failed");
    out.unverified = num("unverified");
    if (yyjson_val* issues = yyjson_obj_get(root, "issues"); yyjson_is_arr(issues)) {
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(issues, &iter);
        for (yyjson_val* item = nullptr; (item = yyjson_arr_iter_next(&iter)) != nullptr;) {
            const auto str = [item](const char* key) {
                const char* raw = yyjson_get_str(yyjson_obj_get(item, key));
                return raw != nullptr ? std::string(raw) : std::string{};
            };
            ReportIssue issue;
            issue.code = str("code");
            issue.severity = str("severity");
            issue.detail = str("detail");
            out.issues.push_back(std::move(issue));
        }
    }
    if (yyjson_val* notes = yyjson_obj_get(root, "notes"); yyjson_is_arr(notes)) {
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(notes, &iter);
        for (yyjson_val* item = nullptr; (item = yyjson_arr_iter_next(&iter)) != nullptr;) {
            if (const char* raw = yyjson_get_str(item); raw != nullptr) {
                out.notes.emplace_back(raw);
            }
        }
    }
    yyjson_doc_free(doc);
    return true;
}

void Shell::RequestReportScan() {
    if (layout_.projectRoot.empty()) {
        return;
    }
    const std::filesystem::path root = layout_.projectRoot / "work";
    reportScanning_ = true;
    reportScanRoot_ = layout_.projectRoot;
    async::RunOnWorker([this, root] {
        std::vector<ChapterReport> found;
        std::error_code ec;
        if (std::filesystem::is_directory(root, ec)) {
            for (auto it = std::filesystem::directory_iterator(root, ec);
                 it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) {
                    break;
                }
                if (!it->is_directory(ec)) {
                    continue;
                }
                const int ord = ChapterOrdFromDir(it->path());
                if (ord <= 0) {
                    continue;
                }
                const std::filesystem::path file = it->path() / "v08_continuity.json";
                ChapterReport report;
                if (!ParseContinuityReport(file, report)) {
                    continue;  // 目录在但报告没落盘 / 损坏 —— 不拿它凑一条空报告
                }
                report.chapterOrd = ord;
                report.path = util::PathToUtf8(file);
                found.push_back(std::move(report));
            }
        }
        std::stable_sort(found.begin(), found.end(),
                         [](const ChapterReport& a, const ChapterReport& b) {
                             return a.chapterOrd < b.chapterOrd;
                         });
        async::PostToUi([this, rows = std::move(found)] {
            reportScanning_ = false;
            reports_ = std::move(rows);
            // 排下一次重扫的时点。⚠️ 这一行以前写的是 `= 0.0f`，于是
            // DrawDockReports 里的 `aged` 恒为假 —— 换工程之外再也没有第二个触发点，
            // 跑完一个阶段后这一页会一直停在「尚无校验报告」。这里给真的时点。
            reportRefreshAt_ = ImGui::GetTime() + 2.0f;
            // 重扫后别让模态指着一条已经没了的报告。-1（本来就没打开）保持 -1。
            if (reportDetail_ >= static_cast<int>(reports_.size())) {
                reportDetail_ = reports_.empty() ? -1 : 0;
            }
        });
    });
}

void Shell::ExportReport(int index) {
    if (index < 0 || index >= static_cast<int>(reports_.size())) {
        return;
    }
    const ChapterReport report = reports_[static_cast<std::size_t>(index)];
    if (report.path.empty()) {
        return;
    }
    const std::filesystem::path src(report.path);
    // 逐字节复制原报告，不重新序列化 —— 序列化会把排版和未知字段抹掉。
    // 写盘走 worker：UI 线程不做同步 IO。
    async::RunOnWorker([this, src] {
        const std::optional<std::string> bytes = util::ReadFileBytes(src);
        if (!bytes.has_value()) {
            async::PostToUi([this, src] {
                PushLog("err", "导出校验报告失败：读不到 " + util::PathToUtf8(src));
            });
            return;
        }
        const std::filesystem::path dst = layout_.projectRoot / "output" / src.filename();
        const bool ok = util::WriteFileEnsuredDir(dst, *bytes);
        async::PostToUi([this, dst, ok] {
            PushLog(ok ? "info" : "err",
                    ok ? "已导出校验报告 " + util::PathToUtf8(dst)
                       : "导出校验报告失败：写不进 " + util::PathToUtf8(dst));
        });
    });
}

void Shell::DrawDockReports(Rect body, ImDrawList* draw) {
    // 换工程 / 2 秒后重扫（与 DrawDockArtifacts 同一套判定，不发明第二份）。
    const bool stale = reportScanRoot_ != layout_.projectRoot;
    const bool aged = reportRefreshAt_ > 0.0f && ImGui::GetTime() >= reportRefreshAt_;
    if (stale || aged) {
        reportScanning_ = false;  // 上一次的结果作废
        RequestReportScan();
    }
    if (layout_.projectRoot.empty()) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 520.0f, body.min.y + 90.0f},
              "target", "未打开工程", "打开工程并跑完 T12 / T15 后，逐项校验报告会在此列出");
        return;
    }
    if (reports_.empty() && !reportScanning_) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 620.0f, body.min.y + 100.0f},
              "target", "尚无校验报告", "校验只比较状态，不让 LLM 自行猜测连续性 · 跑完 T12 / T15 后在此查看逐项结论");
        return;
    }

    // 行 = code(70) | name(90) | 结论 Tag | 尾部 chevron。设计稿这一组是 flex 行
    // （Shell.jsx:255-262）**没有列头**，所以这里不套 DataTable，也不加排序。
    //
    // ⚠️ 同 DrawDockArtifacts：原来这里是 `if (y + 26.0f > body.max.y - 20.0f) break;`
    //    —— 又一处**静默截断**，第 N+1 章之后的报告无声消失，界面上看不出 work/ 下还有
    //    v08_continuity.json。改成画**全部**行，列表自己滚；页脚留在滚动区**外面**。
    constexpr float kRepRowH = 26.0f;
    constexpr float kRepFootH = 18.0f;
    const Rect listArea{body.min.x, body.min.y, body.max.x, body.max.y - kRepFootH};
    kit::ScrollRegion list("dock-report-list", listArea);
    if (list) {
        // 内容必须画在 child 自己的 draw list 上，否则裁剪无效（见 Scroll.h）。
        ImDrawList* ldraw = list.drawList();
        const Rect inner = list.content();
        float y = inner.min.y;
        for (std::size_t i = 0; i < reports_.size(); ++i) {
            const ChapterReport& report = reports_[i];
            const Rect line{inner.min.x, y, inner.min.x + 560.0f, y + kRepRowH};
            std::string code = "ch" + std::to_string(report.chapterOrd);
            while (code.size() < 5) {
                code.insert(code.begin() + 2, '0');
            }
            // 报告行走 kit::ListRow：hover 底 / 命中 / 标题 Y / chevron 在里面。
            // 原来 `code` 写 `y + 8.0f`（Δ +0.25）、名称写 `y + 6.0f`（Δ −0.75）——
            // 同一行里两段字差 1px，Tag 又在第三档上。
            //
            // 这一行是「码 + 名称 + 结论 Tag」三段，没有左图标：
            // 码当 trailing 之前先用 icon 位？不行 —— 码是等宽小字不是图标。
            // 所以码并进标题（用 · 分隔会改视觉），这里改为：标题给名称，
            // 码用 ListRow 的 icon 位画不了，改由本函数在 ListRow **之前**
            // 画（它是行内最左的一段，且不参与 hover）。
            DrawTextClipped(ldraw, MonoAt(10.5f), 10.5f,
                            ImVec2(line.min.x + 4.0f,
                                   kit::CenterTextY(MonoAt(10.5f), 10.5f, line.center().y)),
                            62.0f, ColorTextMuted(), code);
            kit::ListRowSpec spec;
            spec.id = chromeInteractive_ ? "dock-report-" + std::to_string(i) : std::string_view{};
            spec.title = "连续性 C1-C12";
            spec.titleSize = 12.5f;
            spec.paddingX = 74.0f; // 让开左边的等宽码（62 + 8 间距）
            spec.suppressed = !chromeInteractive_;
            // 结论 Tag 画在 ListRow 之后：Tag 自带量宽、垂直居中自己算，
            // 不走 ListRow 的 trailing（trailing 是纯文字，画不了 Tag）。
            //
            // ⚠️ 命中**只有 ListRow 这一次**。别在下面再补一次 HitTest：
            // ImGui 同窗口内先注册者独占 HoveredId（imgui.cpp:5161），第二次
            // 永远 clicked=false —— 而症状是「报告点不开」，编译与截图全绿。
            const kit::Hit hit = kit::ListRow(ldraw, line, spec);
            const std::string summary = ReportSummary(report);
            const float tagW = TagWidth(summary, true, false);
            Tag(ldraw, RectAt(line.min.x + 232.0f, line.center().y - TagHeight(true) * 0.5f, tagW,
                              TagHeight(true)),
                summary, ReportTone(report), /*small=*/true);
            if (hit.clicked) {
                reportDetail_ = static_cast<int>(i);
            }
            y += kRepRowH;
        }
        list.setContentHeight(y - inner.min.y);
    }
    const std::string foot = "共 " + std::to_string(reports_.size()) +
                             " 章有报告 · 来自 work/ch<NNN>/v08_continuity.json · "
                             "点击任一行查看逐项结论（检查 / 级别 / 结论 / 详情）";
    DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f),
                    body.width(), ColorTextMuted(), foot);
}

void Shell::DrawReportModal() {
    if (reportDetail_ < 0) {
        return;
    }
    if (reportDetail_ >= static_cast<int>(reports_.size())) {
        reportDetail_ = -1;  // 重扫后这条没了 —— 别让模态指着一个不存在的下标
        return;
    }
    const ChapterReport& report = reports_[static_cast<std::size_t>(reportDetail_)];
    const std::vector<CheckRow> rows = BuildCheckRows(report);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // ⚠️ 必须是 **foreground** draw list，不能用 GetWindowDrawList()。
    //    外壳的页面 / 底栏各自跑在 ScrollRegion(BeginChild) 里，而 child 是在
    //    父窗口那份 draw list **之后**渲染的 —— 画在父 list 上的浮层会被整片盖住。
    //    实测症状：遮罩和面板都画了，但工作区的 KPI 卡片压在模态上面，
    //    截图上「模态」只剩一条表头带，manifest 还写着 saved。
    //    本仓 Tooltip 早就用同一招并在 Widgets.cpp:793 记了原因。
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    // 设计稿内联 width 640 / height min(74vh,660)（Shell.jsx:272）—— 尺寸原样保留。
    const float w = 640.0f;
    const float h = std::min(660.0f, display.y * 0.74f);
    const float headerH = 46.0f;
    const float footerH = 12.0f * 2.0f + ButtonHeight(ButtonSize::Medium) + 1.0f;
    // 外壳走 kit::ModalFrameRect：遮罩 / 圆角 / 投影 / 头 / 体 / 脚 全部由 kit 画。
    // 原来这里是页面层第五份私有的模态外壳 —— 前四份在 pages 各页与 ProjectHub.cpp（上一轮收掉），
    // 而 `kit::Modal` 本体因为一直没有调用点，一直是零调用。
    //
    // ⚠️ **这一处是「遮罩绝不能注册热区」的高危位置**：外壳先画、下面那些
    //    「导出 / ×」按钮后画，所以若遮罩注册成全屏 InvisibleButton，它会
    //    **先**拿到 HoveredId ⇒ 这一模态里每一个按钮都永远点不动，而外观全都
    //    画得好好的。项目中心的对话框不会中招（它最后画，遮罩排在按钮之后）——
    //    两种顺序结论相反，靠读代码判断极易搞反。kit::ModalFrameRect 里遮罩
    //    只画不注册，`kit::DuplicateHitCount` 兜底。
    //
    // 头高：kit 的 `.modal-h` 是 14/18 padding + 18 行高 = 47，而设计稿这一处
    // 写的是 46（行高由头右侧那两个按钮决定）。所以**不传标题**（头高 0），
    // 整行头由页面层自己画 —— 传了标题会多出一行 47 的头，与下面这行 46 的
    // 头叠在一起。
    const kit::ModalFrame mf = kit::ModalFrameRect(
        draw, RectAt(0.0f, 0.0f, display.x, display.y), {}, {}, w, h, /*footerButtons=*/2);
    const Rect bounds = mf.frame;
    // 点遮罩关闭。⚠️ 这里**不能**用 kit::HitTest：全屏遮罩若先注册成 InvisibleButton，
    // 它会先拿到 HoveredId，模态里所有按钮（同一帧、位置落在遮罩内）永远 hovered=false。
    // 改成手算点在不在面板内：既不注册 item（所以不抢 HoveredId），
    // 也避开了 ImGui::IsMouseHoveringRect —— 后者在**本工程**会 0xC0000005
    // （fault offset 0x8b8597，见 refactor/PROGRESS.md 的那次记录）。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bounds.contains(ImGui::GetIO().MousePos)) {
        reportDetail_ = -1;
    }

    // ---- .modal-h（ui.css:955-965）：padding 14/18 + gap 10 + 1px 下边 ----
    // ⚠️ 标题里的规则组写作 "C1-C12"（ASCII 连字符）而不是设计稿的 en dash：
    //    U+2013 不在图集的字形基线里，用它会画成缺字形方块。
    const std::string title = "连续性 C1-C12 · 第 " + std::to_string(report.chapterOrd) + " 章";
    DrawIcon(draw, "target", ImVec2(bounds.min.x + 18.0f, bounds.min.y + 15.0f), 17.0f, ColorAccent());
    DrawTextClipped(draw, FontBoldAt(14.0f), 14.0f, ImVec2(bounds.min.x + 45.0f, bounds.min.y + 15.0f),
                    260.0f, ColorText(), title);
    ButtonSpec exportSpec;
    exportSpec.variant = ButtonVariant::Ghost;
    exportSpec.size = ButtonSize::Small;
    exportSpec.icon = "download";
    const std::string exportLabel = "导出 JSON";
    const float exportW = ButtonWidth(ButtonSize::Small, 13.0f,
                                       FontAt(10.5f)->CalcTextSizeA(
                                           10.5f, 1e9f, 0.0f, exportLabel.data(),
                                           exportLabel.data() + exportLabel.size())
                                           .x);
    if (Button(draw, RectAt(bounds.max.x - 18.0f - 22.0f - 8.0f - exportW, bounds.min.y + 12.0f,
                            exportW, ButtonHeight(ButtonSize::Small)),
               exportLabel, exportSpec, "report-export")) {
        ExportReport(reportDetail_);
    }
    if (IconButton(draw, RectAt(bounds.max.x - 18.0f - 22.0f, bounds.min.y + 12.0f, 22.0f, 22.0f),
                   "x", false, false, "report-close")) {
        reportDetail_ = -1;
    }
    draw->AddLine(ImVec2(bounds.min.x, bounds.min.y + headerH - 0.5f),
                  ImVec2(bounds.max.x, bounds.min.y + headerH - 0.5f), ColorLineSubtle(), 1.0f);

    // ---- .modal-b（ui.css:966-969）：fill-muted 底 + padding 18 + 纵向滚 ----
    //
    // ⚠️ 这里**必须**用 draw->PushClipRect 自己裁，**不能**用 ScrollRegion（BeginChild）。
    //    BeginChild 会另开一个 ImGuiWindow，而 ImGui 的渲染次序是「后建的先画」——
    //    这个 child 就沉到了工作区那个 child 底下，父 draw list 上的部分（面板底、
    //    标题栏、表头）还看得见，落在 child 里的表体却被工作区整片盖住。
    //    实测症状：模态只剩一条表头带，下面全是工作区的 KPI 卡片。
    //    模态是单帧定尺的浮层，本来就不需要独立窗口 —— 全留在同一个 draw list 上，
    //    z 序由调用顺序决定，不用赌 ImGui 的窗口次序。
    const Rect body{bounds.min.x, bounds.min.y + headerH, bounds.max.x, bounds.max.y - footerH};
    const float tableX = bounds.min.x + 18.0f;
    const float tableW = w - 36.0f;
    const float headH = TableHeaderHeight(true);
    const float rowH = TableRowHeight(true);
    const float summaryH = TagHeight(true) + 10.0f;
    const float contentH =
        18.0f + summaryH + headH + (rows.empty() ? rowH : static_cast<float>(rows.size()) * rowH) + 18.0f;
    const float maxScroll = std::max(0.0f, contentH - body.height());
    if (body.contains(ImGui::GetIO().MousePos)) {
        reportScroll_ -= ImGui::GetIO().MouseWheel * 40.0f;
    }
    reportScroll_ = std::clamp(reportScroll_, 0.0f, maxScroll);

    draw->PushClipRect(body.min, body.max, true);
    // .modal-b 的底是 --fill-muted（Shell.jsx:280 内联），不是模态框的 overlay。
    draw->AddRectFilled(body.min, body.max, ColorFillMuted());
    float y = body.min.y + 18.0f - reportScroll_;

    // 摘要 Tag，tone=info（设计稿 Shell.jsx:281-283）。
    const std::string summary = ReportSummary(report) + " · 阈值：high 未过即 FAIL";
    Tag(draw, RectAt(tableX, y, TagWidth(summary, true, false), TagHeight(true)), summary,
        theme::Tone::Info, /*small=*/true);
    y += summaryH;

    // 表格走 kit::DataTable：表头 / 列宽 / 单元格 / 排序命中 / Tag 列全在里面。
    //
    // ⚠️ 原来这里在 DataTable **之前**还有一段手写表头（4 个标题 + 底色 + 下边线）。
    //    迁移时若只删单元格不删表头，界面上就是**两层表头叠在一起** ——
    //    编译过、截图里有东西、看不出是重影。整段删掉，列宽写进 tableCols。
    //
    // 设计稿那张表**渲染 5 个 td 却只写了 4 个标题**（Shell.jsx:285 vs 289-293），
    // 照抄会留下一列没有表头的裸格子；这里保留它的 4 个标题，但都落在有内容的列上。
    //
    // 原来这里是页面层手写的四列表：表头 `y + 7.0f`（Δ −1.75）、单元格
    // `y + 7.0f`（Δ −1.75 / −1.5）、Tag `y + 6.0f`、hover 底是手算
    // `line.contains(MousePos)`（**没有 id、没有计数**，于是 hover 探针分不清
    // 「坐标点空了」和「hover 链路断了」）。而 kit::DataTable 那份按
    // ui.css:721-768 算好、零调用。
    //
    // 保留的差异：详情列超宽要挂 tooltip（kit 的表格不代做这个），所以超宽
    // 判定仍在外面算一次 —— 但不再自己画单元格。
    // 列：检查(mono+accent，规则码) | 级别(tag) | 结论(tag) | 详情(剩余宽)。
    // 宽度照原表：56 / 88 / 88 / 余量。
    const std::vector<kit::TableColumn> tableCols{
        {"检查", 56.0f, /*numeric=*/false, /*centered=*/false, /*sortable=*/false, /*mono=*/true,
         /*tag=*/false},
        {"级别", 88.0f, false, false, false, false, /*tag=*/true},
        {"结论", 88.0f, false, false, false, false, /*tag=*/true},
        {"详情", 0.0f, false, false, false, false, false},
    };
    std::vector<kit::TableRow> tableRows;
    if (rows.empty()) {
        // 没有 issue 也没有 note = 这一章一条都没判出来。照实说，别写成「全部通过」。
        kit::TableRow empty;
        empty.cells = {"", "", "",
                       report.pairs > 0 ? "本组没有记下任何不一致项" : "没有可比较的镜对"};
        empty.tones = {theme::Tone::Idle, theme::Tone::Idle, theme::Tone::Idle, theme::Tone::Idle};
        tableRows.push_back(empty);
    } else {
        for (const CheckRow& row : rows) {
            kit::TableRow tr;
            tr.cells = {row.code, row.level, row.verdict, row.detail};
            tr.tones = {theme::Tone::Idle, row.levelTone, row.verdictTone, theme::Tone::Idle};
            tableRows.push_back(tr);
        }
    }
    kit::TableSort tableSort; // 无可排序列（设计稿这一组没有排序交互）
    // 高度**由 DataTable 自己算**（表头 + 行数），不在这儿再手算一遍 ——
    // 两处各算一次的话，改了 compact 或行高就会有一处对不上，而界面上
    // 表现是「表格比框矮一截 / 溢出框」，很容易被当成布局问题去查别处。
    const float tableH = kit::TableHeaderHeight(true) + kit::TableRowHeight(true) *
                                                       static_cast<float>(tableRows.size());
    const Rect tableRect{tableX, y, tableX + tableW, y + tableH};
    y += kit::DataTable(draw, tableRect, tableCols, tableRows, tableSort, /*compact=*/true,
                        "report-table");
    // 详情列超宽的 tooltip：DataTable 不代做（它只管画），全文仍要能看全。
    if (!rows.empty()) {
        const float detailDrawn =
            MeasureClipped(FontAt(12.0f), 12.0f, 1e9f, rows.front().detail);
        if (detailDrawn >= tableW - 56.0f - 88.0f - 88.0f - 16.0f) {
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const Rect line{tableX, tableRect.min.y + headH + rowH * static_cast<float>(i),
                                tableX + tableW, tableRect.min.y + headH + rowH * static_cast<float>(i + 1)};
                if (line.contains(ImGui::GetIO().MousePos)) {
                    Tooltip(line, rows[i].detail);
                    break;
                }
            }
        }
    }
    draw->PopClipRect();

    // 滚动条：内容装不下时才画（thumb = 可见比例，track = 主体高度）。
    if (maxScroll > 0.0f) {
        const float trackW = 4.0f;
        const float trackX = bounds.max.x - 18.0f - trackW;
        const float thumbH = std::max(24.0f, body.height() * (body.height() / contentH));
        const float thumbY =
            body.min.y + (body.height() - thumbH) * (reportScroll_ / maxScroll);
        draw->AddRectFilled(ImVec2(trackX, body.min.y), ImVec2(trackX + trackW, body.max.y),
                            ColorFillMuted());
        draw->AddRectFilled(ImVec2(trackX, thumbY), ImVec2(trackX + trackW, thumbY + thumbH),
                            ColorLineStrong());
    }

    // ---- .modal-f（ui.css:970-975）：padding 12/18 + 1px 上边 ----
    draw->AddLine(ImVec2(bounds.min.x, bounds.max.y - footerH + 0.5f),
                  ImVec2(bounds.max.x, bounds.max.y - footerH + 0.5f), ColorLineSubtle(), 1.0f);
    // ⚠️ 原来这行写 `bounds.max.y - 12.0f - 14.0f`，比**同一页脚里**的「关闭」按钮
    //    （`max.y - 12 - ButtonHeight(Medium=30)`，中心 `max.y - 27`）低 6.25px
    //    —— 页脚左边一句说明、右边一个按钮，两个东西差了 6px，一眼可见。
    //    页脚高 footerH、行盒按自身 10.5px 取半高。
    DrawTextClipped(draw, FontAt(10.5f), 10.5f,
                    ImVec2(bounds.min.x + 18.0f,
                           kit::CenterTextY(FontAt(10.5f), 10.5f,
                                            bounds.max.y - 12.0f - footerH * 0.5f)),
                    w - 120.0f, ColorTextMuted(),
                    "校验只比较状态，不让 LLM 自行猜测连续性 · Esc 关闭");
    ButtonSpec closeSpec;
    closeSpec.variant = ButtonVariant::Ghost;
    if (Button(draw, RectAt(bounds.max.x - 18.0f - 72.0f, bounds.max.y - 12.0f - ButtonHeight(ButtonSize::Medium),
                            72.0f, ButtonHeight(ButtonSize::Medium)),
               "关闭", closeSpec, "report-dismiss")) {
        reportDetail_ = -1;
    }
}


} // namespace shine::pages
