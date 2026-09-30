#pragma once
// shine::pages —— 页面层弹窗外壳（ModalFrameRect 之上的三件事）
//
// 为什么提成组件：项目中心有三个对话框（新建向导 / 打开 / 确认），而**弹窗本身**
// 的三段逻辑原先是 ProjectHub.cpp 里的三份私有实现：
//   ① 外壳 + 「点面板外关闭」  ② 页脚按钮右对齐排布  ③ 页脚左侧提示文字
// 三处各自抄一遍的后果不是「重复」，是**三份会各自漂移的几何** —— 头高、页脚高、
// 按钮间距、提示文字的 Y 只要有一处抄错半像素，就出现「向导的按钮比确认框低 1px」。
//
// ⚠️ 本组件**不含**遮罩与面板框的绘制：那是 kit::ModalFrameRect 的活，且它已经
//    是全仓唯一一份（Shell 的设置模态 / 报告模态 / 本页三个对话框共用）。
//    这里只做「拿到 mf 之后页面自己要管的那部分」。
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::pages {

struct ModalBox {
    kit::Rect frame;
    kit::Rect header;
    kit::Rect body;
    kit::Rect footer;
};

// 弹窗外壳。几何与 kit::Modal 同源（全部由 kit::ModalFrameRect 算）。
//
// `dismiss` 出参：点遮罩（面板外）时置 true，由**调用方**关掉自己的状态。
// 为什么是出参而不是在函数里直接改状态：三个对话框关的是三个不同的字段
// （wizard / openDlg / confirm），让外壳去认它们等于把状态所有权搅在一起。
// `dismiss` 传 nullptr = 不判「点外面关闭」。
ModalBox DrawModal(ImDrawList* draw, kit::Rect area, std::string_view title, std::string_view icon,
                   float width, float height, bool* dismiss, int footerButtons = 2);

struct ModalAction {
    std::string_view label;
    kit::ButtonSpec spec;
    std::string_view id;
};

// 页脚按钮右对齐（.modal-f 是「提示 + spacer + 按钮组」）。
// 返回每个按钮本帧是否被点（下标与 actions 一一对应）。
std::vector<int> DrawModalFooter(ImDrawList* draw, kit::Rect footer,
                                 const std::vector<ModalAction>& actions);

// 页脚左侧提示文字。三个对话框各有一句，位置/字号/Y 完全相同。
//
// ⚠️ 这里**只画不裁剪**（走 AddText 而不是 DrawTextClipped）：原实现就是这样，
//    提示语都是短句，裁剪只会让「长一点的提示」在某一处被切而在别处不被切。
void ModalFooterHint(ImDrawList* draw, kit::Rect footer, std::string_view text);

} // namespace shine::pages
