#include "ui/imgui/verify/Review.h"

#include "core/Log.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/verify/Capture.h"
#include "util/File.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace shine::imguiverify {
namespace {

using imguiapp::Host;
using pages::Shell;

// 每张图固定推进的帧数：ImGui 自绘控件是逐帧提交的，字体图集也要一帧才烘焙上传。
// 3 帧足以让「本帧新建的图元」进 draw list，再多只是刷同样的内容。
constexpr int kSettleFrames = 3;

    // 像素内容的 FNV-1a 64。用途不是密码学，是「同一工作区在 5 套主题下的图必须互不相同」
    // 这条判据 —— 主题没加载上时 5 张会**逐字节相同**，而只判「PNG 写出来了」的旧门禁
    // 会照样报 overall=PASS（实测：exe 放到没有 themes/ 的目录里跑，37 张只有 13 张唯一，
    // manifest 依旧 PASS）。这条判据把那种假绿变成 FAIL。
std::uint64_t HashPixels(const std::vector<std::uint8_t>& pixels) {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint8_t b : pixels) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
// 底栏页签名 → 文件名片段。与 `Shell.cpp` 的 `tabs[]` 同序（任务队列/日志/产物/校验报告）。
// 写在取证里而不是去读外壳那份数组：那张表是私有局部量，跨不过 TU，抄一份反而
// 多一个可能失同步的副本 —— 所以用**下标查表**而不是再维护一份平行定义。
const char* DockTabSlug(int tab) {
    static const char* kSlugs[] = {"queue", "logs", "artifacts", "reports"};
    return (tab >= 0 && tab < static_cast<int>(std::size(kSlugs))) ? kSlugs[tab] : "tab";
}

// 悬停探针要打的目标 + **前置动作**。
//
// ⚠️ 前置动作必须显式写全。踩过的坑：资产网格那张卡探针一开始没切视图，
//    上一条 `assets-leaf-selected` 已经把资产页留在**详情**态 —— 那张坐标上根本没卡片，
//    探针报「hover 前后相同」，看上去像卡片 hover 链路断了。症状与真缺陷一模一样，
//    判据本身没错，错在没把状态摆成它承诺的样子。
struct HoverTarget {
    const char* name;
    int workspace;
    float x;
    float y;
    // 资产页要不要先切回总览网格（详情态下主区没有卡片）。
    bool assetsOverview = false;
    // 资产 kind 筛选要不要先清空（筛掉了目标所在的那一类就只剩空态）。
    bool clearKindFilter = false;
    // 底栏页签（0 队列 / 1 日志 / 2 产物 / 3 校验报告）。-1 = 不动。
    int dockTab = -1;
    // 出图页右侧面板页签；-1 = 不动。
    int imageFlowPanel = -1;
    // 小说页模式标签；-1 = 不动。
    int novelMode = -1;
};

// 热区扫描：沿一片网格把鼠标挨个挪过去，收集出现过的**全部**控件 id。
// 用途有两个：
//   1. 探针报「点空了」时把坐标**量**出来，而不是猜；
//   2. 判断「某个浮层里的控件到底能不能收到鼠标」—— 见下面项目中心那条判据。
//
// 为什么需要它：`hit=0` 这一个信号至少对应三种原因（注入没到位 / 视口塌了 /
// 坐标落在热区外），而第三种是**布局一改就全失效**的：坐标是照着某个
// 旧版截图量的。布局改完之后探针报红，日志只能告诉你「点空了」，不告诉你
// 「现在这个控件在哪」—— 于是要么去改本来正确的产品代码，要么凭感觉挪
// 两个数字再跑一轮 300 秒。扫描一次把这个信息补齐。
//
// ⚠️ 返回的是**集合**：全屏遮罩若注册成热区，会在每一格都命中并盖住所有真
//    控件 —— 只看「某一点命中了什么」看不出来，看「整片里出现过哪些 id」
//    才看得出来（那时集合里只有遮罩，没有被盖住的那些）。
std::set<std::string> ScanHotspots(Host& host, Shell& shell, float x0, float y0, float x1,
                                    float y1, float step, bool verbose = true) {
    struct Unpin {
        ~Unpin() { kit::UnpinAnimation(); }
    } unpin;
    kit::PinAnimation(kit::Now());
    std::set<std::string> found;
    for (float y = y0; y <= y1; y += step) {
        for (float x = x0; x <= x1; x += step) {
            host.SetFrameMouseOverride(x, y);
            kit::ResetHoveredItemCount();
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            host.ClearFrameMouseOverride();
            if (kit::HoveredItemCount() > 0) {
                const char* id = kit::LastHoveredItem();
                found.insert(id != nullptr ? id : "(null)");
                if (verbose) {
                    shine::log::Info("scan: ({:.0f},{:.0f}) -> {} x{}", x, y, id,
                                     kit::HoveredItemCount());
                }
            }
        }
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
    }
    return found;
}

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
bool ProbeHover(Host& host, Shell& shell, const std::filesystem::path& dir,
                const HoverTarget& target, shine::theme::ThemeId theme,
                std::vector<std::uint64_t>& drivenHashes,
                std::vector<std::string>& drivenNames, int* hoverOk, int* hoverUnstable,
                int* hoverBroken, int* captured, int* failed) {
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
        ++*hoverUnstable;
    } else if (restHovered > 0) {
        // 静息帧竟然命中了控件 ⇒ -FLT_MAX 哨兵没生效，后面所有比较都不可信。
        shine::log::Error("review: {} 静息帧（鼠标用 -FLT_MAX 哨兵）仍命中了 {} 个控件，"
                          "哨兵失效，本探针与后面全部悬停结论都不可信",
                          name, restHovered);
        ++*hoverUnstable;
    } else if (!stable) {
        // 页面在动，这一拍测不出 hover 的因果。**记成不稳定并让整轮红掉** ——
        // 放过去就等于「这个控件的 hover 链路没验过」却报 PASS，比误报红更坏：
        // 门禁一旦学会放过自己的不确定，后面所有 PASS 都不可信。
        // 修法通常是换坐标、或者给这一帧把动画时间钉住，不是改判据。
        shine::log::Error("review: {} 静息两帧就不一致（页面在动），本探针结论不可用", name);
        ++*hoverUnstable;
    } else if (hoveredPerFrame == 0) {
        // ⚠️ 像素没变有**两种**完全不同的原因，只看像素差分不开：
        //   · 坐标写偏了，鼠标没落进任何热区 —— 这是**探针**的问题，要改坐标；
        //   · 命中了，但这个控件的 hover 分支什么都不画 —— 这是**产品**的缺陷。
        // 少一次命中计数时，我一度把后者当成了前者去改产品。命中自检就是为了
        // 在两者之间给出确定的答案：现在直接报「探针坐标点空了」。
        shine::log::Error("review: {} 注入到位({:.0f},{:.0f})、页面静止，但鼠标**一个控件都没命中**"
                          " —— 探针坐标落在热区之外（布局变了），不是产品的 hover 缺陷。改坐标，别改产品",
                          name, sawX, sawY);
        ++*hoverUnstable;
    } else if (!changed) {
        // 命中了（hoveredPerFrame > 0，hover 分支确实被执行了）却一个像素都没变 ——
        // 这才是真的「hover 链路断在绘制侧」。
        shine::log::Error(
            "review: {} 命中了 {} 个控件（最后一个 {}）、页面也静止，悬停前后像素却**完全相同** "
            "—— 命中测试通了但 hover 分支什么都不画",
            name, hoveredPerFrame, hoveredId);
        ++*hoverBroken;
    } else {
        ++*hoverOk;
    }
    drivenHashes.push_back(hash);
    drivenNames.push_back(name);
    return true;
}

// 一张图的完整动作：**显式**设主题 + 显式设工作区 → 推进 N 帧 → 抓后备缓冲 → 存 PNG。
// 纪律：前置动作写全，不依赖"默认状态恰好是我要的"（phases.md P6 第 3 条）。
// 返回 false = 没抓到有效像素或编码失败，计入 manifest 的 FAILED。
// 第三个出参回传像素内容哈希，供「主题真的切了没有」判据用。
bool Shoot(Host& host, Shell& shell, const std::filesystem::path& dir, std::string_view name,
           int workspace, shine::theme::ThemeId theme, std::uint64_t* hashOut) {
    shell.SetTheme(theme);
    shell.SetWorkspace(workspace);
    host.PumpFrames(kSettleFrames, [&shell](float dt) { shell.DrawFrame(dt); });

    // ⚠️ 抓图只能发生在 PumpFrames 之后、SwapBuffers 之前。
    // glReadPixels 默认读 GL_BACK，SwapBuffers 之后后备缓冲内容未定义。
    RECT client{};
    GetClientRect(host.window(), &client);
    const auto width = static_cast<std::uint32_t>(client.right);
    const auto height = static_cast<std::uint32_t>(client.bottom);
    const std::vector<std::uint8_t> pixels = host.CaptureBackBuffer();

    if (GrabAndSave(pixels, width, height, dir, name)) {
        if (hashOut != nullptr) {
            *hashOut = HashPixels(pixels);
        }
        shine::log::Info("review shot {} saved ({}x{}, ws={}, theme={})", name, width, height,
                         workspace, shine::theme::ThemeIdKey(theme));
        return true;
    }
    shine::log::Error("review shot {} FAILED (ws={}, theme={})", name, workspace,
                      shine::theme::ThemeIdKey(theme));
    return false;
}

