#pragma once
// shine::pages —— 页面层**共用**的常量与小工具
//
// 抽这一份的理由：下面这些东西原先是 WorkspaceB.cpp 顶部匿名命名空间里的局部定义，
// 而六个工作区页面**每一个**都要用。住在那一个 .cpp 里就等于「共用逻辑锁在一个
// 恰好很大的文件里」—— 文件一拆就断链。
//
// ⚠️ 刻意**不含**任何页面自己的布局：各页的栅格 / 分栏留在各自的 Page_*.cpp。
//    提上来就是第二份假抽象。收录标准只有一条：**换一页也照样成立**。
#include "ui/imgui/pages/WorkspacePages.h"

#include "ui/imgui/kit/Fmt.h"

#include <cstdio>
#include <string_view>

namespace shine::pages {

// 工作区竖排 gap。design-spec §7 的 .vw 骨架就是「padding 20/24/26 + 竖排 gap16」。
inline constexpr float kGap = 16.0f;
inline constexpr float kFlowStackWidth = 740.0f;

// ShotCode 的**零分配**版：写进调用方的栈缓冲（第二十轮波 3 —— 胶片条等
// 每帧 × 每格的调用点不再造临时 string）。产物与 ShotCode 逐字节一致。
inline std::string_view ShotCodeTo(char* buf, std::size_t cap, int ord) {
    const int n = std::snprintf(buf, cap, "S%03d", ord);
    return std::string_view(buf, n > 0 ? static_cast<std::size_t>(n) : 0u);
}

// 业务层没有这一项时的**诚实**占位（区别于编一个 0 / 写死一个哈希）。
//
// ⚠️ 提到共用层而不是留在某个页面里：出图 / 出片的节点图、资产卡、章节表、连续性
//    条目都拿它表示「这一步没有只读投影」—— 各自写一个字面量的话，
//    日后有人改成别的破折号样式就会分裂。
inline constexpr const char* kDash = "—";

// ⚠️ `kit::Art()` 是**纯程序化绘制**（kit/Views.cpp）：调色板只按 `seed % 12` 取，
//    形状是固定几何，跟 novel.db / Comfy 的任何字段都无关 —— 它画出来的图
//    **不是这个工程的出图结果**，只是一块占位色块。
//
//    所以每一处画了它的地方都必须有一句**可见**标注，否则读者会把占位插画当成
//    真实渲染。`Shell.cpp` 的设计稿区早就标注了同一句（那处的 Art 同样是占位），
//    图评审 / 结果 / 胶片条三处漏了 ——「同一件事有的地方标了有的地方没标」比
//    「都不标」更糟：它让人以为没标的那几张是真的。
//
//    **全仓统一这一句，不各写各的**：措辞一分裂，日后就没人说得清哪句才是标准。
//    需要更短的形式（格子太小放不下）时用 kArtNoticeShort，但要在注释里说明它是
//    同一句话的缩写。
inline constexpr const char* kArtNotice = "示意插画 · 非本工程出图结果";
inline constexpr const char* kArtNoticeShort = "示意";

// 值列占位：空串 → kDash。检查器 / 详情卡 / 各处 KV 的值列共用 —— 空白格分不清
// 是「没值」还是「漏画」，破折号才诚实。（原先是 Shell_Panels.cpp 匿名命名空间里
// 的一份 + Page_Storyboard.cpp 里的一个 lambda，拆文件后提到共用层。）
inline std::string OrDash(std::string_view value) {
    return value.empty() ? std::string(kDash) : std::string(value);
}

// 库主键的显示占位：有 id → "#N"，没有 → kDash。机位 / 光线这类引用列共用。
inline std::string OrDashId(long long id) {
    return id > 0 ? kit::Fmt("#{}", id) : std::string(kDash);
}

// 秒 → 设计稿口径的时长文案（"3.5s"，zmij 定点一位小数）。<=0 → kDash。
//
// 全树共用这一条：故事板详情卡、时间线格子、检查器「时长」三处原来是三种写法
// （%.1f / std::to_string 六位小数 / 原文退回），同一个镜在三个面板上是三个时长。
inline std::string DurationText(double seconds) {
    return seconds > 0.0 ? kit::Fmt("{}s", kit::Fixed(seconds, 1)) : std::string(kDash);
}

// 单行文字的像素宽。页面上「标签左排、控件右排」的位置全靠它量出来。
float LabelWidth(ImFont* font, float size, const char* text);

// 三页共用的**可视底**（绝对屏幕 y）。
//
// Shell 每帧把真实可视高度写进 WorkspaceViewportHeight()，而它给页面的布局区是
// **写死的 2400px** —— 那是「内容超出视口仍能被滚到」的上限，不是可视高度。页面
// 拿 2400 当视口，就会把「常驻可见」的底部元素钉到 y≈2280（时间轴、阶段表脚注），
// 要滚到最底才看得着，而上面全是空白。
//
// 返回 area.max.y = Shell 尚未写入这一帧（单跑页面 / 早于第一帧），调用方据此
// 退回旧行为 —— 逐页迁移，不要求外壳先到齐。
float ViewportBottom(kit::Rect area);

} // namespace shine::pages
