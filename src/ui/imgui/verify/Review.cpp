#include "ui/imgui/verify/Review.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/Hub.h"
#include "ui/imgui/pages/Shell_Layout.h"
#include "ui/imgui/verify/ReviewFixture.h"
#include "ui/imgui/verify/ReviewHotspot.h"

#include "ui/imgui/kit/Image.h"        // ImageKeyCollisionCount：两档 key 的号段自检
#include "ui/imgui/kit/Widget_Core.h"  // ResetInvertedRectCount / ResetDuplicateHitCount
#include "ui/imgui/verify/Capture.h"
#include "ui/imgui/verify/ReviewScene.h"
#include "ui/imgui/verify/ReviewSession.h"
#include "ui/imgui/verify/ReviewVerdict.h"
#include "util/Encoding.h"

#include <imgui_internal.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <thread>

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

namespace {

void RunUiSmoke(detail::Session& session) {
    auto& host = session.host;
    auto& shell = session.shell;
    const auto frame = [&](float dt) { shell.DrawFrame(dt); };
    const auto record = [&](std::string_view label, bool ok) {
        WriteManifest(session.manifest, std::string(label) + (ok ? "=ok" : "=FAIL"));
        session.result.pass &= ok;
    };
    session.result.pass = true;
    const auto fixture = session.outputDir / "_review_reports_project";
    record("fixture", detail::SeedReportFixture(fixture));
    session.GrabHubDriven("project-center", session.base);
    if (host.UiScale() == 1.0f) {
        auto& wizard = pages::hub::S();
        wizard.wizard = true;
        wizard.step = 0;
        wizard.name.clear();
        wizard.dir = util::PathToUtf8(session.outputDir);
        wizard.idea = "取证样例创意";
        const auto clickWizard = [&](const char* id) {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const float right = (display.x + std::min(824.0f, display.x - 32.0f)) * 0.5f;
            const float bottom = (display.y + std::min(572.0f, display.y - 24.0f)) * 0.5f;
            ImVec2 target;
            if (!detail::FindHotspot(host, shell, id, target, 8.0f, right - 158.0f,
                                     bottom - 44.0f, right - 22.0f, bottom - 12.0f)) {
                return false;
            }
            host.SetFrameMouseOverride(target.x, target.y);
            host.SetFrameMouseButtonOverride(true);
            host.PumpFrames(1, frame);
            host.SetFrameMouseButtonOverride(false);
            host.PumpFrames(1, frame);
            host.ClearFrameMouseButtonOverride();
            host.ClearFrameMouseOverride();
            host.PumpFrames(1, frame);
            return true;
        };
        session.GrabHubDriven("create-01-template", session.base);
        record("create-template", clickWizard("hub-wiz-next") && wizard.step == 1);
        record("create-empty-name", clickWizard("hub-wiz-next") && wizard.step == 1 &&
                                    wizard.statusError);
        wizard.name = "UI-Review-" + std::to_string(GetTickCount64());
        const auto created = session.outputDir / wizard.name;
        session.GrabHubDriven("create-02-name", session.base);
        record("create-name", clickWizard("hub-wiz-next") && wizard.step == 2);
        session.GrabHubDriven("create-03-idea", session.base);
        record("create-idea", clickWizard("hub-wiz-next") && wizard.step == 3);
        session.GrabHubDriven("create-04-confirm", session.base);
        record("create-project", clickWizard("hub-wiz-create") &&
                                 std::filesystem::exists(created / "project.json") &&
                                 shell.projectName() == created.filename().string());
        wizard.wizard = false;
    }
    shell.SetHubOpen(false);
    shell.SetProjectRoot(fixture, "取证样例工程");
    shell.SetWorkspace(detail::kWsOverview);
    for (int i = 0; i < 180 && !pages::BookSide().bound; ++i) {
        host.PumpFrames(1, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // ImGui 在 NewFrame 用上一帧窗口树判定命中。项目中心退出后先让活动栏提交一帧。
    host.PumpFrames(2, frame);
    record("fixture-bound", pages::BookSide().bound);

    RECT client{};
    GetClientRect(host.window(), &client);
    const float scale = host.UiScale();
    const ImGuiIO& io = ImGui::GetIO();
    record("logical-viewport", std::abs(io.DisplaySize.x * scale - client.right) < 1.0f &&
                                std::abs(io.DisplaySize.y * scale - client.bottom) < 1.0f &&
                                io.DisplayFramebufferScale.x == scale);
    WriteManifest(session.manifest, "scale=" + std::to_string(scale) +
                    " pixels=" + std::to_string(client.right) + "x" + std::to_string(client.bottom));

    // 走真正的 Win32 消息入口，避免取证鼠标覆盖绕开 DPI 换算。
    const float x = 28.0f;
    const float y = pages::kTopBarHeight + 75.0f;
    const auto pt = MAKELPARAM(static_cast<int>(std::lround(x * scale)),
                               static_cast<int>(std::lround(y * scale)));
    std::string pointerTrace;
    bool logicalPress = false;
    bool logicalRelease = false;
    const auto observePointer = [&](float dt) {
        // 真实鼠标可在合成按键释放后的下一帧回填，坐标只和对应的按键沿配对。
        const bool expected = std::abs(io.MousePos.x - x) <= 1.0f &&
                              std::abs(io.MousePos.y - y) <= 1.0f;
        logicalPress |= expected && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        logicalRelease |= expected && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        frame(dt);
        const auto* ctx = ImGui::GetCurrentContext();
        pointerTrace += " [" + std::to_string(io.MousePos.x) + "," + std::to_string(io.MousePos.y) +
                        " down=" + std::to_string(io.MouseDown[0]) +
                        " window=" + (ctx->HoveredWindow ? ctx->HoveredWindow->Name : "none") + "]";
    };
    SendMessageW(host.window(), WM_LBUTTONDOWN, MK_LBUTTON, pt);
    host.PumpFrames(2, observePointer);
    const std::string pressedOn = kit::LastHoveredItem();
    SendMessageW(host.window(), WM_LBUTTONUP, 0, pt);
    host.PumpFrames(2, observePointer);
    record("logical-mouse", logicalPress && logicalRelease);
    record("rail-click", shell.workspace() == detail::kWsNovel);
    WriteManifest(session.manifest, "rail-hit=" + pressedOn + pointerTrace);

    shell.SetWorkspace(detail::kWsOverview);
    host.PumpFrames(2, frame);
    const auto activeWorkspace = []() -> ImGuiWindow* {
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
            if (window->Active && std::string_view(window->Name).find("/workspace-scroll_") !=
                                      std::string_view::npos) {
                return window;
            }
        }
        return nullptr;
    };
    POINT wheelPoint{static_cast<LONG>(std::lround(414.0f * scale)),
                     static_cast<LONG>(std::lround(212.0f * scale))};
    ClientToScreen(host.window(), &wheelPoint);
    SendMessageW(host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA),
                 MAKELPARAM(wheelPoint.x, wheelPoint.y));
    host.PumpFrames(3, frame);
    const ImGuiWindow* wheelWindow = activeWorkspace();
    record("workspace-wheel", wheelWindow != nullptr && wheelWindow->Scroll.y > 0.0f);
    kit::ScrollRegion::RequestScrollBottom("workspace-scroll");
    host.PumpFrames(3, frame);
    const auto scroll = kit::ScrollRegion::LastApplied();
    record("scroll-bottom", scroll.id == "workspace-scroll" && scroll.ok);
    kit::ScrollRegion::RequestScrollTop("workspace-scroll");
    host.PumpFrames(3, frame);
    for (int i = 0; i < pages::kWorkspaceCount; ++i) {
        (void)session.Grab(std::string("workspace-") + pages::WorkspaceIcon(i), i, session.base);
    }
    for (const int workspace : {detail::kWsImageFlow, detail::kWsVideoFlow}) {
        shell.SetWorkspace(workspace);
        host.PumpFrames(2, frame);
        const int requestFrame = ImGui::GetFrameCount();
        kit::ScrollRegion::RequestScrollBottom("workspace-scroll");
        host.PumpFrames(3, frame);
        const auto panel = kit::ScrollRegion::LastApplied();
        const bool image = workspace == detail::kWsImageFlow;
        const std::string label = image ? "image-panel" : "video-panel";
        // 并排布局可能刚好装下。堆叠布局仍要求 ImGui 有足够的行程且实际滚到底。
        const bool fits = panel.contentHeight > 0.0f && panel.contentHeight <= panel.viewHeight &&
                          panel.maxScrollY == 0.0f && panel.imGuiMaxScrollY == 0.0f &&
                          panel.scrollY == 0.0f;
        const bool reachedBottom = panel.ok && panel.maxScrollY > 0.0f &&
                                   std::abs(panel.scrollY - panel.maxScrollY) <= 1.0f &&
                                   panel.imGuiMaxScrollY >= panel.maxScrollY - 1.0f;
        record(label + "-scroll", panel.id == "workspace-scroll" && panel.frame > requestFrame &&
                                     (fits || reachedBottom));
        WriteManifest(session.manifest, label + "-metrics id=" + panel.id +
                          " content=" + std::to_string(panel.contentHeight) +
                          " view=" + std::to_string(panel.viewHeight) +
                          " expected-max=" + std::to_string(panel.maxScrollY) +
                          " imgui-max=" + std::to_string(panel.imGuiMaxScrollY) +
                          " position=" + std::to_string(panel.scrollY));
        // 到底判据验证行程。取证图回到任务面板开头，同时保留标题和操作页签。
        if (ImGuiWindow* window = activeWorkspace()) {
            ImGui::SetScrollY(window, panel.viewHeight + (image ? 8.0f : 116.0f));
        }
        (void)session.Grab(label, workspace, session.base);
    }
    record("captures", session.result.failed == 0);
    record("valid-rectangles", kit::InvertedRectCount() == 0);
    const int duplicateHits = kit::DuplicateHitCount();
    WriteManifest(session.manifest, "duplicate-hits=" + std::to_string(duplicateHits));
    record("unique-hit-targets", duplicateHits == 0);
    WriteManifest(session.manifest, session.result.pass ? "overall=PASS" : "overall=FAIL");
}

} // namespace

