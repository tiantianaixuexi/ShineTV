#include "ui/imgui/verify/ReviewOverlayProbe.h"

#include "core/Log.h"
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/pages/Hub.h"       // 打开项目对话框的搜索过滤判据（hub::S() 自报状态）
#include "ui/imgui/pages/MediaViewer.h"  // 查看器浮层：开态读数与产品自己的矩形
#include "ui/imgui/pages/Shell_Layout.h"
#include "ui/imgui/verify/Capture.h"
#include "ui/imgui/verify/ReviewHotspot.h"

#include <imgui.h>

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace shine::imguiverify::detail {

// 浮层点击探针。六组：设置模态（开 / 关 / 底下点不动 ×2）、项目中心对话框
// （能打开、打开后里面控件收得到鼠标）、命令面板（点外面关 / 不许同帧自毁）、
// 主题菜单（真实按钮打开 / 点第二项切主题并关闭 / 点外面关闭）、
// 查看器浮层（× / 点遮罩关 / 底下点不动）、查看器页面入口（派生链节点 / 基线图 /
// 出图页结果页签那张成图）。
//
// 分母从设置模态的 4 条起步，其余探针运行时各自计入。
void RunOverlayProbes(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const std::filesystem::path& manifest = session.manifest;

    int overlayClicksPassed = 0;
    int overlayClickTotal = 4;
    // 失败分项：**「哪一条 + 为什么」**。
    //
    // ⚠️ 原来 manifest 只有 `overlay-clicks=7/7` 一个数，红了以后从 manifest 上
    //    完全看不出是哪两条、也看不出原因，只能去翻 `.stdout.txt` 的日志 ——
    //    而 `run_reviews.ps1` 读的正是 manifest。`hover-probes` 至少给了
    //    `broken=` / `unstable=` 的分档，两者不对等（见 PROGRESS「真缺陷」）。
    std::vector<std::string> overlayFailures;
    const auto noteFailure = [&overlayFailures](const std::string& name, const std::string& why) {
        overlayFailures.push_back(name + "：" + why);
    };

    // 浮层（设置模态 / 报告模态 / 命令面板 / 主题菜单）的 item 是在**根窗口**里提交的，
    // 而工作区是 `BeginChild`。ImGui 定 `g.HoveredWindow` 是「从 `g.Windows` 末尾往前
    // 扫、取第一个命中」（imgui.cpp:6573 / 6607），`ItemHoverable` 第一句又判
    // `if (g.HoveredWindow != window) return false;`（:5156）—— 只要鼠标在工作区
    // 范围内，浮层按钮恒 hovered=false / clicked=false。
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
                sawMouse = sawMouse || (std::abs(sawX - target.x) < 1.0f &&
                                        std::abs(sawY - target.y) < 1.0f);
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
                // ⚠️ 这里必须打**一位小数**。原来用 static_cast<int>，于是目标落在
                //    277.5、注入读到 277.0 时两条消息都印成「277」，输出一条
                //    **自相矛盾**的失败（要 277、看到 277，却说没到位）——
                //    而我第一反应是「判据在骗我」，实际是容差 0.5 卡在边界上。
                //    报错信息自相矛盾时，先怀疑信息本身：它比报错更可能是错的。
                why = "位置注入没到位（要 " + std::to_string(target.x) + "," +
                      std::to_string(target.y) + "，帧内看到 " + std::to_string(sawX) + "," +
                      std::to_string(sawY) + "）—— 判据不可用，不是产品的缺陷";
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
                noteFailure(oc.name, why);
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
    // 没了」。修法是 `HubState::dismissArmed`（见 ProjectHub.cpp）。
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
        // ⚠️ 用 `SetHubOpen(true)` 而不是 `ToggleProjectHub()`：项目中心现在是**启动
        //    默认开着**的那一屏（webui `store.jsx:13`），一 Toggle 就是**关**掉它，
        //    于是这条判据会以「项目中心开不起来」的样子红 —— 而它本该验的是
        //    「打开项目对话框里的控件收得到鼠标」。开态要**要求**，不要翻转。
        shell.SetHubOpen(true);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        if (!shell.projectHubOpen()) {
            why = "项目中心开不起来（SetHubOpen(true) 之后 projectHubOpen() 仍是 false）";
        } else {
            const ImVec2 d = ImGui::GetIO().DisplaySize;
            // 第 1 步：全屏扫，找 hub-open 的坐标（不猜）。
            ImVec2 openAt{0.0f, 0.0f};
            const bool foundOpen = FindHotspot(host, shell, "hub-open", openAt);
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
                //    「压根没开」的状态下去扫一片**没有遮罩**的界面，照常扫到
                //    `hub-*`，于是报通过 —— 而它本该抓的缺陷（遮罩抢走
                //    HoveredId）恰恰只在「开着」时才发生。
                //    判据自检时我正是这么被骗的：把遮罩改回抢热区，它仍然 5/5。
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
        ++overlayClickTotal;
        if (hubOk) {
            ++overlayClicksPassed;
        } else {
            shine::log::Error("review: 项目中心按钮判据失败：{}", why);
            noteFailure("项目中心对话框内控件可点", why);
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
    // ---- 打开项目对话框：搜索框要**真的**过滤（打字 → 列表跟着变）----
    //
    // 抓的是「搜索框画得出来、过滤没接线」那一类。两头读数都在产品自己手里：
    // 搜索框写 `hub.openQuery`、列表读 `hub.openShown`（Hub.h），判据直接读它们
    // （与 `settingsOpen()` 同一模式，不另发明一份平行定义）。
    // 打字走 Host 的字符注入（`SetFrameTextOverride` → `AddInputCharactersUTF8`，
    // 与真实 WM_CHAR 同一条路）：先点中搜索框拿键盘焦点、泵过激活帧、再注入字符
    // —— InputText **激活当帧会丢弃字符队列**（Shell_Palette 的 firstText 注释记过）。
    {
        bool ok = true;
        std::string why;
        shell.SetHubOpen(true);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        pages::hub::HubState& hs = pages::hub::S();
        // 前置动作自己带：正常回归不跑 RunUiSmoke（Review.cpp:247 只认
        // SHINE_IMGUI_REVIEW_SMOKE），取证沙箱的最近索引**本来就是空的** ——
        // 判据要有行可滤，自己建一个最小项目登记进去（Create 内部 TouchRecent
        // 落盘索引）。空索引下报「前置不成立」不等于通过：那等于这条判据永远不跑。
        if (hs.cards.empty()) {
            project::ProjectService svc;
            project::ProjectSpec spec;
            spec.name = "UI-OpenSearch";
            spec.rootDir = session.outputDir / "_openprobe";
            spec.templateId = "blank";
            spec.premise = "打开项目搜索过滤判据用";
            if (const std::expected<project::ProjectRef, project::Error> made = svc.Create(spec);
                made) {
                pages::hub::Refresh(hs);
            }
        }
        if (hs.cards.empty()) {
            ok = false;
            why = "自建项目后最近列表仍为空 —— Create/TouchRecent/Refresh 这条链断了";
        } else {
            hs.openDlg = true;
            hs.openQuery.clear();
            host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
            const std::string name = hs.cards[0].entry.name;
            // 点中搜索框拿键盘焦点。几何取产品自己那份 openSearchRect ——
            // 判据里复算 660×484 的布局，一改就打在不存在的位置上还报通过。
            const kit::Rect sr = hs.openSearchRect;
            const auto clickAt = [&](float cx, float cy) {
                host.SetFrameMouseOverride(cx, cy);
                host.SetFrameMouseButtonOverride(true);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                // ⚠️ 释放沿必须在覆盖还开着的那一帧送出去（长注释见命令面板那组探针）。
                host.SetFrameMouseButtonOverride(false);
                host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
                host.ClearFrameMouseButtonOverride();
                host.ClearFrameMouseOverride();
            };
            const float midX = (sr.min.x + sr.max.x) * 0.5f;
            const float midY = (sr.min.y + sr.max.y) * 0.5f;
            clickAt(midX, midY);
            // ① 打一个不可能命中的词：必须进搜索框（openQuery == 注入文本）、且列表被清空。
            host.SetFrameTextOverride("zzz-no-such-project-9527");
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            host.ClearFrameTextOverride();
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            if (hs.openQuery != "zzz-no-such-project-9527") {
                ok = false;
                why = "字符注入没进搜索框（openQuery 是「" + hs.openQuery +
                      "」）—— 输入框没拿到键盘焦点或注入没到位，判据/探针问题，不是过滤逻辑";
            } else if (!hs.openShown.empty()) {
                ok = false;
                why = "过滤没生效：搜「zzz-no-such-project-9527」还剩 " +
                      std::to_string(hs.openShown.size()) + " 行";
            } else {
                // ② 打项目全名：必须至少命中它自己（openShown 含下标 0）。
                //
                // ⚠️ 换词之前必须先让输入框**失活**：InputText 激活期间编辑态在
                //    ImGui 自己手里，外部改 openQuery 会被它原样写回来 ——
                //    实测外部 clear 后再注入，两段文字被拼接成
                //    「zzz-no-such-project-9527UI-OpenSearch」。
                //    失活的落点取搜索框上方的标题区（列表区会误开项目）。
                clickAt(midX, sr.min.y - 24.0f);
                hs.openQuery.clear();
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                clickAt(midX, midY); // 重新激活（激活帧会丢字符队列，所以先激活后打字）
                host.SetFrameTextOverride(name);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                host.ClearFrameTextOverride();
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                bool hitSelf = false;
                for (const int idx : hs.openShown) {
                    hitSelf = hitSelf || idx == 0;
                }
                if (hs.openQuery != name) {
                    ok = false;
                    why = "第二次字符注入没进搜索框（openQuery 是「" + hs.openQuery + "」）";
                } else if (!hitSelf) {
                    ok = false;
                    why = "搜项目全名「" + name + "」却没命中它自己 —— 匹配逻辑坏了";
                }
            }
            // 先关框（输入框随之失活、不再写回），再清词。
            hs.openDlg = false;
            host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
            hs.openQuery.clear();
        }
        ++overlayClickTotal;
        if (ok) {
            ++overlayClicksPassed;
        } else {
            shine::log::Error("review: 打开项目对话框判据「搜索过滤」失败：{}", why);
            noteFailure("打开项目对话框搜索过滤", why);
        }
        // 复位：按语义显式复位。
        shell.SetHubOpen(false);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
    }
    // ---- 命令面板：点外面关闭，且**不许同帧自毁**----
    //
    // 这两条对应本轮新加的行为。缺判据的新行为等于没实现 —— 下面第 ② 条就是
    // 「点外面关闭」最可能踩的坑，而它在**只跑截图**的轮次里完全看不出来
    // （面板开出来又当场关掉，截图序列里只多一张正常画面）。
    {
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        // 面板矩形：x 居中 ±280、y 从 0.22h 起共 420（见 DrawCommandPalette）。
        // 判据不重算它来做「面板外」判定 —— 只挑一个**明显**在下面的点，
        // 1600×960 下是 (800, 883)，而面板下沿在 y≈631。
        const ImVec2 outside{d.x * 0.5f, d.y * 0.92f};
        struct PaletteProbe {
            const char* name;
            bool openByClick;  // false = 判「点外面会关」，true = 判「点开不许自毁」
        };
        const PaletteProbe paletteProbes[] = {
            {"命令面板 点遮罩关闭", false},
            {"点顶栏搜索框打开命令面板（不许同帧自己关掉）", true},
        };
        for (const PaletteProbe& pp : paletteProbes) {
            bool ok = true;
            std::string why;
            // 复位到「面板关着」的干净态（按语义复位，不是「再按一次」）。
            if (shell.commandPaletteOpen()) {
                shell.SetCommandPaletteOpen(false);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            }
            if (pp.openByClick) {
                // ② **同帧开 + 同帧点外面 ⇒ 仍必须开着**。
                //
                // 本条要抓的缺陷是「点外面关闭」缺少「本帧之前就已打开」的前置
                // 条件。真实触发路径是**点顶栏搜索框**打开面板，而那一次点击的
                // 位置就在面板之外。
                //
                // ⚠️ 这里**不扫顶栏找 tb-search**，改用 `SetCommandPaletteOpen(true)`
                //    走同一个 `ToggleCommandPalette()`（它同样置 `paletteJustOpened_`），
                //    再在**同一帧**注入一次「面板外」的点击。验的是同一个守卫，
                //    但不依赖坐标 —— 而坐标扫描这条路现在**走不通**，见下面的注释。
                //
                // 先验前置条件：面板真的开着，且这一帧确实是「刚打开」的那一帧。
                shell.SetCommandPaletteOpen(true);
                if (!shell.commandPaletteOpen()) {
                    ok = false;
                    why = "命令面板开不起来，前置条件不成立";
                } else {
                    host.SetFrameMouseOverride(outside.x, outside.y);
                    host.SetFrameMouseButtonOverride(true);
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    // ⚠️ **释放沿要走一帧之后再清覆盖**（两处命令面板探针原来都是
                    //    先 `Clear` 再泵帧）：`ClearFrameMouseButtonOverride()` 一调，
                    //    宿主那一帧就不再把 `AddMouseButtonEvent(0,false)` 推给 ImGui
                    //    （Host.cpp:352 有 Set 才推），于是**释放事件永远没发生**，
                    //    `io.MouseDown[0]` 从此恒为 true。后果不是这两条探针红，而是
                    //    **后面第一条按下的探针**红：`IsMouseClicked` 要求
                    //    `MouseDownDuration == 0`（即上一帧是抬起的），上一帧却还是按下的
                    //    ⇒ 那一次点击静默不生效。实测是查看器的 × 探针 ——
                    //    帧内读数 `down=1 clicked=0`，而 hover 明明落在按钮上。
                    host.SetFrameMouseButtonOverride(false);
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    host.ClearFrameMouseButtonOverride();
                    host.ClearFrameMouseOverride();
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    if (!shell.commandPaletteOpen()) {
                        ok = false;
                        why = "面板在**打开的那一帧**就被同一次点击关掉了（同帧自毁）——"
                              "「点外面关闭」缺少「本帧之前就已打开」的前置条件";
                    }
                }
            } else {
                // ① 点面板外关闭。开着是**前置条件**：没开就点外面，判据会在一个
                //    「压根没有遮罩」的状态下量一条不存在的路径，然后报通过。
                shell.SetCommandPaletteOpen(true);
                host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
                if (!shell.commandPaletteOpen()) {
                    ok = false;
                    why = "命令面板开不起来，前置条件不成立";
                } else {
                    host.SetFrameMouseOverride(outside.x, outside.y);
                    host.SetFrameMouseButtonOverride(true);
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    // ⚠️ 同上面那条：释放沿必须在覆盖还开着的那一帧送出去（见上方长注释）。
                    host.SetFrameMouseButtonOverride(false);
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    host.ClearFrameMouseButtonOverride();
                    host.ClearFrameMouseOverride();
                    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                    if (shell.commandPaletteOpen()) {
                        ok = false;
                        why = "点了面板之外，面板还开着（点" +
                              std::to_string(static_cast<int>(outside.x)) + "," +
                              std::to_string(static_cast<int>(outside.y)) + "）";
                    }
                }
            }
            ++overlayClickTotal;
            if (ok) {
                ++overlayClicksPassed;
            } else {
                shine::log::Error("review: 命令面板判据「{}」失败：{}", pp.name, why);
                noteFailure(pp.name, why);
            }
            if (shell.commandPaletteOpen()) {
                shell.SetCommandPaletteOpen(false);
            }
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        }
    }
    // ---- 主题菜单：实际点击顶栏按钮和菜单项，读取产品开态与当前主题 ----
    {
        const auto drawFrame = [&shell](float dt) { shell.DrawFrame(dt); };
        const auto click = [&](ImVec2 at) {
            host.SetFrameMouseOverride(at.x, at.y);
            host.PumpFrames(1, drawFrame);
            host.SetFrameMouseButtonOverride(true);
            host.PumpFrames(1, drawFrame);
            host.SetFrameMouseButtonOverride(false);
            host.PumpFrames(1, drawFrame);
            host.ClearFrameMouseButtonOverride();
            host.ClearFrameMouseOverride();
        };
        const auto closeIfOpen = [&] {
            if (shell.themeMenuOpen()) {
                host.SetFrameKeyOverride(ImGuiKey_Escape, true);
                host.PumpFrames(1, drawFrame);
                host.SetFrameKeyOverride(ImGuiKey_Escape, false);
                host.PumpFrames(1, drawFrame);
                host.ClearFrameKeyOverride();
            }
        };
        const auto record = [&](const char* name, const char* failure) {
            ++overlayClickTotal;
            if (failure == nullptr) {
                ++overlayClicksPassed;
            } else {
                shine::log::Error("review: 主题菜单判据「{}」失败：{}", name, failure);
                noteFailure(name, failure);
            }
        };

        const theme::ThemeId savedTheme = theme::CurrentThemeId();
        if (savedTheme == theme::ThemeId::Dusk) {
            shell.SetTheme(theme::ThemeId::PaperInk);
            host.PumpFrames(1, drawFrame);
        }
        const auto openMenu = [&]() -> const char* {
            ImVec2 trigger{};
            const float topBarCenter = pages::kTopBarHeight * 0.5f;
            if (!FindHotspot(host, shell, "tb-theme", trigger, 12.0f, 0.0f, topBarCenter,
                             -1.0f, topBarCenter)) {
                return "找不到顶栏主题按钮（tb-theme）";
            }
            click(trigger);
            return shell.themeMenuOpen() ? nullptr
                                         : "点击结束的帧菜单未打开，检查按钮动作及同帧关闭闸门";
        };
        const char* openFailure = openMenu();
        record("点顶栏主题按钮打开菜单", openFailure);

        const char* rowFailure = nullptr;
        if (!shell.themeMenuOpen()) {
            rowFailure = "菜单未打开，无法点击第二项";
        } else {
            const kit::Rect panel = shell.ThemeMenuRect();
            ImVec2 duskRow{};
            // 下标 0 是分组标题，第二项薄暮的热区下标为 2。
            if (!FindHotspot(host, shell, "theme-menu##2", duskRow, 12.0f, panel.min.x,
                             panel.min.y, panel.max.x, panel.max.y)) {
                rowFailure = "找不到薄暮的热区（theme-menu##2）";
            } else {
                click(duskRow);
                if (theme::CurrentThemeId() != theme::ThemeId::Dusk || shell.themeMenuOpen()) {
                    rowFailure = "点击后主题未切到薄暮，或菜单仍开着";
                }
            }
        }
        record("点薄暮切换主题并关闭菜单", rowFailure);

        closeIfOpen();
        const char* outsideFailure = openMenu();
        if (outsideFailure == nullptr) {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const ImVec2 outside{display.x * 0.5f, display.y * 0.5f};
            const theme::ThemeId beforeDismiss = theme::CurrentThemeId();
            if (shell.ThemeMenuRect().contains(outside)) {
                outsideFailure = "测试落点位于面板内";
            } else {
                click(outside);
                if (shell.themeMenuOpen() || theme::CurrentThemeId() != beforeDismiss) {
                    outsideFailure = "点击面板外后菜单仍开着，或主题发生变化";
                }
            }
        }
        record("点主题菜单外关闭", outsideFailure);
        closeIfOpen();
        if (theme::CurrentThemeId() != savedTheme) {
            shell.SetTheme(savedTheme);
        }
        host.PumpFrames(1, drawFrame);
    }
    // ---- 查看器浮层：里面那些按钮收得到鼠标吗（2026-09-30 补）----
    //
    // 与设置模态那三条**同构**，但打的是另一个浮层 —— 而且它比设置模态更容易坏：
    // 查看器画在帧尾（最上面那层）、按钮又在**根窗口**里注册，只要
    // `OverlayBlocksMouse()` / `chromeInteractive_` 漏了它，工作区 child 与 chrome
    // 就会先把这次点击吃掉。症状与设置模态那次一模一样：**按钮照画不误、点不动**，
    // 而 80 张静息截图与 hover 探针对它零覆盖。
    //
    // 矩形一律用**产品自己那份**（`MediaViewerCloseRect()`）：在判据里照
    // 720/min(78vh,700) 重算一遍，布局一改就会打在一个已经不存在的位置上，而它报的
    // 仍然是「通过」—— `SettingsCloseRect()` 那次分叉就是这么来的。
    {
        struct ViewerProbe {
            const char* name;
            int key;  // 0 = × 按钮，1 = 点遮罩关闭，2 = 验「底下点不动」
        };
        const ViewerProbe viewerProbes[] = {
            {"查看器 ×", 0},
            {"查看器 点遮罩关闭", 1},
            {"查看器开着时点导航栏（不许穿透去切工作区）", 2},
        };
        for (const ViewerProbe& vp : viewerProbes) {
            // 前置条件：查看器要真的开起来。请求来自**资产快照**、走页面同一条
            // `AssetViewerRequest`（第 1 页 = 派生链「正脸」那一层）。
            pages::OpenMediaViewer(pages::AssetViewerRequest(0), /*initialIndex=*/1);
            host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
            const bool opened = pages::MediaViewerOpen();
            const int wsBefore = shell.workspace();
            const ImVec2 d = ImGui::GetIO().DisplaySize;
            ImVec2 target{0.0f, 0.0f};
            switch (vp.key) {
            case 0: {
                const kit::Rect r = pages::MediaViewerCloseRect();
                target = ImVec2((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f);
                break;
            }
            case 1: {
                const kit::Rect r = pages::MediaViewerRect();
                // 面板正下方偏左：确定落在遮罩里、且不在面板矩形内。
                target = ImVec2(r.center().x - r.width() * 0.5f - 60.0f, d.y * 0.5f + 300.0f);
                break;
            }
            default:
                // 导航栏第 3 格（小说）：它注册在根窗口里、且排在浮层之前。
                target = ImVec2(24.0f, 128.0f);
                break;
            }
            bool sawMouse = false;
            float sawX = 0.0f;
            float sawY = 0.0f;
            // 帧内诊断：这一帧到底哪个 item 拿到了 hover。判据红了以后「点不动」的常见
            // 根因是**别处**的 item 先占了 `HoveredId`（同窗口先注册者独占）、或者占了
            // `ActiveId`（一次没被释放的按下会让后面所有 item 都收不到点击）。
            // 只报一句「没收到点击」，看的人无从下手 —— 上一轮就是这么在浮层按钮上
            // 转了半天才定位到宿主层的。
            std::string hoveredAt;
            // 注入侧的两个原始读数。`IsItemClicked()` = `IsMouseClicked(0) && IsItemHovered()`，
            // 两头要分开看：`down=0` 是**注入没到**（按下沿没被 NewFrame 折出来），
            // `clicked=1` 而 hover 也在按钮上、却仍没关 —— 那才是产品侧的问题。
            // 这两个数在帧回调里读：`io.MouseClicked` 是**每帧**的边沿信号，出了那一帧就没了。
            bool sawDown = false;
            bool sawClicked = false;
            bool openAfterPress = true;
            bool openAfterRelease = true;
            bool ok = true;
            std::string why;
            if (!opened) {
                // 前置条件不成立时**不能**继续往下点：那会在一片没有遮罩的界面上量一条
                // 不存在的路径，然后报通过（项目中心那条判据的注释里记过这个坑）。
                ok = false;
                why = "查看器开不起来（OpenMediaViewer 之后 MediaViewerOpen() 仍是 false）—— "
                      "判据前置不成立";
            } else {
                host.SetFrameMouseOverride(target.x, target.y);
                host.SetFrameMouseButtonOverride(true);
                // 只数**这一帧**的 hover：上一帧的残留会让诊断指错 item。
                kit::ResetHoveredItemCount();
                host.PumpFrames(1, [&](float dt) {
                    shell.DrawFrame(dt);
                    sawX = ImGui::GetIO().MousePos.x;
                    sawY = ImGui::GetIO().MousePos.y;
                    sawDown = ImGui::GetIO().MouseDown[0];
                    sawClicked = ImGui::GetIO().MouseClicked[0];
                    sawMouse = sawMouse || (std::abs(sawX - target.x) < 1.0f &&
                                            std::abs(sawY - target.y) < 1.0f);
                });
                hoveredAt = kit::LastHoveredItem();
                openAfterPress = pages::MediaViewerOpen();
                host.SetFrameMouseButtonOverride(false);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                openAfterRelease = pages::MediaViewerOpen();
                host.ClearFrameMouseButtonOverride();
                host.ClearFrameMouseOverride();
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                WriteManifest(manifest, std::string("viewer-probe=") + vp.name + " down=" +
                                            (sawDown ? "1" : "0") + " clicked=" +
                                            (sawClicked ? "1" : "0") + " hover=" + hoveredAt +
                                            " open: after-press=" +
                                            (openAfterPress ? "1" : "0") + " after-release=" +
                                            (openAfterRelease ? "1" : "0"));

                if (!sawMouse) {
                    ok = false;
                    why = "位置注入没到位（要 " + std::to_string(target.x) + "," +
                          std::to_string(target.y) + "，帧内看到 " + std::to_string(sawX) + "," +
                          std::to_string(sawY) + "）—— 判据不可用，不是产品的缺陷";
                } else if (vp.key == 2) {
                    // 只要求「底下点不动」=工作区索引没变（**不**顺带要求查看器还开着，
                    // 关闭是遮罩的既定行为）。
                    if (shell.workspace() != wsBefore) {
                        ok = false;
                        why = "点击穿透到了底下：workspace " + std::to_string(wsBefore) + " → " +
                              std::to_string(shell.workspace()) +
                              "（查看器开着时底下不该点得动）";
                    }
                } else if (pages::MediaViewerOpen()) {
                    ok = false;
                    const kit::Rect closeR = pages::MediaViewerCloseRect();
                    why = "点了却没关掉（查看器仍开着）—— 目标 " + std::to_string(target.x) + "," +
                          std::to_string(target.y) + "（产品给的 × 矩形 = " +
                          std::to_string(closeR.min.x) + "," + std::to_string(closeR.min.y) + " " +
                          std::to_string(closeR.max.x) + "," + std::to_string(closeR.max.y) +
                          "）；这一帧的 hover 落在「" +
                          (hoveredAt.empty() ? std::string("(没有任何 kit 控件)") : hoveredAt) +
                          "」上 ⇒ 查看器的按钮没拿到 HoveredId（同窗口先注册者独占 / "
                          "HoveredWindow 被别的 child 拿走 / ActiveId 被别处占着）";
                }
            }
            ++overlayClickTotal;
            if (ok) {
                ++overlayClicksPassed;
            } else {
                shine::log::Error("review: 浮层交互探针「{}」失败：{}", vp.name, why);
                noteFailure(vp.name, why);
            }
            // 复位：按语义复位（关掉查看器），别让后面的截图都拍在浮层下面。
            pages::CloseMediaViewer();
            if (shell.workspace() != wsBefore) {
                shell.SetWorkspace(wsBefore);
            }
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        }
    }

    // ---- 页面入口 → 查看器：点链上第 1 格 / 点基线图 / 点出图页成图，要落在**预期的页**上 ----
    //
    // 这一条挡的是「入口接错了字段」那一类：`OpenMediaViewer(req, initialIndex)` 的
    // 第二个实参写错（或请求构造器自己收了个**被忽略**的参数）时，查看器照样开得出来、
    // 截图也有内容 —— 只是**页不对**。本轮真踩过一次：`AssetViewerRequest(0, 1)` 的那个
    // 1 落进了被 `(void)` 掉的形参里，于是「点第 2 层」打开的是第 1 页，而两条语句
    // 各自看起来都对（判据是拿纯色像素数的，颜色当场对不上才发现）。
    //
    // 坐标不猜：`FindHotspot` 全屏扫出控件**现在**在哪（与项目中心那条判据同一条纪律）。
    // 顺带它还把「入口有没有注册热区」验了 —— 派生链的节点原来是 `node.contains(MousePos)`
    // 的手算 hover，不注册热区（画得出、点不动）。
    {
        const int wsBefore = shell.workspace();
        // ⚠️ 每个入口的**前置动作自己带**（工作区 + 该页的视图态），不靠上一条留下的残留
        //    状态：资产页有**两个视图**，而那两个入口只存在于**详情**里；悬停探针
        //    （`hover-asset-card` 的 `assetsOverview = true` 前置）跑完把这一页留在总览态，
        //    而它**不会还原** —— 少设这一行，扫描扫到的是总览网格（`asset-card-*`），
        //    两个入口一个都不在，判据报「入口没注册热区」，指的方向完全错。
        struct PageEntry {
            const char* id;
            const char* label;
            int expectIndex;
            int workspace;  // 入口所在的工作区
            int panelTab;   // 出图页：-1 = 不是出图页；>=0 = 先切到该页签
        };
        const PageEntry pageEntries[] = {
            {"asset-chain-0", "派生链第 1 格 → 查看该层大图", 1, kWsAssets, -1},
            {"asset-base-art", "详情页基线图 → 查看大图", 0, kWsAssets, -1},
            // 出图页「结果」页签那张成图：画廊 = **本章各镜**（页序与镜序同序），
            // 所以「落在他那一页」= 画廊下标 = 当前选中的镜下标。前置动作写全：
            // 切到结果页签 + 显式选第 2 镜（不靠残留选中态，否则期望值会随顺手改动的
            // 上一条探针漂移，而那正是这条判据要挡的东西）。
            {"if-result-hero", "出图页结果成图 → 查看该镜大图", 1, kWsImageFlow, 3},
        };
        for (const PageEntry& entry : pageEntries) {
            pages::CloseMediaViewer();
            shell.SetWorkspace(entry.workspace);
            if (entry.workspace == kWsAssets) {
                shell.SetAssetsOverview(false);
            } else if (entry.panelTab >= 0) {
                shell.SetImageFlowPanel(entry.panelTab);
                pages::SelectBookShot(entry.expectIndex);
            }
            host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
            ImVec2 at{0.0f, 0.0f};
            bool ok = true;
            std::string why;
            // 只扫工作区那一片：全屏扫要上万次泵帧，而这两个入口一定在正文里。
            if (!FindHotspot(host, shell, entry.id, at, /*step=*/16.0f, 300.0f, 80.0f, 1320.0f,
                             744.0f)) {
                ok = false;
                why = std::string("全屏扫不到「") + entry.id +
                      "」—— 入口没注册热区（画得出来、点不动），或者那个控件没画";
            } else {
                host.SetFrameMouseOverride(at.x, at.y);
                host.SetFrameMouseButtonOverride(true);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                host.SetFrameMouseButtonOverride(false);
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                host.ClearFrameMouseButtonOverride();
                host.ClearFrameMouseOverride();
                host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
                if (!pages::MediaViewerOpen()) {
                    ok = false;
                    // ⚠️ 这里也要分开报：入口**点开了但同帧被自己关掉**（缺
                    //    `openFrame` 闸门）与「压根没开」在截图上都是「没有查看器」。
                    why = std::string("点了「") + entry.id + "」（" + std::to_string(at.x) + "," +
                          std::to_string(at.y) + "）查看器没开着 —— 入口是空动作，或者"
                          "它开的那一帧又被「点外面关闭」关掉了（缺同帧闸门）";
                } else if (pages::MediaViewerIndex() != entry.expectIndex) {
                    ok = false;
                    why = std::string("查看器打开的页不对：点「") + entry.id + "」期望第 " +
                          std::to_string(entry.expectIndex + 1) + " 页，实际第 " +
                          std::to_string(pages::MediaViewerIndex() + 1) +
                          " 页 —— 「点第 N 格看第 N 层」这条接线断了";
                }
            }
            ++overlayClickTotal;
            if (ok) {
                ++overlayClicksPassed;
            } else {
                shine::log::Error("review: 浮层交互探针「{}」失败：{}", entry.label, why);
                noteFailure(entry.label, why);
            }
            pages::CloseMediaViewer();
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        }
        if (shell.workspace() != wsBefore) {
            shell.SetWorkspace(wsBefore);
        }
        // 把这一族动过的**页面态**还原：出图页的页签与选中镜。不还原的话，后面那些
        // 抓图拍到的就是「结果页签 + 第 2 镜」—— 与前面同一页的那几张差在一个没人
        // 声明的状态上，读图的人会以为看到的是默认态。
        shell.SetImageFlowPanel(0);
        shell.SetAssetsOverview(false);
        pages::SelectBookShot(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
    }

    session.overlayClicksPassed = overlayClicksPassed;
    session.overlayClickTotal = overlayClickTotal;
    // ⚠️ manifest 行**必须写在两条浮层判据都跑完之后**：早先写在项目中心那条
    //    之前，于是它报 4/4 而 kOverlayClickTotal 已经是 5 —— 判据自己报的
    //    分母与实际条数不一致，读者会以为 5 条里过了 4 条。
    // 与 `hover-probes` 的写法对齐：计数行带 `broken=N` 后缀，失败时**另起一行**列出
    // 「哪一条 + 为什么」。只报一个 `7/7` 的坏处是：红了以后读 manifest 的人
    // 既不知道是哪两条、也不知道原因，只能去翻 `.stdout.txt` —— 而脚本读的是 manifest。
    std::string overlayLine = std::string("overlay-clicks=") +
                              std::to_string(session.overlayClicksPassed) + "/" +
                              std::to_string(session.overlayClickTotal);
    if (!overlayFailures.empty()) {
        overlayLine += "  broken=" + std::to_string(overlayFailures.size());
    }
    WriteManifest(manifest, overlayLine);
    if (!overlayFailures.empty()) {
        std::string detail;
        for (std::size_t i = 0; i < overlayFailures.size(); ++i) {
            if (i > 0) {
                detail += " | ";
            }
            detail += overlayFailures[i];
        }
        WriteManifest(manifest, "overlay-clicks-failed=" + detail);
    }
}

} // namespace shine::imguiverify::detail

