#include "ui/imgui/verify/Review.h"

#include "ui/imgui/verify/Capture.h"
#include "ui/imgui/verify/ReviewScene.h"
#include "ui/imgui/verify/ReviewSession.h"
#include "ui/imgui/verify/ReviewVerdict.h"

#include <filesystem>

// 总回归入口。这个文件现在**只**做三件事：建 manifest、按既定顺序编排各族的
// 判据、返回判据的结论。实现在按职责分好的文件里：
//
//   场景与抓图   ReviewScene.cpp        —— 段一/段二覆盖扫描、第三~第八段受控状态
//   单张图       ReviewPixel.cpp        —— 显式设状态 → 推帧 → 读后备缓冲 → 存 PNG + 像素指纹
//   输入 fixture  ReviewFixture.cpp      —— 报告 JSON 与 output 产物（应用侧绝不造）
//   悬停探针     ReviewHoverProbe.cpp   —— 悬停前后像素必须不同（含注入自证 / 命中自检）
//   快捷键判据   ReviewShortcutProbe.cpp—— 注入按键 → 读产品状态 → 按语义复位
//   浮层点击判据 ReviewOverlayProbe.cpp —— 读产品自己的开态（像素上看不出来的一族）
//   热区扫描     ReviewHotspot.cpp      —— 把坐标量出来（诊断，不参与 pass/fail）
//   收尾与判定   ReviewVerdict.cpp      —— 判据二/三/四 → overall → manifest 收尾三行
//
// 共享的常量与一次会话的状态在 ReviewSession.h（namespace shine::imguiverify::detail）。
//
// ⚠️ **拆分是纯结构的**：判定阈值、manifest 字段、字段顺序、输出字节一个都没动。
//    `overall=PASS/FAIL` 是发布门禁，不是调试信息 —— 改这块之前先读
//    scripts/run_reviews.ps1:56/61/66（它按正则读那三行）。
namespace shine::imguiverify {

ReviewResult RunReview(imguiapp::Host& host, pages::Shell& shell,
                       const std::filesystem::path& outputDir) {
    detail::Session session{host, shell, outputDir};

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    // ⚠️ 文件名是**脚本契约**：scripts/run_reviews.ps1:56 读的是 `shots-manifest.txt`。
    //    早先这里叫 `manifest.txt`，于是脚本永远读到 NO-REPORT，verdict 恒空 ——
    //    整个取证流水线一直在假绿。改名后还要在末尾写 `overall=PASS|FAIL`
    //    （脚本 :61 靠这条正则判 verdict），缺了它同样判不出来。
    const std::filesystem::path manifest = outputDir / kManifestName;
    std::filesystem::remove(manifest, ec); // 重跑不追加，避免上一次的行混进来
    session.manifest = manifest;
    WriteManifest(manifest, "# shine imgui review — capture 1600x960, glReadPixels(GL_RGBA)");

    // 段一/段二：静息态覆盖扫描（当前主题 7 个工作区 + 6 工作区 × 5 套主题）。
    detail::RunCoverageSweep(session);
    // 第三段起：受控状态取证 + 悬停/快捷键/浮层三族探针（内部按段序调用）。
    detail::RunControlledStates(session);
    // 判据二/三/四 + overall 判定 + manifest 收尾三行。
    detail::FinishVerdict(session);

    return session.result;
}

} // namespace shine::imguiverify
