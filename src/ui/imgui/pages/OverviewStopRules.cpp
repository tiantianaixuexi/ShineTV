// 总控页的 **S1–S12 停止条件规则表**（左列整宽）
//
// 单独立一个文件，因为它是全页唯一一块「**不含任何状态、只有规则文本**」的区域，
// 而它恰恰是本轮之前差点被当成缺口删掉的那块（见下面那段注释）。
//
// ⚠️ 它读的是 novelcore 的三个自由函数，与 pipeline::StopPolicy **不是同一套编号**。
//    两处编号撞名这件事是这一段注释存在的唯一理由；把它搬进别的文件时，
//    这段必须跟着走，否则下一个人会照着 pipeline/StopPolicy.cpp 再标一遍 S1。
#include "ui/imgui/pages/Overview.h"

#include "novel/NovelRunLoop.h"

namespace shine::pages {
namespace overview {

using namespace shine::kit;

Rect DrawStopRuleTable(ImDrawList* draw, Rect content, float s12Top) {
    // ---- 停止条件 · S1–S12（`09` §2.2 的真实规则表），**左列整宽** ----
    //
    // ⚠️ 早先这里写着一句「设计稿的 S1–S12 来自 mock.js，`StopPolicy` 只判 5 组、
    //    没有可枚举的规则表接口，所以不硬凑 12 行」。**那个结论是错的**，而且错得
    //    有害：真实的 S 编号就在 `shine::novelcore` 里，是三个公开的自由函数
    //    （`src/novel/NovelRunLoop.h:43-48`）：
    //      StopCode（S1…S12）/ StopCodeName / StopCodeCondition / StopCodeHint
    //    实现在 `NovelRunLoop.cpp:102-138` 逐条落地，阈值（>=2 次、>2×、>=5 次、
    //    >=500 条…）都写在那十二条中文判据里。**零 shine_core 改动。**
    //
    //    错因是**编号撞名**：`src/pipeline/StopPolicy.cpp:7-11` 的字面量也叫
    //    「S1–S4 预算」/ S5 / S6 / S7 / S8，那是另一套 5 组判定。旧实现照着它把
    //    「LLM 调用 0/1000」标成 S1 —— 于是同一个 S1 在领域文档里是「机器校验连续
    //    失败 >= 2 次」，在界面上却是「LLM 调用预算」。**这不是少了个功能，是误导。**
    //
    //    放**左列整宽**而不是右栏：这些判据句子长（「Comfy 不可用且本章需要出图：
    //    探活失败 >= 3 次（间隔 5s）」），塞进 300px 的右栏会被裁掉后半句 ——
    //    规则表被截断等于没写。右栏留给「流水线停止规则」（实时预算状态）。
    constexpr int kStopRuleCount = 12;
    constexpr float kStopRuleRowH = 20.0f;
    const float s12H = 44.0f + 16.0f + kStopRuleRowH * kStopRuleCount + 16.0f;
    const Rect s12{content.min.x, s12Top, content.max.x, s12Top + s12H};
    Rect s12Body = Card(draw, s12, "停止条件 · S1–S12", "alert", false, false);
    float sry = s12Body.min.y;
    for (int i = 1; i <= kStopRuleCount; ++i) {
        const novelcore::StopCode code = static_cast<novelcore::StopCode>(i);
        const std::string codeText(novelcore::StopCodeName(code));
        const std::string_view cond = novelcore::StopCodeCondition(code);
        const std::string_view hint = novelcore::StopCodeHint(code);
        draw->AddText(MonoAt(11.0f), 11.0f, ImVec2(s12Body.min.x, sry + 1.0f), ColorTextMuted(),
                      codeText.data(), codeText.data() + codeText.size());
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(s12Body.min.x + 34.0f, sry),
                        s12Body.width() * 0.56f, ColorTextSecondary(), cond, true);
        // 右半列给「停下后该做什么」（`StopCodeHint`）—— 判据说明**什么时候停**，
        // 提示说明**停下之后怎么办**，两者缺一这条规则就没法用。
        DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                        ImVec2(s12Body.min.x + s12Body.width() * 0.58f, sry),
                        s12Body.width() * 0.42f, ColorTextMuted(), hint, true);
        sry += kStopRuleRowH;
    }
    // ⚠️ 诚实标注：这是**规则表**，不是实时状态。真正的逐条判定发生在
    //    `novelcore::EvaluateStop`，它要一整轮跑出来的现场数字（各 check_id 失败
    //    次数、评审 FAIL 累计、引用缺失计数…），而本页没有跑起来的章，没有那些数。
    //    画成「已触发 / 未触发」就是编状态 —— 那正是本轮清掉的那类假数据。
    DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(s12Body.min.x, sry + 2.0f),
                    s12Body.width(), ColorTextMuted(),
                    "规则表（`09` §2.2，novelcore::StopCodeCondition / StopCodeHint）。"
                    "逐条判定发生在跑完一章之后，本页没有现场数字，故不标触发状态。");
    return s12;
}

} // namespace overview
} // namespace shine::pages
