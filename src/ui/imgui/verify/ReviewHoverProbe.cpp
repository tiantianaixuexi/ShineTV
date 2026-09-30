#include "ui/imgui/verify/ReviewHoverProbe.h"

#include "core/Log.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/verify/Capture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace shine::imguiverify::detail {

// 悬停探针：把鼠标放到 (x, y) 拍一张，**再把鼠标放到窗外拍一张只取哈希**，
// 要求两者像素不同。对照那张不落盘（落盘会与已有静息态图逐字节撞上，把
// 「受控图不许重样」那条判据变成噪声）。
//
// 为什么必须判「不同」而不是拍完就算：悬停**拍不出来**的失败模式至少三种
// （控件没有 hover 样式 / 坐标算偏了点在空处 / 动画噪声恰好抵消），它们全都表现为
// 「图在、manifest 记 saved、overall=PASS」。哈希相同把三种一起变成 FAIL。
//
// ⚠️ 鼠标位置是**在帧内注入**的（`onFrame` 里、`shell.DrawFrame` 之前），
//    不是靠移真实光标。原因是取证跑在后台作业里：窗口不是前台窗口，
//    `ImGui_ImplWin32_UpdateMouseData` 的 `is_app_focused` 补位分支整个跳过，
//    WM_MOUSEMOVE 也不会送到 —— 实测 `io.MousePos` 始终停在初值 `-FLT_MAX`
//    （ImGui 的「无鼠标」哨兵，且从不被逐帧重置）。
//    `PumpFrames` 的顺序是 PumpMessages → NewFrame → onFrame → Render，
//    所以在 onFrame 里写 `io.MousePos` 与后端投递对 `DrawFrame` 是**等价**的。
//    本探针验的是**本工程的 hover 绘制**，不是 vendored ImGui 后端的消息接线
//    （那是第三方代码，真实交互下由 RunLoop 泵消息保证）。
bool ProbeHover(imguiapp::Host& host, pages::Shell& shell, const std::filesystem::path& dir,
                const HoverTarget& target, shine::theme::ThemeId theme,
                std::vector<std::uint64_t>& drivenHashes,
                std::vector<std::string>& drivenNames, HoverTally* tally, int* captured,
                int* failed) {
    const std::string& name = target.name;
    const float clientX = target.x;
    const float clientY = target.y;
    shell.SetTheme(theme);
    shell.SetWorkspace(target.workspace);
    // 前置动作：主题 / 工作区 / 视图 / 筛选 / 页签，一个都不靠上一条探针的残留状态。
    if (target.clearKindFilter) {
        shell.SetKindFilter(std::string());
    }
    if (target.assetsOverview) {
        shell.SetAssetsOverview(true);
    }
    if (target.dockTab >= 0) {
        shell.SetDockTab(target.dockTab);
    }
    if (target.imageFlowPanel >= 0) {
        shell.SetImageFlowPanel(target.imageFlowPanel);
    }
    if (target.novelMode >= 0) {
        shell.SetNovelMode(target.novelMode);
    }

    // kNoMouse 是 ImGui 自己用的「无鼠标」哨兵。
    constexpr float kNoMouse = -3.402823466e+38f;

    // ⚠️ 三拍，不是两拍，而且顺序有讲究。
    //
    // A、B 都是「无鼠标」帧，C 是「鼠标在目标上」。**A 必须等于 B** 才说明这一帧
    // 页面是静止的 —— `kit::TickAnimation(dt)` 每帧推进时间，资产页的 Progress 微光、
    // StatusDot 的呼吸、Tag 的 busyPulse 都会让像素逐帧变。如果不先证明
    // 静止就直接比 A 与 C，那「hover 有效果」和「页面正好在闪」根本分不开，
    // 探针就是在测动画噪声。
    //
    // ⚠️ 整段探针期间**钉住动画时钟**（`kit::PinAnimation`）。不钉的话，那两个
    //    永不静止的指示器会让 A 永远不等于 B，探针恒判 unstable —— 首轮实测
    //    8 个探针里 4 个就是这么红的（assets 2 个 + overview 2 个），而 novel 上的
    //    4 个因为恰好没有运行态指示器才过。钉住之后唯一变量就是鼠标位置。
    //    前面 52 张静息态截图**不钉**，它们该看到的就是带动画的真实界面。
    const float kPinnedTime = kit::Now();
    kit::PinAnimation(kPinnedTime);
    // 注入点换成宿主的 `SetFrameMouseOverride`（在 NewFrame **之前**）。原先写在
    // onFrame 里只改到 `IsMouseHoveringRect` 用的那份，`g.HoveredId` 早按真实光标
    // 算好了 ⇒ 每帧恒有 1 个 item 报 hovered，诊断信号整个是噪声。
    //
    // ⚠️ 这里同时记下**帧内 ImGui 实际看到**的 MousePos / DisplaySize，判据自证。
    //    「命中数 = 0」这一个信号至少对应三种完全不同的原因，光看它必然改错地方：
    //      · 覆盖没落地（`io.MousePos` 压根不是我们设的值）—— 宿主/后端层；
    //      · 视口塌成 0×0 —— 前面所有坐标一起失效；
    //      · 坐标落在热区外 —— 布局变了。
    //    r90 实测 11/11 全红、浮层探针同时报「位置注入没到位」，说明是前两种；
    //    而 r89 我把原因归到「CenterTextY 的日志刷屏冲掉了驱动协议」是**错的** ——
    //    删掉那条日志重跑，11/11 照旧红。没有这条自证就会一直猜错。
    float sawX = 0.0f;
    float sawY = 0.0f;
    float sawW = 0.0f;
    float sawH = 0.0f;
    bool sawAny = false;
    const auto frameWithMousePinned = [&shell, &host, &sawX, &sawY, &sawW, &sawH,
                                       &sawAny](float x, float y) {
        host.SetFrameMouseOverride(x, y);
        host.PumpFrames(kSettleFrames, [&shell, &sawX, &sawY, &sawW, &sawH, &sawAny](float dt) {
            shell.DrawFrame(dt);
            if (!sawAny) {
                sawX = ImGui::GetIO().MousePos.x;
                sawY = ImGui::GetIO().MousePos.y;
                sawW = ImGui::GetIO().DisplaySize.x;
                sawH = ImGui::GetIO().DisplaySize.y;
                sawAny = true;
            }
        });
        host.ClearFrameMouseOverride();
    };
    struct Unpin {
        ~Unpin() { kit::UnpinAnimation(); }
    } unpin;

    // 静息帧：每帧单独归零再数，取**最大**那一帧。早先只在悬停帧前归零，
    // 于是 `restHovered` 里混着上一条探针的累计值 —— 第一条探针读到 0、后面
    // 全读到 2，看着像「哨兵失效」，其实是计数器没归零。
    int restHovered = 0;
    for (int i = 0; i < kSettleFrames; ++i) {
        kit::ResetHoveredItemCount();
        frameWithMousePinned(kNoMouse, kNoMouse);
        restHovered = std::max(restHovered, kit::HoveredItemCount());
    }
    const std::vector<std::uint8_t> restA = host.CaptureBackBuffer();
    frameWithMousePinned(kNoMouse, kNoMouse);
    const std::vector<std::uint8_t> restB = host.CaptureBackBuffer();
    const std::uint64_t restHash = HashPixels(restA);

    // 悬停帧：同样每帧单独归零，取最大那一帧。
    int hoveredPerFrame = 0;
    sawAny = false;  // 只记**悬停**那一拍的读数，静息拍的 -FLT_MAX 没有诊断价值
    for (int i = 0; i < kSettleFrames; ++i) {
        kit::ResetHoveredItemCount();
        frameWithMousePinned(clientX, clientY);
        hoveredPerFrame = std::max(hoveredPerFrame, kit::HoveredItemCount());
    }
    const char* hoveredId = kit::LastHoveredItem();
    const std::vector<std::uint8_t> pixels = host.CaptureBackBuffer();
    const std::uint64_t hash = HashPixels(pixels);
    RECT client{};
    GetClientRect(host.window(), &client);
    const auto width = static_cast<std::uint32_t>(client.right);
    const auto height = static_cast<std::uint32_t>(client.bottom);
    if (!GrabAndSave(pixels, width, height, dir, name)) {
        // 计数必须在这里自己加，不能靠调用点用返回值 —— 早先返回值在调用点被丢掉，
        // 于是悬停那 8 张图「存了、manifest 记 saved、failed 也不加」：
        // `# shots:` 少报 8 张，编码失败则完全不可见。取证的数字要能对上盘上的文件数。
        ++*failed;
        return false;
    }
    ++*captured;
    const bool stable = restHash == HashPixels(restB);
    const bool changed = restHash != hash;
    // 判据自证：ImGui 帧内看到的坐标是不是我们注入的那个。
    const bool landed = sawAny && std::abs(sawX - clientX) < 0.5f && std::abs(sawY - clientY) < 0.5f;
    shine::log::Info(
        "review: probe {} at ({:.0f},{:.0f}) rest-stable={} rest-hovered={} hit={} last={} "
        "注入: landed={} saw=({:.0f},{:.0f}) 视口={:.0f}x{:.0f}",
        name, clientX, clientY, stable ? 1 : 0, restHovered, hoveredPerFrame, hoveredId,
        landed ? 1 : 0, sawX, sawY, sawW, sawH);
    if (!landed) {
        // 注入没到位 ⇒ 后面「命中数 / 像素变化」全部不可用，**不要再往下解读**。
        // 这一条排在最前面就是为了掐死「看到 hit=0 就去改产品的 hover 分支」这条路。
        shine::log::Error(
            "review: {} 鼠标覆盖**没到位**：要 ({:.1f},{:.1f})，帧内 ImGui 看到 ({:.1f},{:.1f})，"
            "视口 {:.0f}x{:.0f}。这是宿主/后端层的问题，与布局和 hover 分支都无关",
            name, clientX, clientY, sawX, sawY, sawW, sawH);
        ++tally->unstable;
    } else if (restHovered > 0) {
        // 静息帧竟然命中了控件 ⇒ -FLT_MAX 哨兵没生效，后面所有比较都不可信。
        shine::log::Error("review: {} 静息帧（鼠标用 -FLT_MAX 哨兵）仍命中了 {} 个控件，"
                          "哨兵失效，本探针与后面全部悬停结论都不可信",
                          name, restHovered);
        ++tally->unstable;
    } else if (!stable) {
        // 页面在动，这一拍测不出 hover 的因果。**记成不稳定并让整轮红掉** ——
        // 放过去就等于「这个控件的 hover 链路没验过」却报 PASS，比误报红更坏：
        // 门禁一旦学会放过自己的不确定，后面所有 PASS 都不可信。
        // 修法通常是换坐标、或者给这一帧把动画时间钉住，不是改判据。
        shine::log::Error("review: {} 静息两帧就不一致（页面在动），本探针结论不可用", name);
        ++tally->unstable;
    } else if (hoveredPerFrame == 0) {
        // ⚠️ 像素没变有**两种**完全不同的原因，只看像素差分不开：
        //   · 坐标写偏了，鼠标没落进任何热区 —— 这是**探针**的问题，要改坐标；
        //   · 命中了，但这个控件的 hover 分支什么都不画 —— 这是**产品**的缺陷。
        // 少一次命中计数时，我一度把后者当成了前者去改产品。命中自检就是为了
        // 在两者之间给出确定的答案：现在直接报「探针坐标点空了」。
        shine::log::Error("review: {} 注入到位({:.0f},{:.0f})、页面静止，但鼠标**一个控件都没命中**"
                          " —— 探针坐标落在热区之外（布局变了），不是产品的 hover 缺陷。改坐标，别改产品",
                          name, sawX, sawY);
        ++tally->unstable;
    } else if (!changed) {
        // 命中了（hoveredPerFrame > 0，hover 分支确实被执行了）却一个像素都没变 ——
        // 这才是真的「hover 链路断在绘制侧」。
        shine::log::Error(
            "review: {} 命中了 {} 个控件（最后一个 {}）、页面也静止，悬停前后像素却**完全相同** "
            "—— 命中测试通了但 hover 分支什么都不画",
            name, hoveredPerFrame, hoveredId);
        ++tally->broken;
    } else {
        ++tally->ok;
    }
    drivenHashes.push_back(hash);
    drivenNames.push_back(name);
    return true;
}

