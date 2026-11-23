#include "ui/imgui/verify/ReviewVerdict.h"

#include "core/Log.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Image.h" // RequestedImageCount / PendingImageCount（诊断信息）
#include "ui/imgui/verify/Capture.h"

#include <cstddef>
#include <string>
#include <vector>

namespace shine::imguiverify::detail {

// ---- 判据二：同一工作区在 5 套主题下的像素必须两两不同 ----
// 只判「PNG 写出来了」的旧门禁会漏掉「主题压根没加载」这种整轮崩掉的情况。
void FinishVerdict(Session& session) {
    const std::filesystem::path& manifest = session.manifest;
    const ReviewResult& result = session.result;

    int identicalPairs = 0;
    for (const auto& [workspace, hashes] : session.themedHashes) {
        for (std::size_t i = 0; i < hashes.size(); ++i) {
            for (std::size_t j = i + 1; j < hashes.size(); ++j) {
                if (hashes[i] != 0 && hashes[i] == hashes[j]) {
                    ++identicalPairs;
                    shine::log::Error(
                        "review: ws={} themes #{} and #{} produced BYTE-IDENTICAL pixels — "
                        "theme switch did not take effect",
                        shine::pages::WorkspaceIcon(workspace), i, j);
                }
            }
        }
    }

    // ---- 判据三：受控图之间不许出现逐字节相同的两张 ----
    // 症状是「图在、manifest 记 saved、overall=PASS，但内容是上一张」—— 不报错、
    // 肉眼也未必立刻看出来。上一轮 side-tree 与 side-tree-empty-chapter 就是这么撞的。
    int drivenDuplicates = 0;
    for (std::size_t i = 0; i < session.drivenHashes.size(); ++i) {
        for (std::size_t j = i + 1; j < session.drivenHashes.size(); ++j) {
            if (session.drivenHashes[i] != 0 && session.drivenHashes[i] == session.drivenHashes[j]) {
                ++drivenDuplicates;
                shine::log::Error("review: {} and {} produced BYTE-IDENTICAL pixels — "
                                  "至少有一张没拍到它承诺的状态",
                                  session.drivenNames[i], session.drivenNames[j]);
            }
        }
    }

    // ---- 判据四：全程不得出现「反向矩形」----
    //
    // `kit::Rect` 的四参构造是 (minX,minY,maxX,maxY)，人写出来十有八九是 (x,y,w,h)。
    // 传错不报编译错，只是 max < min，于是 DrawRoundRect / HitTestImpl 把控件整块丢掉：
    // 不画、不可点，日志和 manifest 全绿。上面 52 张覆盖全部 7 个工作区 + 5 套主题，
    // 跑完还有计数就说明有一个控件在某个工作区里是**隐形**的。
    // 这个门禁比 tools\find-rect-wh-misuse.ps1 的启发式扫描强：扫描靠猜第 3/4 参像不像
    // 尺寸，运行时兜底不猜 —— 谁真的传反了就自己举手。
    const int inverted = kit::InvertedRectCount();
    if (inverted > 0) {
        shine::log::Error("review: {} 次反向矩形，最后一次 = {} —— 有控件被整块丢弃",
                          inverted, kit::LastInvertedRect());
    }
    // 同一帧重叠热区：ImGui 先注册者独占，后一个 InvisibleButton 永远
    // clicked=false。它上面的控件**画得出来**（不画完不等于画不出），所以
    // 静息截图与 hover 探针对它零覆盖 —— 漏进判据就只是一行日志。
    const int duplicateHits = kit::DuplicateHitCount();
    if (duplicateHits > 0) {
        shine::log::Error("review: {} 次同帧重叠热区，最后一次 = {} —— 有一个控件画得出但点不动",
                          duplicateHits, kit::LastDuplicateHit());
    }
    // 悬停探针全过才算数（-1 = 这一段根本没跑到，判它不通过而不是当它通过）。
    if (session.hoverProbesPassed < session.hoverProbeTotal) {
        shine::log::Error("review: 悬停探针只过了 {}/{} —— 有控件的 hover 链路是断的，"
                          "而静息态截图看不出来",
                          session.hoverProbesPassed, session.hoverProbeTotal);
    }
    // 快捷键判据：-1 = 这一段没跑到，判它不通过而不是当它通过。
    if (session.shortcutsPassed < session.shortcutTotal) {
        shine::log::Error("review: 快捷键只有 {}/{} 按下去真的改变了界面状态 —— "
                          "面板上写着它们，其中有条是空动作",
                          session.shortcutsPassed, session.shortcutTotal);
    }

    // ⚠️ overall 行必须存在且与退出码一致（脚本 :66 要求 PASS↔0 / FAIL↔1）。
    //    reportScanConverged / bookSnapshotConverged 也进判据：没拍成就是没拍成，
    //    不能因为「其它图都写出来了」就整轮报绿。
    //    scrollFailed 同理，而且**必须**进判据：第一版就是漏了它 —— 判据全绿，
    //    而 overview-vstages 拍的其实是右栏顶部，矩阵那张等于没拍。
    //    overlayClicksPassed 同理：浮层按钮点不动这件事**在像素上看不出来**，
    //    静息截图与 hover 探针对它零覆盖。漏进判据的话，那条正交信号就只是
    //    一行日志，overall 照样 PASS —— 那是假绿，不是验证。
    // sheetImagesSettled 同理，而且它挡的正是最难自查的一类：真设定图是**异步**上屏的，
    // 而「抓图太早」与「图片链断了」在截图上完全一样（都是占位画）。没有这条判据时，
    // 界面上少了一整块真图而 overall 照样 PASS —— 假绿。
    if (!session.sheetImagesSettled) {
        shine::log::Error("review: 真设定图没落地（等满 {} 帧后 requested={} / pending={}）—— "
                          "requested=0 表示资产网格那一支**一次都没请求过图**（调用点没跑到，"
                          "或筛选后没有带设定图的实体）；pending>0 表示 worker/上传链卡住。"
                          "两种情况都让资产网格画成 `Art` 占位，等于「显示真实设定图」这条链"
                          "在本轮没被验过。查链路，别改判据",
                          kReportWaitFrameCap, kit::RequestedImageCount(),
                          kit::PendingImageCount());
    }
    // sheetImagesRecovered 是「纹理被淘汰后会自己回来」那条约束的判据。它挡的正是
    // 最难自查的一类缺陷：只在 `State==NotLoaded` 时才请求（我第一版就是这样），
    // 于是纹理被 LRU 淘汰后**永远不再出现** —— 不报错、不崩、静息截图照样 saved。
    if (!session.sheetImagesRecovered) {
        shine::log::Error("review: 清空 GPU 纹理缓存之后真设定图**没有回来** —— "
                          "`kit/Image.h` 的约束 1（`RequestImage` 每帧都要调）被写坏了："
                          "「图片闪过一次就没了」，而不报任何错。查 RequestImage 有没有"
                          "被写成「只在 State==NotLoaded 时请求」，别改判据");
    }
    // viewerFullSettled 是查看器那条链的判据（大图档 2048）。它挡的是同一类假绿的
    // 第三个实例：大图**只在查看器开着的那一帧**才被请求，所以「没请求过」与
    // 「请求了没落地」在截图上都是 `Art` 占位。没有它，查看器里一张真图都没上屏
    // 而 overall 照样 PASS。
    if (!session.viewerFullSettled) {
        shine::log::Error("review: 查看器里的真图没上屏（大图档 requested={} / pending={}）—— "
                          "requested=0 表示查看器那一帧**一次都没请求大图**（页面入口没跑到，"
                          "或 RequestImageFull 没被调）；pending>0 表示 worker/上传链卡住；"
                          "两者都正常却仍判红时，是**某一屏的纯色像素数为 0** —— "
                          "查看器画的是 `Art` 占位，或者打开的不是预期的那一页"
                          "（`viewer-full=` 那行里 pixels 是图片屏、sheet-pixels 是联系表屏）。"
                          "查链路，别改判据",
                          kit::RequestedImageFullCount(), kit::PendingImageFullCount());
    }
    // 逐镜成图的两处位点（出片页成片条 / 出图页结果页签）。与 `sheetImagesSettled` 同族：
    // 这两处早先画的是 `Art` 占位画，于是「占位画」与「真图还没落地」在像素上完全同形，
    // 而没有判据时「这一段一次都没请求过图」也不会体现在 overall 上（假绿）。
    if (!session.filmStripImages) {
        shine::log::Error("review: 出片页「成片条」里没有逐镜成片的真图（`film-strip=` 那行）—— "
                          "这一族的图来自 `generated_images` 的 `source_kind='shot'`"
                          "（与分镜页 `.tthumb` 同一个查询）。两格都数不到时先查：镜图有没有"
                          "真的请求过（`RequestImage` 每帧无条件调）、fixture 里第 1/2 镜的"
                          "`source_id` 有没有铺上、成片条有没有被面板挤到画不出来。"
                          "别改成数「有没有画东西」——占位画也能数出像素");
    }
    if (!session.imageFlowResultImage) {
        shine::log::Error("review: 出图页「结果」页签那张成图不是真图（`imageflow-result=` 那行）"
                          "—— 这一格早先画的是 `Art(..., seed=6)` 的程序化占位画，页签名就叫"
                          "「结果」，所以它是最容易被当成出图结果的一处。查 `BookState::shot()`"
                          "的 `imageRelPath`（`BookQuery.cpp` 那一句 `source_kind='shot'`）"
                          "有没有填上、以及结果页签那一格有没有调 `RequestImage`");
    }
    // 出图页「结果」页签那两行生成参数（`Prompt` / `生成参数`）的投影。
    // 与上面几条**不同**：这一条判的不是像素而是数据 —— 那两行同步画，没有异步窗口。
    // 它挡的是查询漏列 / 列序读串 / 行被判空跳过（界面少两行而整轮全绿）。
    if (!session.imageFlowParams) {
        shine::log::Error("review: 出图页「结果」页签的生成参数不是 fixture 契约里的那几个值"
                          "（`imageflow-params=` 那行，带实测值与期望值）—— "
                          "查 `BookQuery.cpp` 那条 `source_kind='shot'` 查询有没有把 "
                          "`prompt/model/steps/width/height` 一起 SELECT 出来、"
                          "`ColumnInt/ColumnText` 的下标有没有与 SELECT 的列序对上、"
                          "以及 `Page_ImageFlow.cpp` 里那两行 KV 的判空条件。"
                          "别改期望值：它们是 `_fixture_seed.sql` 的独立契约"
                          "（`ReviewFixture.h` 的 `kShot1*`）");
    }
    // 总控 KPI 卡右上角的柔光（`views.css:419-430`）有没有真的上屏、且**五张卡都在**。
    // 这一团是 `PushClipRect` 裁到 padding box 之后画的 ⇒ 裁剪写反/写小会让它**整团消失**，
    // 而卡、文字、静息截图全都正常 ⇒ 只有数像素能发现。
    if (!session.kpiGlow) {
        shine::log::Error("review: 总控 KPI 的柔光像素不在量级内（`kpi-glow=` 那行带实测值/"
                          "颜色/上下界）—— 要看数字落在哪一边："
                          "① pixels≈0：整团没画（裁剪反了 / 半径传错）；"
                          "② pixels≈3200：只接上了一张卡；"
                          "③ pixels 远大于上界（十万级）：**颜色退化成了一块面** —— "
                          "去查 `PageKpi.cpp` 的 `GlowColor()`：`WithAlpha` 是**调制**"
                          "（原 alpha × 参数），要覆盖得用 `WithAlphaSet`。"
                          "改产品，别改上下界");
    }
    // 总控「最近产物」那一行的出图产物（`generated_images`，非 shot 来源）。
    // 与上面两条同族：这一卡以前只列 T 链账本，而账本在执行体接入之前**恒为空** ⇒
    // 卡片永远只有空态文案，设计稿里「出图 · 1.8 MB」那一行从来没有过数据。
    if (!session.recentProductsImage) {
        shine::log::Error("review: 总控「最近产物」里没有出图产物的真图（`recent-products=` 那行）"
                          "—— 数据来自 `generated_images`（**不限 source_kind**，"
                          "`BookQuery.cpp` 里那句 `rel_path<>'' ORDER BY id DESC`），"
                          "文件大小在 worker 侧 stat。查：快照有没有落地"
                          "（这一卡的数据是异步回投的）、右栏有没有滚到顶部"
                          "（那几行在视口外等于没拍到）、以及那一行有没有 `RequestImage`");
    }
    // imageKeysDistinct 是上面那条判据的**成立前提**，所以它自己也得进 overall：
    // `ImageKey` 与 `ImageKeyFull` 撞成同一个 key 时，大图档登记不进登记表
    // （`try_emplace`），`RequestedImageFullCount()` 恒 0 ⇒ 上一条报红。
    // 而「撞不撞」取决于整条路径字符串的 FNV（路径里含 `SHINE_IMGUI_REVIEW` 的输出目录）
    // ⇒ 同一份代码换个 `-OutDir` 就从 PASS 变 FAIL。没有这条自检，那种红只会被当成
    // 「偶发」而放过去，根因永远不露头。
    if (!session.imageKeysDistinct) {
        shine::log::Error("review: `kit::Image` 的两档 key 号段**没有分裂**（{} / {} 条探针撞了）"
                          "—— `ImageKey` 与 `ImageKeyFull` 相撞时，页面里的缩略图先注册过"
                          "那条路径，大图档就登记不进去（`try_emplace` 不覆盖），于是"
                          "`viewer-full` 会以「大图一次都没请求过」的样子假红，而大图明明在屏上；"
                          "同时服务侧按 key 存 targetSize ⇒ 两个调用点每帧互相换档重解码。"
                          "修法是**先清号段位再置位**（见 `kit/Image.h` 约束 3），别改判据",
                          kit::ImageKeyCollisionCount(), kit::kImageKeySelfTestProbes);
    }
    const bool pass = result.failed == 0 && !result.scrollFailed && identicalPairs == 0 &&
                      drivenDuplicates == 0 && session.reportScanConverged &&
                      session.bookSnapshotConverged && session.assetSnapshotConverged &&
                      session.artifactsConverged && session.sheetImagesSettled &&
                      session.sheetImagesRecovered && session.viewerFullSettled &&
                      session.filmStripImages && session.imageFlowResultImage &&
                      session.imageFlowParams && session.kpiGlow && session.recentProductsImage &&
                      session.imageKeysDistinct && inverted == 0 &&
                      duplicateHits == 0 &&
                      session.hoverProbesPassed == session.hoverProbeTotal &&
                      session.shortcutsPassed == session.shortcutTotal &&
                      session.overlayClicksPassed == session.overlayClickTotal;
    WriteManifest(manifest, "# shots: " + std::to_string(result.captured) +
                                "  failed: " + std::to_string(result.failed) +
                                "  scroll-failed: " + (result.scrollFailed ? "1" : "0") +
                                "  identical-theme-pairs: " + std::to_string(identicalPairs) +
                                "  identical-driven-pairs: " + std::to_string(drivenDuplicates) +
                                "  inverted-rects: " + std::to_string(inverted) +
                                "  duplicate-hits: " + std::to_string(duplicateHits) +
                                "  hover-probes: " + std::to_string(session.hoverProbesPassed) + "/" + std::to_string(session.hoverProbeTotal) +
                                "  shortcuts: " + std::to_string(session.shortcutsPassed) + "/" + std::to_string(session.shortcutTotal) +
                                "  overlay-clicks: " + std::to_string(session.overlayClicksPassed) + "/" + std::to_string(session.overlayClickTotal) +
                                "  report-scan: " + (session.reportScanConverged ? "converged" : "TIMEOUT") +
                                "  book-snapshot: " +
                                (session.bookSnapshotConverged ? "converged" : "TIMEOUT") +
                                "  asset-snapshot: " +
                                (session.assetSnapshotConverged ? "converged" : "TIMEOUT") +
                                "  artifact-snapshot: " +
                                (session.artifactsConverged ? "converged" : "TIMEOUT") +
                                "  viewer-full: " +
                                (session.viewerFullSettled ? "settled" : "TIMEOUT"));
    if (inverted > 0) {
        WriteManifest(manifest, std::string("last-inverted-rect=") + kit::LastInvertedRect());
    }
    WriteManifest(manifest, std::string("overall=") + (pass ? "PASS" : "FAIL"));
    // 判据的结论**原样**交给调用方：退出码由它决定，不许调用方拿 captured / failed
    // 自己二次推算（那正是上面 Review.h 里记的假绿）。日志也带上 verdict，
    // 免得只看 "[imgui-review] shots=79 failed=0" 就以为这轮绿了。
    session.result.pass = pass;
    shine::log::Info("review done: {} saved, {} failed, overall={} -> {}", result.captured,
                     result.failed, (pass ? "PASS" : "FAIL"), session.outputDir.string());
}

} // namespace shine::imguiverify::detail