// 主题轮：画廊 + 资产两页最密（覆盖全部控件与 7 色调），足以暴露主题映射漏项。
// 6 工作区全跑 × 5 主题 = 30 张，判读成本远高于收益；这里用「全页 × 当前主题」
// 加「密页 × 5 主题」两段覆盖，30 张的覆盖面一分不少。
// 主题轮覆盖哪些页。P6.3 要求「6 工作区 × 5 主题 = 30 组」（phases.md:356），
// 早先只挑了画廊 + 资产两页，于是总控/小说/分镜/出图/出片在 4 套非默认主题下
// **从未被截过** —— 主题映射在这些页上漏项不会被任何证据图发现。
// 现在全量覆盖，判读成本靠文件名分组与 md5 排重来控制。
constexpr int kThemedWorkspaces[] = {
    static_cast<int>(pages::Workspace::Overview),
    static_cast<int>(pages::Workspace::Novel),
    static_cast<int>(pages::Workspace::Assets),
    static_cast<int>(pages::Workspace::Storyboard),
    static_cast<int>(pages::Workspace::ImageFlow),
    static_cast<int>(pages::Workspace::VideoFlow),
};

// 等报告扫描回投的帧数上限。PumpFrames 是紧循环、不 sleep，光推帧未必给 worker
// 调度机会，所以调用方每帧还要真的让出一会儿。
constexpr int kReportWaitFrameCap = 400;

// 造一个**格式与 NovelContinuity.cpp:467 完全一致**的报告集合，只为取证流水线有东西可拍。
//
// ⚠️ 这是**取证流水线的输入 fixture**，写在输出目录下的临时工程里，由本文件自己造。
//    应用侧绝不造报告 —— DrawDockReports 只读 business 层落盘的文件。
//    三章各给一种结论（全过 / 有未过 / 有未核对），列表与模态才拍得出三态的区别：
//    只给一种的话，「有未核对」那条分支在证据图里等于没被覆盖。
bool SeedReportFixture(const std::filesystem::path& root) {
    struct Seed {
        int ord;
        const char* json;
    };
    const Seed seeds[] = {
        {1,
         R"({"stage":"V8","stage_name":"CONTINUITY","chapter_id":1,"shots":13,"pairs":12,)"
         R"("rules_checked":12,"failed":0,"unverified":0,"issues":[],"notes":[]})"},
        {2,
         R"({"stage":"V8","stage_name":"CONTINUITY","chapter_id":2,"shots":10,"pairs":9,)"
         R"("rules_checked":12,"failed":2,"unverified":0,"issues":[)"
         R"({"code":"C1","severity":"high","detail":"主角外套在第 2 场与第 5 场之间由深灰变为藏青"},)"
         R"({"code":"C7","severity":"low","detail":"第 3 镜缺 prev_shot_id，跨镜比较只用了单侧状态"}],)"
         R"("notes":[]})"},
        {3,
         R"({"stage":"V8","stage_name":"CONTINUITY","chapter_id":3,"shots":8,"pairs":7,)"
         R"("rules_checked":12,"failed":0,"unverified":2,"issues":[],)"
         R"("notes":["C4：场 2 未登记起止状态，时间线无从比较",)"
         R"("C9：第 8 镜没有关联实体，服装一致性无从核对"]})"},
    };
    for (const Seed& seed : seeds) {
        std::ostringstream dir;
        dir << "ch" << std::setw(3) << std::setfill('0') << seed.ord;
        if (!util::WriteFileEnsuredDir(root / "work" / dir.str() / "v08_continuity.json",
                                       seed.json)) {
            return false;
        }
    }
    // 产物页（底栏第 3 个页签）扫的是 `<root>/output`。
    //
    // ⚠️ 以前这个目录是空的，于是那一页只拍到「output/ 目录是空的」——**页面拍到了，
    //    列表分支从来没被执行过**。和「图评审」那轮同一类洞：fixture 缺数据 = 覆盖洞。
    //    造几个**不同扩展名、不同大小**的文件，好让「kind 标签 / 字节数 / 点击打开」
    //    这几处都真的有值可显示。
    struct ArtifactSeed {
        const char* rel;
        const char* body;
    };
    const ArtifactSeed artifacts[] = {
        {"output/ch001_S001.png", "not-a-real-png-fixture"},
        {"output/ch001_S002.png", "not-a-real-png-fixture-2"},
        {"output/ch002_S011.png", "not-a-real-png-fixture-3"},
        {"output/linwan_base_v2.png", "not-a-real-png-fixture-4"},
        {"output/ep001_take01.mp4", "not-a-real-mp4-fixture"},
        {"output/sheet.json", R"({"layout":"1x4","panels":4})"},
    };
    for (const ArtifactSeed& art : artifacts) {
        if (!util::WriteFileEnsuredDir(root / std::filesystem::path(art.rel), art.body)) {
            return false;
        }
    }
    return true;
}

} // namespace

