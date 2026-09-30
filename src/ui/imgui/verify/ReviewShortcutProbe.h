#pragma once
// shine::imguiverify::detail —— 快捷键动作判据
//
// 验的是「按下去到底有没有发生什么」。这几轮反复抓到的都是同一类：死模态、
// 死入口、点不动的复选框、写死的数据 —— 而「按了没反应」在**静息态截图**上与
// 「按了」长得一模一样，光靠看图永远抓不到。
//
// 做法：注入按键（必须在 `NewFrame` 之前，见 Host::SetFrameKeyOverride），
// 比对**该动作真正会改的那个状态**前后变没变。
//
// ⚠️ 覆盖面不足与真缺陷在这条判据眼里长得一模一样（注释全文在
//    ReviewShortcutProbe.cpp 的「第七个动作」那一段）。所以状态读数**按语义分四类**，
//    每类读产品自己的读数，不另发明一份平行定义。

#include "ui/imgui/verify/ReviewSession.h"

namespace shine::imguiverify::detail {

// 跑完整轮快捷键判据，并把 `shortcuts=ok/total  dead=…` 写进 manifest。
// 同时填 Session::shortcutsPassed / shortcutTotal。
void RunShortcutProbes(Session& session);

} // namespace shine::imguiverify::detail
