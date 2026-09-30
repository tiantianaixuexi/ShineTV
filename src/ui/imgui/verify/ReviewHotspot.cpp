#include "ui/imgui/verify/ReviewHotspot.h"

#include "core/Log.h"
#include "ui/imgui/kit/Draw.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace shine::imguiverify::detail {

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
std::set<std::string> ScanHotspots(imguiapp::Host& host, pages::Shell& shell, float x0, float y0,
                                   float x1, float y1, float step, bool verbose) {
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

// 全屏扫一遍，返回**第一个**命中 `id` 的坐标。
//
// 为什么单独一个函数：`ScanHotspots` 只回 id **集合**，而「我要去点它」需要坐标。
// 坐标必须**量**出来 —— 猜出来的坐标（`ImVec2(120, 24)` 之类）在布局一改就静默
// 失效，判据却仍然报「通过」。项目中心的 hub-open 与命令面板的 tb-search 各写过
// 一遍同样的双层循环，收在这里一份。
bool FindHotspot(imguiapp::Host& host, pages::Shell& shell, const char* id, ImVec2& out, float step,
                 float x0, float y0, float x1, float y1) {
    struct Unpin {
        ~Unpin() { kit::UnpinAnimation(); }
    } unpin;
    kit::PinAnimation(kit::Now());
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    if (x1 < 0.0f) {
        x1 = d.x;
    }
    if (y1 < 0.0f) {
        y1 = d.y;
    }
    for (float y = y0; y <= y1; y += step) {
        for (float x = x0; x <= x1; x += step) {
            host.SetFrameMouseOverride(x, y);
            kit::ResetHoveredItemCount();
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            host.ClearFrameMouseOverride();
            const char* got = kit::LastHoveredItem();
            if (kit::HoveredItemCount() > 0 && got != nullptr && std::string(got) == id) {
                out = ImVec2(x, y);
                return true;
            }
        }
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
    }
    return false;
}

// SHINE_SCAN=<ws>:<x0>:<y0>:<x1>:<y1>:<step>：热区扫描，**跑在悬停探针之后**。
//
// ⚠️ 位置很要紧：放在探针前面扫到的是「fixture 还没就绪」的那一帧 ——
//    小说侧栏的「快速跳转」2×2 那一段那时候压根没画，扫出来的结果里
//    根本没有 `jump-*`，而据此去改探针坐标就会一路改错。探针跑完时
//    fixture 已经落地（章节 / 树 / 资产都在），那才是量坐标的正确状态。
//
// 用途：探针报「点空了」时，坐标应当**量**出来而不是猜。诊断工具，
// 不参与 pass/fail（它只打 id，不判像素变化）。
void RunHotspotScanEnv(imguiapp::Host& host, pages::Shell& shell, shine::theme::ThemeId base) {
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
}

} // namespace shine::imguiverify::detail