// ---- 第六段的悬停部分：目标表 + 整轮跑法 + 报告行 ----
//
// 前 52 张全是**静息态**。设计稿里几乎每个控件都定义了 `:hover`，而悬停恰恰是
// 最容易整条链路断掉的状态：win32 后端接没接上、HitTest 的 item 有没有占位、
// 控件有没有 hover 样式 —— 三者任何一个断了，界面都只是「鼠标划过去没反应」，
// 静息态截图**完全看不出来**。所以单独验，并且把「悬停前后必须像素不同」
// 做成硬判据。
// 坐标来自 design-spec §4 的定宽栅格（Rail 56 / SidePanel 240 / TopBar 46），
// 逐个对着上一段的截图核过。⚠️ 布局一改这里就会偏 —— 偏了的症状不是报错，
//    而是判据报「悬停前后相同」，属于会自己暴露的那种。
// （第六段的另一半 toast 在 ReviewScene.cpp，与这里隔着浮层三族判据。）
void RunHoverProbes(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const std::filesystem::path& outputDir = session.outputDir;
    const std::filesystem::path& manifest = session.manifest;
    const shine::theme::ThemeId base = session.base;

    const HoverTarget targets[] = {
        // 侧栏 2×2 里的「资产」
        {"hover-jump-btn", kWsNovel, 123.0f, 135.0f},
        // 左栏第 4 个开关
        {"hover-rail-toggle", kWsNovel, 28.0f, 275.0f},
        // kind 树里的实体叶子（侧栏，与主区视图无关）
        {"hover-tree-node", kWsAssets, 170.0f, 210.0f},
        // 总览网格的一张卡 —— 必须显式切回总览并清空 kind 筛选，
        // 否则上一条探针留在详情态/筛选态，这张坐标上压根没有卡片。
        {"hover-asset-card", kWsAssets, 420.0f, 200.0f, true, true},
        // 底栏「校验报告」的一行
        {"hover-dock-row", kWsOverview, 420.0f, 790.0f},
        // 检查器「属性」段头
        {"hover-inspector-head", kWsNovel, 1400.0f, 69.0f},
        // 判别探针：同样这两个控件换到另一个工作区再试一次。
        // 首轮实测 novel 上的 3 个全灭、assets/overview 上的 3 个全过，所以要分清
        // 是「小说工作区整体没有 hover」还是「这几个控件本身没 hover 样式」。
        {"hover-rail-toggle-overview", kWsOverview, 28.0f, 275.0f},
        {"hover-dock-row-novel", kWsNovel, 420.0f, 790.0f},
        // 第七段新覆盖的视图。坐标逐个对着 out/review-* 里那些新图量的；
        // 命中数与命中的 widget id 都会进日志，坐标偏了会直接报「一个控件都没命中」
        // 而不是含糊的「悬停前后相同」。
        // 底栏产物页第一行（fixture 造了 6 个产物，行有 hover 底色）
        {"hover-artifact-row", kWsOverview, 200.0f, 789.0f, false, false, 2},
        // 出图页右侧面板的页签。⚠️ 打的是**非选中**项「图评审」：设计稿
        // `.seg > button:hover` 只改文字色，`.on` 本来就是 text-primary，
        // 所以**选中项按设计就没有 hover 反馈**。拿选中项当探针等于测一个设计稿
        // 不承诺的东西，判据会一直红。面板本身停在 1（批量出图）以保证这一页有内容。
        {"hover-imageflow-tab", kWsImageFlow, 1105.0f, 165.0f, false, false, 0, 1},
        // 小说页 8 个模式标签里的「设定」（以前只有 Clicked，标签不可反馈）
        {"hover-novel-mode-tab", kWsNovel, 380.0f, 100.0f, false, false, 0, 0, 1},
    };
    HoverTally tally;
    for (const HoverTarget& target : targets) {
        (void)ProbeHover(host, shell, outputDir, target, base, session.drivenHashes,
                         session.drivenNames, &tally, &session.result.captured,
                         &session.result.failed);
    }
    session.hoverProbesPassed = tally.ok;
    const int hoverTotal = static_cast<int>(std::size(targets));
    WriteManifest(manifest, std::string("hover-probes=") + std::to_string(tally.ok) + "/" +
                                std::to_string(hoverTotal) + " changed-vs-rest" +
                                (tally.broken > 0 ? "  broken=" + std::to_string(tally.broken)
                                                  : std::string()) +
                                (tally.unstable > 0
                                     ? "  unstable=" + std::to_string(tally.unstable)
                                     : std::string()));
    session.hoverProbeTotal = hoverTotal;
}

} // namespace shine::imguiverify::detail
