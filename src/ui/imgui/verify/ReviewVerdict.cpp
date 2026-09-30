#include "ui/imgui/verify/ReviewVerdict.h"

#include "core/Log.h"
#include "ui/imgui/kit/Draw.h"
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
    const bool pass = result.failed == 0 && !result.scrollFailed && identicalPairs == 0 &&
                      drivenDuplicates == 0 && session.reportScanConverged &&
                      session.bookSnapshotConverged && session.assetSnapshotConverged &&
                      session.artifactsConverged && inverted == 0 && duplicateHits == 0 &&
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
                                (session.artifactsConverged ? "converged" : "TIMEOUT"));
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