ReviewResult RunReview(Host& host, Shell& shell, const std::filesystem::path& outputDir) {
    ReviewResult result;
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    // ⚠️ 文件名是**脚本契约**：scripts/run_reviews.ps1:56 读的是 `shots-manifest.txt`。
    //    早先这里叫 `manifest.txt`，于是脚本永远读到 NO-REPORT，verdict 恒空 ——
    //    整个取证流水线一直在假绿。改名后还要在末尾写 `overall=PASS|FAIL`
    //    （脚本 :61 靠这条正则判 verdict），缺了它同样判不出来。
    const std::filesystem::path manifest = outputDir / kManifestName;
    std::filesystem::remove(manifest, ec); // 重跑不追加，避免上一次的行混进来
    WriteManifest(manifest, "# shine imgui review — capture 1600x960, glReadPixels(GL_RGBA)");

    const auto grab = [&](const std::string& name, int workspace, shine::theme::ThemeId theme) {
        std::uint64_t hash = 0;
        if (Shoot(host, shell, outputDir, name, workspace, theme, &hash)) {
            result.captured++;
        } else {
            result.failed++;
        }
        return hash;
    };

    // ---- 第一段：当前主题下全部工作区各一张（含组件画廊）----
    const shine::theme::ThemeId base = shine::theme::CurrentThemeId();
    for (int workspace = 0; workspace < pages::kWorkspaceCount; ++workspace) {
        grab(std::string("ws-") + shine::pages::WorkspaceIcon(workspace), workspace, base);
    }

    // ---- 第二段：6 个业务工作区 × 5 套主题（phases.md P6.3 的 30 组）----
    // 每个工作区记下 5 个哈希，末尾检查两两不同：**主题没真正切过去**时 5 张会完全一样。
    std::map<int, std::vector<std::uint64_t>> themedHashes;
    for (const shine::theme::ThemeId theme : shine::theme::kAllThemes) {
        for (const int workspace : kThemedWorkspaces) {
            themedHashes[workspace].push_back(grab(
                std::string("theme-") + std::string(shine::theme::ThemeIdKey(theme)) + "-" +
                    shine::pages::WorkspaceIcon(workspace),
                workspace, theme));
        }
    }

    // 跑完回到起始主题，别把用户的 theme.json 留在取证用的最后一套上。
    shell.SetTheme(base);

    // ---- 第三段：底栏「校验报告」页 + 逐项详情模态 ----
    //
    // ⚠️ 这三张的前置动作必须写全：摆一个带**真实格式**报告的工程 → 选第 4 个页签 →
    //    等 worker 那次目录遍历真的回投 → 才推进到可抓图的状态。
    //    少任何一步，拍到的就是「未打开工程」或「尚无校验报告」空态 ——
    //    图名和内容对不上，而 manifest 照样记 saved。
    bool reportScanConverged = false;
    bool bookSnapshotConverged = false;
    // 资产快照也要显式等：kind 树的数据来自同一次 worker 读库，没落地就拍到空态。
    bool assetSnapshotConverged = false;
    // 产物页的列表也走 worker 扫目录。同样要等收敛：空态是**正确**的输出，
    // 不等就拍到空态，manifest 照样记 saved。
    bool artifactsConverged = false;
    // 悬停探针：几个目标里几个真的产生了像素变化。
    int hoverProbesPassed = -1;
    // 快捷键动作判据：几个快捷键按下去真的改变了界面状态。
    int shortcutsPassed = -1;
    // 浮层点击探针的结果。**必须与 hover / shortcuts 同级**：它的效果不在像素上，
    // 只打日志而 overall 仍 PASS 就是假绿（见下面 pass 的那条判据）。
    int overlayClicksPassed = 0;
    int kOverlayClickTotal = 4;
    int kShortcutTotal = 7;
    // 本轮探针总数（targets 数组长度）。写死 4 的话加探针时会忘了改判据，判据跟着目标数走。
    int kHoverProbeTotal = 4;
    // 受控图的像素哈希。第三/四段的每张都是**显式驱动**出来的（开工程 / 选页签 /
    // 选条目 / 开浮层），彼此应该两两不同 —— 出现重样就说明其中一张没拍到它承诺的状态。
    // ⚠️ 只对这批做重样判据，不对全树做：`ws-*`（当前主题）与 `theme-<base>-*`
    //    拍的是同一个工作区同一套主题，只因累积的界面状态不同才没有撞上 ——
    //    拿它们互相比较，判据就成了碰运气。
    std::vector<std::uint64_t> drivenHashes;
    std::vector<std::string> drivenNames;
    const auto grabDriven = [&](const std::string& name, int workspace,
                                shine::theme::ThemeId theme) {
        drivenHashes.push_back(grab(name, workspace, theme));
        drivenNames.push_back(name);
    };
    const std::filesystem::path fixture = outputDir / "_review_reports_project";
    if (!SeedReportFixture(fixture)) {
        shine::log::Error("review: 报告 fixture 写不出来，校验报告三张取证跳过");
    } else {
        // SetProjectRoot 会立刻写 layout.dat —— 先把用户登记的工程根读出来，抓完原样还回去。
        const std::filesystem::path savedRoot = shell.projectRoot();
        const std::string savedName = shell.projectName();
        const int savedTab = shell.dockTab();

        shell.SetProjectRoot(fixture, "取证样例工程");
        shell.SetDockTab(3);
        int waited = 0;
        while (!shell.ReportsReady() && waited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++waited;
        }
        // ⚠️ 等待的结论要写进 manifest。上一轮把一个等待的返回值 (void) 掉，
        //    拍到「扫描中 · 已载入 0 张」那一帧，manifest 依旧 saved。
        reportScanConverged = shell.ReportsReady();
        WriteManifest(manifest, std::string("report-scan=") +
                                    (reportScanConverged ? "converged" : "TIMEOUT") +
                                    " after " + std::to_string(waited) + " frames");

        const int overview = static_cast<int>(pages::Workspace::Overview);
        grabDriven("dock-reports", overview, base);

        // 「绑定工程之后」的总控台 —— 与第一段的 `ws-gauge` 是**不同分支**。
        //
        // ⚠️ 第一段那 7 张拍的是**未打开项目**：`s.bound == false`，于是运行状态卡走
        //    「未绑定工程根」分支，两个运行按钮因为 `!s.bound` 而灰掉，而「阶段执行体
        //    未接入 · 两个运行按钮已禁用」那行说明的条件里有 `s.bound` ⇒ **不画**。
        //    也就是说：不单独拍这一张，新增的这条分支在整轮里**一次都跑不到**。
        //    这与「fixture 缺数据造成覆盖洞」是同一族：图拍到了 ≠ 分支跑到了。
        //    （KPI 那半边相反：`wired == false` 是无条件的，所以 ws-gauge 里已经是「未接入」。）
        shell.SetWorkspace(overview);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("overview-bound", overview, base);

        // 右栏滚到底：「章节 × V 阶段」矩阵排在最后（停止条件 → 最近产物 → 运行信息 →
        // 矩阵），默认滚动位置只能拍到前两张。
        //
        // ⚠️ 旧门禁只查「图写出来了」，矩阵那张从来没被拍过 —— 与「fixture 缺数据
        //    造成覆盖洞」同族：**页面上有一块内容，但它不在任何一张截图里**。
        //    粘性请求（ScrollRegion::RequestScrollBottom）解决的是「取证驱动在
        //    DrawFrame 外面，拿不到局部 ScrollRegion 对象」这个接线问题。
        //
        // ⚠️ 下面必须**再判一次请求真的生效**，不能只看两张图不一样：
        //    第一版这里只有 `identical-driven-pairs = 0` 这一个判据，而它是绿的 ——
        //    实际滚都没滚（SetScrollHereY 在构造期没有 item 可依）。两张图仍然
        //    不同，只是因为左上角 326 个像素在动。**「图不一样」有二义性**：
        //    「滚到位了」和「别处在动」分不开。于是加 ScrollRegion::LastApplied()
        //    这个正交信号 —— 它读产品自己的 scrollY / maxScrollY，不从像素反推。
        kit::ScrollRegion::RequestScrollBottom("ov-right");
        // 2 帧：第 1 帧消费请求，第 2 帧才读得到生效后的 scrollY（核销就在第 2 帧）。
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        const kit::ScrollRegion::ScrollApplied scrolled = kit::ScrollRegion::LastApplied();
        // ⚠️ 三个分支必须**平级**写。早先写成
        //    `if (id == "ov-right" && maxScrollY > 0 && !ok)`，于是「maxScrollY == 0」
        //    （内容压根没超出区域）和「id 对不上」（请求压根没被核销）都落进 else，
        //    **静默通过** —— 而「没超出」恰恰是最该红的覆盖洞。判据自己漏一支，
        //    表现得和「一切正常」一模一样。
        if (scrolled.id != "ov-right") {
            shine::log::Error(
                "review: ov-right 的滚动请求从没被核销（id=\"{}\"）—— 请求没送到，"
                "或者 ScrollRegion 那一帧没被构造（覆盖洞）",
                scrolled.id);
            result.scrollFailed = true;
        } else if (scrolled.maxScrollY <= 0.0f) {
            shine::log::Error(
                "review: ov-right 期望滚动上限=0（自报内容高={:.1f} / 可视高={:.1f}）—— "
                "内容没超出就说明右栏压根没画够，或者 setContentHeight 没被调到（覆盖洞）",
                scrolled.contentHeight, scrolled.viewHeight);
            result.scrollFailed = true;
        } else if (!scrolled.ok) {
            shine::log::Error(
                "review: ov-right 滚到底失败：scrollY={:.1f} / 期望上限={:.1f} —— "
                "overview-vstages 这张图拍的仍是顶部，等于矩阵没被取证"
                "（ImGui 侧上限={:.1f}：它比期望值小说明内容高度没报上去，滚轮也滚不动）",
                scrolled.scrollY, scrolled.maxScrollY, scrolled.imGuiMaxScrollY);
            result.scrollFailed = true;
        } else if (scrolled.imGuiMaxScrollY < scrolled.maxScrollY - 1.0f) {
            // 粘性请求到位了，但 ImGui 自己不认识这个高度 ⇒ 用户用滚轮依然滚不动。
            // 判据必须报出来，否则「取证能滚」会掩盖「产品滚不动」。
            shine::log::Error(
                "review: ov-right 内容高度没报给 ImGui（期望上限={:.1f} / ImGui 侧={:.1f}）"
                " —— 自绘内容不被 ImGui 计入 ContentSize，滚轮无法滚动",
                scrolled.maxScrollY, scrolled.imGuiMaxScrollY);
            result.scrollFailed = true;
        }
        grabDriven("overview-vstages", overview, base);
        // 复位：滚回顶部，否则后面几张图都在底部状态。
        //
        // ⚠️ 只登记**一次**。同一 id 先后登记「底 / 顶」时 ScrollRegion 按后登记者覆盖，
        //    多写一次不影响结果，但会让读代码的人以为「两次登记是必要的」—— 而实际上
        //    分成两个请求入口（不带 target 参数）时两次登记会互相抵消，正好停在中途，
        //    两张图都拍不到位。顶与底必须共用 RequestScroll 这一个入口。
        kit::ScrollRegion::RequestScrollTop("ov-right");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 工作区主滚动区（workspace-scroll）也要验 ----
        //
        // ⚠️ 上一轮只验了 `ov-right`，而**工作区才是主滚动区** —— 修好 `setContentHeight`
        //    之后它才第一次真的能滚，却没有任何探针。一个没人验的滚动区等于没有。
        //    这条还顺带把「页面自报内容高度」顶替 2400 这件事验了：期望上限变小 ⇒
        //    滚动范围跟着内容走，而不是那 1700px 的空盒子。
        kit::ScrollRegion::RequestScrollTop("ov-right");
        kit::ScrollRegion::RequestScrollTop("workspace-scroll");
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        kit::ScrollRegion::RequestScrollBottom("workspace-scroll");
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        const kit::ScrollRegion::ScrollApplied ws = kit::ScrollRegion::LastApplied();
        if (ws.id != "workspace-scroll") {
            shine::log::Error(
                "review: workspace-scroll 的滚动请求从没被核销（id=\"{}\"）—— 工作区滚不动，"
                "任何超出视口的内容都够不着", ws.id);
            result.scrollFailed = true;
        } else if (ws.maxScrollY <= 0.0f) {
            shine::log::Error(
                "review: workspace-scroll 期望滚动上限=0（自报内容高={:.1f} / 可视高={:.1f}）"
                " —— 页面没上报内容高度（退回 2400 兜底）或内容确实没超出",
                ws.contentHeight, ws.viewHeight);
            result.scrollFailed = true;
        } else if (!ws.ok) {
            shine::log::Error(
                "review: workspace-scroll 滚到底失败：scrollY={:.1f} / 期望上限={:.1f}"
                "（ImGui 侧={:.1f}）", ws.scrollY, ws.maxScrollY, ws.imGuiMaxScrollY);
            result.scrollFailed = true;
        } else if (ws.imGuiMaxScrollY < ws.maxScrollY - 1.0f) {
            shine::log::Error(
                "review: workspace-scroll 内容高度没报给 ImGui（期望上限={:.1f} / ImGui 侧={:.1f}）"
                " —— 滚轮无法滚动", ws.maxScrollY, ws.imGuiMaxScrollY);
            result.scrollFailed = true;
        } else {
            shine::log::Info("review: workspace-scroll 可滚 自报内容高={:.1f} 可视高={:.1f} 上限={:.1f}",
                             ws.contentHeight, ws.viewHeight, ws.maxScrollY);
        }
        grabDriven("overview-scrolled", overview, base);
        kit::ScrollRegion::RequestScrollTop("workspace-scroll");
        kit::ScrollRegion::RequestScrollTop("ov-right");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // 两个模态状态拍的是**不同内容**（一份有未过项、一份有未核对项），
        // 于是它们既不该与列表图相同，也不该彼此相同。
        shell.SetReportDetail(1);
        grabDriven("report-modal", overview, base);
        shell.SetReportDetail(2);
        grabDriven("report-modal-unverified", overview, base);
        // ⚠️ 必须**先**关掉报告模态再拍下面两个：同一 foreground draw list 上
        //    浮层的 z 序由 DrawFrame 里的调用顺序决定，报告模态画在设置模态**之后**，
        //    两个同时开着时后画的会盖住先画的 —— 拍出来两张图会一模一样。
        shell.SetReportDetail(-1);

        // 另外两个浮层同样要验：它们与页面分别画在不同 draw list 上，z 序错了不崩不报，
        // 只是被工作区内容盖住。上一轮就是靠这两张才发现「模态只剩一条表头带」。
        shell.SetSettingsOpen(true);
        grabDriven("overlay-settings", overview, base);
        shell.SetSettingsOpen(false);
        shell.SetCommandPaletteOpen(true);
        grabDriven("overlay-palette", overview, base);
        shell.SetCommandPaletteOpen(false);

        // ---- 第四段：侧栏树 + 检查器 ----
        //
        // ⚠️ 拍这两张必须**真的打开带 novel.db 的工程**并等 worker 读完。侧栏与检查器
        //    读的是 pages::BookSide() 那份只读快照（由 ApplyBook 在 UI 线程重建），
        //    没绑上就只会拍到诚实空态。快照是异步的 —— 显式等到 chapters 非空为止，
        //    等待结论写进 manifest。
        const int storyboard = static_cast<int>(pages::Workspace::Storyboard);
        shell.SetWorkspace(storyboard);
        int bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().chapters.size() < 2) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool bookReady = !pages::BookSide().loading && pages::BookSide().chapters.size() >= 2;
        // ⚠️ 这一行别漏：漏了的话 manifest 头部会写 book-snapshot: TIMEOUT 而正文写
        //    converged，overall 直接 FAIL —— 自己跟自己打架，比缺判据更难查。
        bookSnapshotConverged = bookReady;
        WriteManifest(manifest, std::string("book-snapshot=") + (bookReady ? "converged" : "TIMEOUT") +
                                    " chapters=" + std::to_string(pages::BookSide().chapters.size()) +
                                    " shots=" + std::to_string(pages::BookSide().shots.size()) +
                                    " after " + std::to_string(bookWaited) + " frames");
        grabDriven("side-tree", storyboard, base);

        // 选中第 2 镜再拍一张：证明检查器属性**跟着选中项变**，不是静态占位。
        pages::SelectBookShot(1);
        grabDriven("inspector-shot", storyboard, base);
        // 换到第 3 章（库里刻意留空，没取过它的镜）→ 侧栏只剩章节点。
        // ⚠️ 等待条件必须**同时**满足 !loading 且 selectedChapter 真的换过去了。
        //    只等 !loading 是不够的：loading 标志是派发瞬间翻的，而视图那时还没落地，
        //    一个字不改就成立 → 一帧都不等 → 拍出来的还是上一章。
        pages::SelectBookChapter(2);
        bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().selectedChapter != 2) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool switched = !pages::BookSide().loading && pages::BookSide().selectedChapter == 2;
        WriteManifest(manifest, std::string("chapter-switch=") +
                                    (switched ? "converged" : "TIMEOUT") + " after " +
                                    std::to_string(bookWaited) + " frames shots=" +
                                    std::to_string(pages::BookSide().shots.size()));
        bookSnapshotConverged = bookSnapshotConverged && switched;
        grabDriven("side-tree-empty-chapter", storyboard, base);
        pages::SelectBookChapter(0);

        // ---- 第五段：资产 kind 筛选树 + 检查器「关联」段 + 快速跳转 ----
        //
        // 这三样都是本轮新接的，而且**都是只画不联动就会看不出错**的东西：
        //   · kind 树 —— chip 点了主区网格不动、叶子点了详情不换，都只是"看着没反应"
        //   · 关联段 —— 段头以前是 const 局部数组，箭头点了永远不展开
        //   · 快速跳转 —— 按钮画出来不跳 workspace，截图上完全看不出
        // 所以每张的前置动作都显式写全，等待结论也进 manifest。
        const int assetsWs = static_cast<int>(pages::Workspace::Assets);
        const int novelWs = static_cast<int>(pages::Workspace::Novel);
        // 出图 / 出片页的常量在**这里**声明：悬停探针（第六段）也要用，而它排在第七段
        // 之前。声明在各自用到的那一段里，后一段就看不见 —— 第一版就是这么写的，
        // 编译直接报「未声明」。共用常量统一提到最早的用点。
        const int imageWs = static_cast<int>(pages::Workspace::ImageFlow);
        const int videoWs = static_cast<int>(pages::Workspace::VideoFlow);
        const int storyboardWs = static_cast<int>(pages::Workspace::Storyboard);
        shell.SetWorkspace(assetsWs);
        bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().assets.empty()) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool assetsReady =
            !pages::BookSide().loading && !pages::BookSide().assets.empty();
        WriteManifest(manifest, std::string("asset-snapshot=") +
                                    (assetsReady ? "converged" : "TIMEOUT") + " assets=" +
                                    std::to_string(pages::BookSide().assets.size()) + " after " +
                                    std::to_string(bookWaited) + " frames");
        assetSnapshotConverged = assetsReady;
        grabDriven("assets-kind-tree", assetsWs, base);

        // 点第 2 个实体（库里是「周姨」）。必须走 shell.SelectAsset 而不是分别写两个状态 ——
        // 侧栏高亮与主区详情是同一份选中的两个投影，分开写迟早只改得动一边。
        if (assetsReady) {
            shell.SelectAsset(1);
            grabDriven("assets-leaf-selected", assetsWs, base);
        }

        // 筛到「物品」：fixture 里 person×2 + item×1，所以这一类只剩 1 张卡。
        // 换了筛选若网格张数不变，就是 chip 没接上主区。
        shell.SetKindFilter("item");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("assets-kind-filtered", assetsWs, base);
        // ⚠️ 上一张停在**详情**态，证明不了 chip 也筛了**主区网格** —— 详情是按
        //    未筛选全集取的（设计稿 Assets.jsx:131 的 cur 口径），怎么筛都显示周姨。
        //    要证明共享筛选，必须切到总览再拍：筛「物品」后网格应只剩 1 张卡。
        shell.SetAssetsOverview(true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("assets-grid-filtered", assetsWs, base);
        shell.SetKindFilter({});
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("assets-grid-all", assetsWs, base);
        shell.SetAssetsOverview(false);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // 检查器「关联」段：默认收起（设计稿 Shell.jsx:146 的 c:false），必须显式展开。
        // ⚠️ 这正是本轮修掉的死段头 —— 展开态以前是每帧新建的 const 局部数组。
        pages::SelectBookShot(0);
        shell.SetInspectorSection(2, true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        const pages::BookRelationView relation = pages::BookSide().relation;
        WriteManifest(manifest,
                      std::string("relation=") + (relation.foreshadows.empty() ? "no-foreshadow" : "ok") +
                          " tags=" + std::to_string(1 + (relation.foreshadows.empty() ? 0 : 1) +
                                                   (relation.sceneOrd > 0 ? 1 : 0) +
                                                   (relation.shotCode.empty() ? 0 : 1)));
        grabDriven("inspector-relations", storyboard, base);
        shell.SetInspectorSection(2, false);

        // 小说侧栏的「快速跳转」按钮组：默认全展开，点一下真的会切工作区。
        shell.SetWorkspace(novelWs);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("side-jump-buttons", novelWs, base);

        // 小说页的另外两个**有内容**的模式：设定（实体网格）与流水线（阶段表）。
        // ⚠️ 这两张以前不存在。`mode_` 只能靠点标签切换，取证到不了，于是这两个模式
        //    里的东西从来没被看过一眼 ——「设定」那张网格的卡片是**反向矩形**（整张
        //    不画也不可点）就是这么活下来的：不是没人修，是没人拍到过。
        //    「覆盖 7 个工作区」不等于「覆盖每个工作区的每个视图」，多态视图要逐个点名。
        shell.SetNovelMode(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("novel-mode-world", novelWs, base);
        shell.SetNovelMode(3);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("novel-mode-pipeline", novelWs, base);
        shell.SetNovelMode(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 第七段：把「取证进不去」的视图逐个点名补齐 ----
        //
        // 本轮的第一条教训：**覆盖要按「视图」点名，不是按「页面」点名**。
        // 「7 个工作区全拍了」听起来够了，但一个工作区里往往还有 4 个 dock 页签、
        // 3 个检查器段、2 个右侧面板页签 —— 它们只能靠点 UI 切换，没有 public 入口，
        // 取证就永远进不去，里面有什么缺陷也就没人看得见。已经这样漏掉过一个
        // 整张不画的卡片网格。所以这一段专门补：能进但没拍的（dock 0/1/2、检查器
        // 「预览」段），以及刚加了入口的（出图/出片的第二个面板页签、画布折叠态）。
        //
        // 每张都必须显式把状态设成它承诺的样子，跑完再复位 —— 否则就是「图名和内容
        // 对不上，而 manifest 照样记 saved」。工作区常量在上面统一声明过了。

        // 底栏四个页签：以前只拍了 3（校验报告），0/1/2 三个页签**从来没被拍过**。
        // 这三个都是大面积视图（任务队列 / 日志 / 产物），最该有证据图。
        //
        // ⚠️ 产物页（2）走 worker 扫 `<root>/output`，**必须等它回投**再拍。
        //    不等的话拍到的是「目录是空的」—— 而那个空态本身是**正确**的输出，
        //    manifest 同样记 saved，图名与内容对不上却抓不到。这就是「等待结果要写进
        //    manifest」那条纪律的又一个实例：结论进 manifest，也进 overall 判据。
        shell.SetDockTab(2);
        int artifactWaited = 0;
        while (shell.ArtifactRowCount() == 0 && artifactWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++artifactWaited;
        }
        const int artifactRows = shell.ArtifactRowCount();
        // ⚠️ 这里是**赋值**给外层的那个变量。早先写成 `const bool artifactsConverged = …`
        // 在块内又声明了一个同名局部量，把外层的遮住 —— manifest 那行写的是
        // `converged`（局部值），而 `overall` 行读的是外层的 false，于是同一件事
        // 在两处得到相反结论。判据变量一律**赋值**，不在块内重新声明。
        artifactsConverged = artifactRows > 0;
        WriteManifest(manifest, std::string("artifact-snapshot=") +
                                    (artifactsConverged ? "converged" : "TIMEOUT") +
                                    " rows=" + std::to_string(artifactRows) + " after " +
                                    std::to_string(artifactWaited) + " frames");
        grabDriven("dock-artifacts", overview, base);
        for (int tab : {0, 1}) {
            shell.SetDockTab(tab);
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            grabDriven(std::string("dock-") + DockTabSlug(tab), overview, base);
        }
        shell.SetDockTab(3);

        // 检查器第 1 段「预览」：以前只拍过段 0（属性）与段 2（关联）。
        shell.SetWorkspace(novelWs);
        shell.SetInspectorSection(0, false);
        shell.SetInspectorSection(1, true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("inspector-preview", novelWs, base);
        shell.SetInspectorSection(1, false);
        shell.SetInspectorSection(0, true);

        // 出图页右侧面板的第二个页签 + 画布折叠态。
        shell.SetImageFlowPanel(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("imageflow-panel-batch", imageWs, base);
        // 第三个页签「图评审」：以前画 5 行假复选框（返回值丢弃 ⇒ 点不动、勾选态写死
        // i < 3、五行标签全是同一个字符串「构图稳定」）。静息态截图看着还挺像个评审页，
        // 所以必须有证据图盯着这一页。
        shell.SetImageFlowPanel(2);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("imageflow-panel-review", imageWs, base);
        shell.SetImageFlowFolded(true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("imageflow-canvas-folded", imageWs, base);
        shell.SetImageFlowFolded(false);
        shell.SetImageFlowPanel(0);

        // 出片页右侧面板的第二个页签「视频任务」。以前是 4 条写死的 20/40/60/80 进度条、
        // running 恒为第一条 —— 界面上永远显示「第一条在跑、其余到 80%」。
        shell.SetVideoFlowPanel(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("videoflow-panel-tasks", videoWs, base);
        shell.SetVideoFlowPanel(0);

        // 分镜页选中的镜。默认 0 = 第一张，**不显式换一张就证明不了选中态会跟着动**
        // —— 与「页面恰好停在你想要的状态」是同一类陷阱。
        //
        // ⚠️ 两张**必须成对**：只有「换到第 2 张」那一张时，它与任何别的图都不重样，
        //    判据「受控图不许重样」也就抓不到「这个入口是死的」。配一张默认态，
        //    入口一旦接错字段（真发生过：写进了零读点的 `selectedShot_`）两张就逐字节
        //    相同，判据当场变红。**单独一张「证明性」的截图，证明不了任何东西。**
        shell.SelectStoryboardShot(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("storyboard-shot-1", storyboardWs, base);
        shell.SelectStoryboardShot(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("storyboard-shot-2", storyboardWs, base);
        shell.SelectStoryboardShot(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 第六段：悬停态 + toast ----
        //
        // 前 52 张全是**静息态**。设计稿里几乎每个控件都定义了 `:hover`，而悬停恰恰是
        // 最容易整条链路断掉的状态：win32 后端接没接上、HitTest 的 item 有没有占位、
        // 控件有没有 hover 样式 —— 三者任何一个断了，界面都只是「鼠标划过去没反应」，
        // 静息态截图**完全看不出来**。所以单独验，并且把「悬停前后必须像素不同」
        // 做成硬判据。
        // 坐标来自 design-spec §4 的定宽栅格（Rail 56 / SidePanel 240 / TopBar 46），
        // 逐个对着上一段的截图核过。⚠️ 布局一改这里就会偏 —— 偏了的症状不是报错，
        //    而是判据报「悬停前后相同」，属于会自己暴露的那种。
        const HoverTarget targets[] = {
            // 侧栏 2×2 里的「资产」
            {"hover-jump-btn", novelWs, 123.0f, 135.0f},
            // 左栏第 4 个开关
            {"hover-rail-toggle", novelWs, 28.0f, 275.0f},
            // kind 树里的实体叶子（侧栏，与主区视图无关）
            {"hover-tree-node", assetsWs, 170.0f, 210.0f},
            // 总览网格的一张卡 —— 必须显式切回总览并清空 kind 筛选，
            // 否则上一条探针留在详情态/筛选态，这张坐标上压根没有卡片。
            {"hover-asset-card", assetsWs, 420.0f, 200.0f, true, true},
            // 底栏「校验报告」的一行
            {"hover-dock-row", overview, 420.0f, 790.0f},
            // 检查器「属性」段头
            {"hover-inspector-head", novelWs, 1400.0f, 69.0f},
            // 判别探针：同样这两个控件换到另一个工作区再试一次。
            // 首轮实测 novel 上的 3 个全灭、assets/overview 上的 3 个全过，所以要分清
            // 是「小说工作区整体没有 hover」还是「这几个控件本身没 hover 样式」。
            {"hover-rail-toggle-overview", overview, 28.0f, 275.0f},
            {"hover-dock-row-novel", novelWs, 420.0f, 790.0f},
            // 第七段新覆盖的视图。坐标逐个对着 out/review-* 里那些新图量的；
            // 命中数与命中的 widget id 都会进日志，坐标偏了会直接报「一个控件都没命中」
            // 而不是含糊的「悬停前后相同」。
            // 底栏产物页第一行（fixture 造了 6 个产物，行有 hover 底色）
            {"hover-artifact-row", overview, 200.0f, 789.0f, false, false, 2},
            // 出图页右侧面板的页签。⚠️ 打的是**非选中**项「图评审」：设计稿
            // `.seg > button:hover` 只改文字色，`.on` 本来就是 text-primary，
            // 所以**选中项按设计就没有 hover 反馈**。拿选中项当探针等于测一个设计稿
            // 不承诺的东西，判据会一直红。面板本身停在 1（批量出图）以保证这一页有内容。
            {"hover-imageflow-tab", imageWs, 1105.0f, 165.0f, false, false, 0, 1},
            // 小说页 8 个模式标签里的「设定」（以前只有 Clicked，标签不可反馈）
            {"hover-novel-mode-tab", novelWs, 380.0f, 100.0f, false, false, 0, 0, 1},
        };
        int hoverOk = 0;
        int hoverUnstable = 0;
        int hoverBroken = 0;
        for (const HoverTarget& target : targets) {
            (void)ProbeHover(host, shell, outputDir, target, base, drivenHashes, drivenNames,
                             &hoverOk, &hoverUnstable, &hoverBroken, &result.captured,
                             &result.failed);
        }
        hoverProbesPassed = hoverOk;
        const int hoverTotal = static_cast<int>(std::size(targets));
        WriteManifest(manifest, std::string("hover-probes=") + std::to_string(hoverOk) + "/" +
                                    std::to_string(hoverTotal) + " changed-vs-rest" +
                                    (hoverBroken > 0 ? "  broken=" + std::to_string(hoverBroken)
                                                     : std::string()) +
                                    (hoverUnstable > 0
                                         ? "  unstable=" + std::to_string(hoverUnstable)
                                         : std::string()));
        kHoverProbeTotal = hoverTotal;

        // SHINE_SCAN=<ws>:<x0>:<y0>:<x1>:<y1>:<step>：热区扫描，**跑在探针之后**。
        //
        // ⚠️ 位置很要紧：放在探针前面扫到的是「fixture 还没就绪」的那一帧 ——
        //    小说侧栏的「快速跳转」2×2 那一段那时候压根没画，扫出来的结果里
        //    根本没有 `jump-*`，而据此去改探针坐标就会一路改错。探针跑完时
        //    fixture 已经落地（章节 / 树 / 资产都在），那才是量坐标的正确状态。
        //
        // 用途：探针报「点空了」时，坐标应当**量**出来而不是猜。诊断工具，
        // 不参与 pass/fail（它只打 id，不判像素变化）。
        if (const char* scan = std::getenv("SHINE_SCAN"); scan != nullptr) {
            int ws = 0;
            float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, step = 10.0f;
            if (std::sscanf(scan, "%d:%f:%f:%f:%f:%f", &ws, &x0, &y0, &x1, &y1, &step) == 6) {
                shell.SetTheme(base);
                shell.SetWorkspace(ws);
                host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
                shine::log::Info("scan: start ws={} rect=({:.0f},{:.0f})-({:.0f},{:.0f}) step={:.0f}",
                                 ws, x0, y0, x1, y1, step);
                ScanHotspots(host, shell, x0, y0, x1, y1, step);
            }
        }

        // ---- 第八段：动作判据（快捷键按了到底有没有发生） ----
        //
        // 这几轮反复抓到的都是同一类：「界面上有个可点的东西」≠「点了会发生什么」。
        // 死模态、死入口、点不动的复选框、写死的数据 —— 而「按了没反应」在**静息态
        // 截图上与「按了」长得一模一样**，光靠看图永远抓不到。
        //
        // 做法：注入按键（必须在 `NewFrame` 之前，见 Host::SetFrameKeyOverride），
        // 比对**该动作真正会改的那个状态**前后变没变。
        //
        // ⚠️ 早先这里七个动作**共用一个** `LayoutStateHash()`，于是 r52 报 3/7：
        //    B/J/I 改的是 `layout_` 里落盘的字段，命中；K/T/N/O 改的是**命令面板开态、
        //    当前主题、项目中心开态** —— 这三个**本来就不该持久化**，所以进不了布局哈希，
        //    哈希当然不变。判据把「哈希没覆盖到」误报成了「产品有空动作」。
        //    差点去改本来正确的代码。教训：**判据自己也会骗人**，覆盖面不足和缺陷
        //    在判据眼里长得一模一样。
        // 现在按语义分四类，每类读产品自己的状态读数（不另发明一份平行定义）。
        enum class ProbeKind {
            Layout,   // 改 layout_ 持久化字段 → 布局态哈希
            Palette,  // 改命令面板开态
            Theme,    // 改当前主题
            Hub,      // 改项目中心开态
        };
        // 读成字符串而不是 uint64：出问题时能直接把 before/after 打出来
        // （「0 → 1」和「deepspace → dusk」都比「哈希没变」信息量大得多）。
        auto readState = [&shell](ProbeKind kind) -> std::string {
            switch (kind) {
            case ProbeKind::Layout:
                return std::to_string(shell.LayoutStateHash());
            case ProbeKind::Palette:
                return shell.commandPaletteOpen() ? "palette=open" : "palette=closed";
            case ProbeKind::Theme:
                return std::string("theme=") + shine::theme::ThemeIdKey(shine::theme::CurrentThemeId());
            case ProbeKind::Hub:
                return shell.projectHubOpen() ? "hub=open" : "hub=closed";
            }
            return "?";
        };
        //
        // ⚠️ 每个动作**独立**记前后状态，并且**跑完复位**：上一条留下的状态会让下一条
        //    「没变」变成「变了」（比如上一条已经收起侧栏，这条再按 B 就是展开）。
        struct ShortcutProbe {
            const char* name;
            int key;          // ImGuiKey 的整数值
            bool ctrl;
            ProbeKind kind;
        };
        const ShortcutProbe shortcuts[] = {
            {"Ctrl+B 侧栏", ImGuiKey_B, true, ProbeKind::Layout},
            {"Ctrl+J 底栏", ImGuiKey_J, true, ProbeKind::Layout},
            {"Ctrl+I 检查器", ImGuiKey_I, true, ProbeKind::Layout},
            {"Ctrl+K 命令面板", ImGuiKey_K, true, ProbeKind::Palette},
            {"Ctrl+T 切主题", ImGuiKey_T, true, ProbeKind::Theme},
            {"Ctrl+N 新建项目", ImGuiKey_N, true, ProbeKind::Hub},
            {"Ctrl+O 打开项目", ImGuiKey_O, true, ProbeKind::Hub},
        };
        int shortcutOk = 0;
        int shortcutDead = 0;
        // Ctrl+T 会**真的换主题**，判据跑完必须还原，否则后面 30 张 theme-* 图
        // 拍的是被换走之后那个主题，而图名还写着原来的名字。
        const shine::theme::ThemeId savedTheme = shine::theme::CurrentThemeId();
        for (const ShortcutProbe& sc : shortcuts) {
            const std::string before = readState(sc.kind);
            host.SetFrameCtrlOverride(sc.ctrl);
            // 帧内自检：把「注入到底有没有到 ImGui」这件事变成可观测的，而不是靠猜。
            // KeyCtrl / IsKeyPressed 任一为假 ⇒ 是**注入**没接上，不是产品缺陷。
            bool sawCtrl = false;
            bool sawPressed = false;
            auto probeFrame = [&](float dt) {
                shell.DrawFrame(dt);
                sawCtrl = sawCtrl || ImGui::GetIO().KeyCtrl;
                sawPressed = sawPressed || ImGui::IsKeyPressed(static_cast<ImGuiKey>(sc.key), false);
            };
            // 按下 → 抬起，两帧：只注入「按下」的话 IsKeyPressed 只在那一帧为真，
            // 而那一帧 DrawFrame 之后紧跟着的帧又把它清掉了，容易整轮测不到。
            host.SetFrameKeyOverride(sc.key, true);
            host.PumpFrames(1, probeFrame);
            host.SetFrameKeyOverride(sc.key, false);
            host.PumpFrames(1, probeFrame);
            host.ClearFrameCtrlOverride();
            const std::string after = readState(sc.kind);
            if (!sawCtrl || !sawPressed) {
                // 注入没到位（Ctrl 没算进 KeyCtrl，或按下沿没被 NewFrame 采到）。
                // 这是**探针**的问题，不能记成「产品有空动作」—— 否则会去改不该改的产品。
                shine::log::Error(
                    "review: 快捷键 {} 的**注入没到位**（saw-ctrl={} saw-pressed={}）—— "
                    "ImGui 压根没看到这个按键。判据不可用，不是产品的空动作",
                    sc.name, sawCtrl ? 1 : 0, sawPressed ? 1 : 0);
                ++shortcutDead;
            } else if (after != before) {
                ++shortcutOk;
            } else {
                ++shortcutDead;
                shine::log::Error(
                    "review: 快捷键 {} 按下去状态**一点没变**（{} → {}）—— 面板上写着它，"
                    "实际是空动作（要么没注册，要么执行体改了一个没人读的状态）",
                    sc.name, before.c_str(), after.c_str());
            }
            // 复位：把这一条造成的变化撤掉，下一条才从同一个起点出发。
            //
            // ⚠️ 早先这里「再按一次同一个快捷键」当复位，对四条是错的：
            //    * Ctrl+N / Ctrl+O 的执行体是 `if (!hubOpen_) ToggleProjectHub()` —— **幂等**，
            //      再按一次什么也不发生，项目中心一直开着，后面所有截图都拍错内容；
            //    * Ctrl+T 是**轮换**主题，再按一次等于把主题真的换走了 —— 判据跑完
            //      残留一个非预期主题，连带后面 30 张 theme-* 图全部拍成另一个主题；
            //    * 只有 B/J/I/K 恰好是纯 toggle，「再按一次」才真的等于撤销。
            // 改成**按语义显式复位**：每条动作记下自己的 before，跑完照着写回去。
            switch (sc.kind) {
            case ProbeKind::Layout:
                // B/J/I 都是纯 toggle，反按一次就是撤销。
                host.SetFrameCtrlOverride(true);
                host.SetFrameKeyOverride(sc.key, true);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                host.SetFrameKeyOverride(sc.key, false);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                host.ClearFrameCtrlOverride();
                break;
            case ProbeKind::Palette:
                shell.SetCommandPaletteOpen(false);
                break;
            case ProbeKind::Theme:
                shell.SetTheme(savedTheme);
                break;
            case ProbeKind::Hub:
                shell.SetHubOpen(false);
                break;
            }
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        }
        shortcutsPassed = shortcutOk;
        kShortcutTotal = static_cast<int>(std::size(shortcuts));
        WriteManifest(manifest, std::string("shortcuts=") + std::to_string(shortcutOk) + "/" +
                                    std::to_string(kShortcutTotal) +
                                    (shortcutDead > 0
                                         ? "  dead=" + std::to_string(shortcutDead)
                                         : std::string()));
        // 收尾复位：命令面板、项目中心、主题都还原到判据开始前的样子。
        shell.SetCommandPaletteOpen(false);
        shell.SetHubOpen(false);
        if (shine::theme::CurrentThemeId() != savedTheme) {
            shell.SetTheme(savedTheme);
        }
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 浮层按钮点击探针 ----
        //
        // 为什么必须有这一段：浮层（设置模态 / 报告模态 / 命令面板 / 主题菜单）的 item
        // 是在**根窗口**里提交的，而工作区是 `BeginChild`。ImGui 定 `g.HoveredWindow`
        // 是「从 `g.Windows` 末尾往前扫、取第一个命中」（imgui.cpp:6573 / 6607），
        // `ItemHoverable` 第一句又判 `if (g.HoveredWindow != window) return false;`
        // （:5156）—— 只要鼠标在工作区范围内，浮层按钮恒 hovered=false / clicked=false。
        //
        // 这个缺陷**在像素上完全看不出来**：按钮照画不误，80 张静息绿图零覆盖，
        // hover 探针也抓不到（它们只打根窗口控件和 child 内部的控件）。
        // 所以判据只能读**产品自己的开态**：点完 `settingsOpen()` 变没变。
        //
        // 「按钮矩形」也必须取产品那份 `SettingsCloseRect()`：在判据里复算一遍
        // `display.x * 0.5f + …` 的话，布局一改就会打在一个已经不存在的位置上，
        // 而它报的仍然是「通过」。
        {
            struct OverlayClickProbe {
                const char* name;
                int key;  // 0 = × 按钮，1 = 点遮罩关闭，2/3 = 验「底下点不动」
            };
            const OverlayClickProbe overlayClicks[] = {
                {"设置模态 ×", 0},
                {"设置模态 点遮罩关闭", 1},
                {"模态开着时点导航栏（不许穿透去切工作区）", 2},
                {"模态开着时点工作区正文（不许穿透到页面控件）", 3},
            };
            for (const OverlayClickProbe& oc : overlayClicks) {
                shell.SetSettingsOpen(true);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                const std::string before = shell.settingsOpen() ? "open" : "closed";
                const int wsBefore = shell.workspace();
                const ImVec2 d = ImGui::GetIO().DisplaySize;
                ImVec2 target{0.0f, 0.0f};
                switch (oc.key) {
                case 0: {
                    const kit::Rect r = shell.SettingsCloseRect();
                    target = ImVec2((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f);
                    break;
                }
                case 1:
                    // 面板正下方偏左：确定落在遮罩里、且不在模态矩形内
                    target = ImVec2(d.x * 0.5f - 420.0f, d.y * 0.5f + 300.0f);
                    break;
                case 2:
                    // 导航栏第 3 格（小说）。它注册在**根窗口**里且排在浮层之前 ——
                    // 「先注册者独占 HoveredId」时它会吃掉这次点击，模态开着也能切工作区。
                    target = ImVec2(24.0f, 128.0f);
                    break;
                default:
                    // 工作区正文中央：这里被 ScrollRegion(BeginChild) 覆盖，
                    // child 退出命中测试之前，底下的页面 item 会照常收到这次点击。
                    target = ImVec2(d.x * 0.5f, d.y * 0.5f);
                    break;
                }
                host.SetFrameMouseOverride(target.x, target.y);
                // 帧内自检：位置注入有没有真的到位。没到位就是**探针**的问题，
                // 不能记成「产品的按钮是死的」—— 那会去改本来正确的代码。
                // ⚠️ 报「没到位」时必须把**实际看到**的坐标一起打出来：光一句
                //    「没到位」分不清是后端覆写了、还是视口塌了。r90 就是靠
                //    「注入到位 + 命中数 0」和浮层探针的「没到位」两条并起来
                //    才定位到宿主层，之前一直误判成布局变了。
                bool sawMouse = false;
                float sawX = 0.0f;
                float sawY = 0.0f;
                host.SetFrameMouseButtonOverride(true);
                host.PumpFrames(1, [&](float dt) {
                    shell.DrawFrame(dt);
                    sawX = ImGui::GetIO().MousePos.x;
                    sawY = ImGui::GetIO().MousePos.y;
                    sawMouse = sawMouse || (std::abs(sawX - target.x) < 0.5f &&
                                            std::abs(sawY - target.y) < 0.5f);
                });
                host.SetFrameMouseButtonOverride(false);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                host.ClearFrameMouseButtonOverride();
                host.ClearFrameMouseOverride();
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                const std::string after = shell.settingsOpen() ? "open" : "closed";
                const int wsAfter = shell.workspace();
                bool ok = true;
                std::string why;
                if (!sawMouse) {
                    ok = false;
                    why = "位置注入没到位（要 " + std::to_string(static_cast<int>(target.x)) +
                          "," + std::to_string(static_cast<int>(target.y)) + "，帧内看到 " +
                          std::to_string(static_cast<int>(sawX)) + "," +
                          std::to_string(static_cast<int>(sawY)) + "）—— 判据不可用，不是产品的缺陷";
                } else if (oc.key >= 2) {
                    // 模态语义要验的是「**底下点不动**」= 工作区索引没变。
                    //
                    // ⚠️ 这里**不能**顺带要求「模态还开着」—— 那是判据自己写错的期望：
                    //    点遮罩关闭本来就是模态的既定行为（DrawSettingsModal 末尾那段），
                    //    第一次跑这条探针就是被它误判成 FAIL 的。症状与「判据有 bug」
                    //    一样：一条永远红的判据和一条坏掉的判据没有区别。
                    if (wsAfter != wsBefore) {
                        ok = false;
                        why = "点击穿透到了底下：workspace " + std::to_string(wsBefore) + " → " +
                              std::to_string(wsAfter) + "（模态开着时底下不该点得动）";
                    }
                } else if (before == after) {
                    // 点了却没反应 = 那个 item 压根没收到点击。
                    ok = false;
                    why = "点了没反应：" + before + " → " + after;
                }
                if (ok) {
                    ++overlayClicksPassed;
                } else {
                    shine::log::Error("review: 浮层交互探针「{}」失败：{}", oc.name, why);
                }
                // 复位：按语义显式复位。**别用「再按一次」** —— 那是 toggle 动作才成立的写法。
                shell.SetSettingsOpen(false);
                if (shell.workspace() != wsBefore) {
                    shell.SetWorkspace(wsBefore);
                }
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            }
        }
        // ---- 项目中心对话框：控件必须收得到鼠标（2026-09-30 补）----
        //
        // 补这条的直接原因：把项目中心那三个对话框从页面层私有外壳换成
        // `kit::ModalFrameRect` 时，我先让遮罩走了 `kit::Scrim`（注册全屏
        // InvisibleButton）。项目中心**没有**因此坏掉 —— 它的对话框是最后画的，
        // 遮罩排在按钮**之后**，「先注册者独占」时按钮照样拿到 HoveredId。
        // 而报告模态**会**坏（外壳先画、按钮后画）。**两种顺序结论相反。**
        //
        // 所以这条判据**不是**遮罩顺序的检测器（那是 `duplicate-hits` 的活：
        // 实测把遮罩改回注册，duplicate-hits 从 0 变 6232，而这条仍然 5/5）。
        // 它验的是另一件事，而且这件也值得验：
        //   **对话框能被打开，且打开之后里面的控件收得到鼠标。**
        // 它当场抓到了一个真 bug：点「打开项目」的那一下点击，在同一帧里
        // `IsMouseClicked()` 仍为真、鼠标仍在工具条上（对话框之外），于是我
        // 加的「点面板外关闭」把刚开的对话框**当场关掉** —— 症状是「闪一下就
        // 没了」。修法是 `HubState::dismissArmed`（见 WorkspaceB）。
        // 那个 bug 编译过、截图正常、打开之前一切正常，**只有真点一下才看得见**。
        //
        // 判据方式：**两步**，都是布局无关的。
        //   第 1 步：全屏扫一遍，找出 `hub-open`（工具条上「打开项目」）的坐标
        //            （**量**出来，不猜 —— 猜出来的坐标在布局一改就静默失效）。
        //   第 2 步：先确认 `hubDialogOpen()` 为真（前置条件！），再全屏扫一遍，
        //            断言集合里至少有一个 `hub-` id。
        {
            bool hubOk = false;
            std::string why;
            const int wsBefore = shell.workspace();
            shell.ToggleProjectHub();
            host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
            if (!shell.projectHubOpen()) {
                why = "项目中心开不起来（ToggleProjectHub 之后 projectHubOpen() 仍是 false）";
            } else {
                const ImVec2 d = ImGui::GetIO().DisplaySize;
                // 第 1 步：全屏扫，找 hub-open 的坐标（不猜）。
                bool foundOpen = false;
                ImVec2 openAt{0.0f, 0.0f};
                for (float y = 0.0f; y <= d.y && !foundOpen; y += 12.0f) {
                    for (float x = 0.0f; x <= d.x; x += 12.0f) {
                        host.SetFrameMouseOverride(x, y);
                        kit::ResetHoveredItemCount();
                        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                        host.ClearFrameMouseOverride();
                        const char* id = kit::LastHoveredItem();
                        if (kit::HoveredItemCount() > 0 && id != nullptr &&
                            std::string(id) == "hub-open") {
                            openAt = ImVec2(x, y);
                            foundOpen = true;
                            break;
                        }
                    }
                }
                if (!foundOpen) {
                    why = "全屏扫不到工具条上的「打开项目」按钮（hub-open）";
                } else {
                    // 第 2 步：点开对话框，再扫一遍。
                    host.SetFrameMouseOverride(openAt.x, openAt.y);
                    host.SetFrameMouseButtonOverride(true);
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    host.SetFrameMouseButtonOverride(false);
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    host.ClearFrameMouseButtonOverride();
                    host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });

                    // ⚠️ **前置条件**：对话框必须真的开着。少了这一步，判据会在
                    // 「压根没开」的状态下去扫一片**没有遮罩**的界面，照常扫到
                    // `hub-*`，于是报通过 —— 而它本该抓的缺陷（遮罩抢走
                    // HoveredId）恰恰只在「开着」时才发生。
                    // 判据自检时我正是这么被骗的：把遮罩改回抢热区，它仍然 5/5。
                    if (!shell.hubDialogOpen()) {
                        why = "点了「打开项目」但对话框没开（hubDialogOpen() 仍是 false）——"
                              "判据前置不成立，这次扫描量的不是遮罩那条路径";
                    } else {
                        const std::set<std::string> ids = ScanHotspots(
                            host, shell, 0.0f, 0.0f, d.x, d.y, 16.0f, /*verbose=*/false);
                        int hubIds = 0;
                        std::string all;
                        for (const std::string& id : ids) {
                            if (id.rfind("hub-", 0) == 0) {
                                ++hubIds;
                            }
                            all += all.empty() ? "" : ", ";
                            all += id;
                        }
                        if (hubIds > 0) {
                            hubOk = true;
                        } else {
                            why = "对话框开着，但整屏扫到的热区里一个 hub-* 都没有 —— 扫到 [" +
                                  (all.empty() ? std::string("(空)") : all) +
                                  "]。遮罩/别的控件抢走了 HoveredId（ImGui 先注册者独占）";
                        }
                    }
                }
            }
            ++kOverlayClickTotal;
            if (hubOk) {
                ++overlayClicksPassed;
            } else {
                shine::log::Error("review: 项目中心按钮判据失败：{}", why);
            }
            // 复位：按语义显式复位（不是「再按一次」—— 那是 toggle 才成立的写法）。
            if (shell.projectHubOpen()) {
                shell.ToggleProjectHub();
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            }
            if (shell.workspace() != wsBefore) {
                shell.SetWorkspace(wsBefore);
            }
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        }
        // ⚠️ manifest 行**必须写在两条浮层判据都跑完之后**：早先写在项目中心那条
        //    之前，于是它报 4/4 而 kOverlayClickTotal 已经是 5 —— 判据自己报的
        //    分母与实际条数不一致，读者会以为 5 条里过了 4 条。
        WriteManifest(manifest, std::string("overlay-clicks=") +
                                    std::to_string(overlayClicksPassed) + "/" +
                                    std::to_string(kOverlayClickTotal));

        // toast：设计稿到处在用的 notify(...)。它只活 3.2s，所以必须**同一轮里**触发
        // 紧跟着抓 —— 跨轮再拍早就过期了，拍到的会是「没有 toast」，而图名还叫 toast。
        shell.Notify("分镜 · 上下文：第 1 章「雨夜里的第七封来信」", shine::theme::Tone::Ok);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("toast-ok", overview, base);
        shell.Notify("ComfyUI 未连接 · 取证期间不连真实服务", shine::theme::Tone::Warn);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        grabDriven("toast-warn", overview, base);

        shell.SetProjectRoot(savedRoot, savedName);
        shell.SetDockTab(savedTab);
    }

    // ---- 判据二：同一工作区在 5 套主题下的像素必须两两不同 ----
    // 只判「PNG 写出来了」的旧门禁会漏掉「主题压根没加载」这种整轮崩掉的情况。
    int identicalPairs = 0;
    for (const auto& [workspace, hashes] : themedHashes) {
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
    for (std::size_t i = 0; i < drivenHashes.size(); ++i) {
        for (std::size_t j = i + 1; j < drivenHashes.size(); ++j) {
            if (drivenHashes[i] != 0 && drivenHashes[i] == drivenHashes[j]) {
                ++drivenDuplicates;
                shine::log::Error("review: {} and {} produced BYTE-IDENTICAL pixels — "
                                  "至少有一张没拍到它承诺的状态",
                                  drivenNames[i], drivenNames[j]);
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
    if (hoverProbesPassed < kHoverProbeTotal) {
        shine::log::Error("review: 悬停探针只过了 {}/{} —— 有控件的 hover 链路是断的，"
                          "而静息态截图看不出来",
                          hoverProbesPassed, kHoverProbeTotal);
    }
    // 快捷键判据：-1 = 这一段没跑到，判它不通过而不是当它通过。
    if (shortcutsPassed < kShortcutTotal) {
        shine::log::Error("review: 快捷键只有 {}/{} 按下去真的改变了界面状态 —— "
                          "面板上写着它们，其中有条是空动作",
                          shortcutsPassed, kShortcutTotal);
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
                      drivenDuplicates == 0 && reportScanConverged && bookSnapshotConverged &&
                      assetSnapshotConverged && artifactsConverged && inverted == 0 &&
                      duplicateHits == 0 &&
                      hoverProbesPassed == kHoverProbeTotal && shortcutsPassed == kShortcutTotal &&
                      overlayClicksPassed == kOverlayClickTotal;
    WriteManifest(manifest, "# shots: " + std::to_string(result.captured) +
                                "  failed: " + std::to_string(result.failed) +
                                "  scroll-failed: " + (result.scrollFailed ? "1" : "0") +
                                "  identical-theme-pairs: " + std::to_string(identicalPairs) +
                                "  identical-driven-pairs: " + std::to_string(drivenDuplicates) +
                                "  inverted-rects: " + std::to_string(inverted) +
                                "  duplicate-hits: " + std::to_string(duplicateHits) +
                                "  hover-probes: " + std::to_string(hoverProbesPassed) + "/" + std::to_string(kHoverProbeTotal) +
                                "  shortcuts: " + std::to_string(shortcutsPassed) + "/" + std::to_string(kShortcutTotal) +
                                "  overlay-clicks: " + std::to_string(overlayClicksPassed) + "/" + std::to_string(kOverlayClickTotal) +
                                "  report-scan: " + (reportScanConverged ? "converged" : "TIMEOUT") +
                                "  book-snapshot: " +
                                (bookSnapshotConverged ? "converged" : "TIMEOUT") +
                                "  asset-snapshot: " +
                                (assetSnapshotConverged ? "converged" : "TIMEOUT") +
                                "  artifact-snapshot: " +
                                (artifactsConverged ? "converged" : "TIMEOUT"));
    if (inverted > 0) {
        WriteManifest(manifest, std::string("last-inverted-rect=") + kit::LastInvertedRect());
    }
    WriteManifest(manifest, std::string("overall=") + (pass ? "PASS" : "FAIL"));
    shine::log::Info("review done: {} saved, {} failed -> {}", result.captured, result.failed,
                     outputDir.string());
    return result;
}

} // namespace shine::imguiverify
