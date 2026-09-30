#include "ui/imgui/verify/ReviewOverlayProbe.h"

#include "core/Log.h"
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/verify/Capture.h"
#include "ui/imgui/verify/ReviewHotspot.h"

#include <imgui.h>

#include <cmath>
#include <set>
#include <string>

namespace shine::imguiverify::detail {

// 浮层点击探针。三组：设置模态（开 / 关 / 底下点不动 ×2）、项目中心对话框
// （能打开、打开后里面控件收得到鼠标）、命令面板（点外面关 / 不许同帧自毁）。
//
// ⚠️ `total` 从 4 起步（设置模态那 4 条），项目中心 +1、命令面板 +2，最后是 7。
//    它是自增的而不是写死的 7：写死的话加探针时容易忘了改判据，判据会拿着旧的
//    分母与实际条数比，报告里出现「4/4 而实际有 7 条」这种自相矛盾的行。
void RunOverlayProbes(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const std::filesystem::path& manifest = session.manifest;

    int overlayClicksPassed = 0;
    int overlayClickTotal = 4;

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
        shell.ToggleProjectHub();
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        if (!shell.projectHubOpen()) {
            why = "项目中心开不起来（ToggleProjectHub 之后 projectHubOpen() 仍是 false）";
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
                    host.SetFrameMouseButtonOverride(false);
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
                    host.SetFrameMouseButtonOverride(false);
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
            }
            if (shell.commandPaletteOpen()) {
                shell.SetCommandPaletteOpen(false);
            }
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        }
    }
    session.overlayClicksPassed = overlayClicksPassed;
    session.overlayClickTotal = overlayClickTotal;
    // ⚠️ manifest 行**必须写在两条浮层判据都跑完之后**：早先写在项目中心那条
    //    之前，于是它报 4/4 而 kOverlayClickTotal 已经是 5 —— 判据自己报的
    //    分母与实际条数不一致，读者会以为 5 条里过了 4 条。
    WriteManifest(manifest, std::string("overlay-clicks=") +
                                std::to_string(session.overlayClicksPassed) + "/" +
                                std::to_string(session.overlayClickTotal));
}

} // namespace shine::imguiverify::detail
