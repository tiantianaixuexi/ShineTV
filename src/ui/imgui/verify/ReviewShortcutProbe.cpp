#include "ui/imgui/verify/ReviewShortcutProbe.h"

#include "core/Log.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/verify/Capture.h"

#include <imgui.h>

#include <iterator>
#include <string>

namespace shine::imguiverify::detail {

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
void RunShortcutProbes(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const std::filesystem::path& manifest = session.manifest;

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
    session.shortcutsPassed = shortcutOk;
    session.shortcutTotal = static_cast<int>(std::size(shortcuts));
    WriteManifest(manifest, std::string("shortcuts=") + std::to_string(shortcutOk) + "/" +
                                std::to_string(session.shortcutTotal) +
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
}

} // namespace shine::imguiverify::detail