ReviewResult RunReview(imguiapp::Host& host, pages::Shell& shell,
                       const std::filesystem::path& outputDir) {
    // ---- 两个自检计数在这里归零 ----
    //
    // ⚠️ 这两个 `Reset*` 长期**零调用点**（见 PROGRESS「真缺陷」）。今天还不是缺陷：
    //    `RunReview` 排在 `RunLoop` 之前，所有泵帧都发生在取证内部，计数天然只含取证帧。
    //    但只要将来**任何**代码在取证之前泵过帧（启动预热、主题探测、窗口尺寸自适应、
    //    上一次会话的残留帧……），那些帧里的反向矩形 / 重叠热区会**累加**进取证的计数：
    //    判据报红，而根因在一次早就结束的交互会话里 —— 这是最难查的一类假红
    //    （反过来，若有人为了让它变绿把判据删了，就是假绿，更糟）。
    //    归零的代价是 0 行风险，所以放在最前：**判据只对它自己跑出来的东西负责。**
    //
    //    两个函数都幂等，重跑多轮不会把上一轮的违规带进来。
    kit::ResetInvertedRectCount();
    kit::ResetDuplicateHitCount();

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
    WriteManifest(manifest, "# shine imgui review — glReadPixels(GL_RGBA)");
    if (std::getenv("SHINE_IMGUI_REVIEW_SMOKE") != nullptr) {
        RunUiSmoke(session);
        return session.result;
    }

    // ---- 图片两档 key 的号段自检（与上面两个 Reset* 同一族）----
    //
    // 「判据只对它自己跑出来的东西负责」：这一条验的是 `viewer-full` 判据的**成立前提**
    // —— `kit::ImageKey` 与 `kit::ImageKeyFull` 是不是真的分裂。两档 key 撞上时，
    // 大图档登记不进登记表（`try_emplace`），`RequestedImageFullCount()` 恒 0，
    // 而界面上那张大图**明明画出来了** ⇒ 判据以「大图链断了」的样子报红。
    // 见 `kit/Image.h` 约束 3 与 `Image.cpp` 顶部：撞不撞取决于**整条路径字符串**的
    // FNV，而路径里含输出目录 ⇒ 换个 `-OutDir` 结果就翻。（实测 `shots-base` 撞、
    // `shots-base2` 不撞，两轮 viewer 截图的像素数一字不差。）
    // 所以这条自检用**就地生成的探针字符串**，天然免疫输出目录。
    const int keyCollisions = kit::ImageKeyCollisionCount();
    session.imageKeysDistinct = (keyCollisions == 0);
    WriteManifest(manifest, std::string("image-keys=") +
                                (session.imageKeysDistinct ? "ok" : "COLLIDE") + " collisions=" +
                                std::to_string(keyCollisions) + " probes=" +
                                std::to_string(kit::kImageKeySelfTestProbes));

    // 段一/段二：静息态覆盖扫描（当前主题 7 个工作区 + 6 工作区 × 5 套主题）。
    detail::RunCoverageSweep(session);
    // 第三段起：受控状态取证 + 悬停/快捷键/浮层三族探针（内部按段序调用）。
    detail::RunControlledStates(session);
    // 判据二/三/四 + overall 判定 + manifest 收尾三行。
    detail::FinishVerdict(session);

    return session.result;
}

} // namespace shine::imguiverify
