#include "ui/imgui/kit/Anim.h"

#include "im_anim.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/theme/Tokens.h"

namespace shine::kit {
namespace {

// tokens.css:27-28 的两条 cubic-bezier，原样搬过来。
iam_ease_desc Desc(const theme::motion::Ease& e) {
    iam_ease_desc d{};
    d.type = iam_ease_cubic_bezier;
    d.p0 = e.x1;
    d.p1 = e.y1;
    d.p2 = e.x2;
    d.p3 = e.y2;
    return d;
}

const iam_ease_desc kEase = Desc(theme::motion::kStandard);
const iam_ease_desc kEaseOut = Desc(theme::motion::kEaseOut);

// 补间用的 dt。pinned 时传 0：时钟停住，在飞的补间原地不动（模块顶层已经直接
// 返回 target 了，正常根本走不到这里，但传 0 是第二道保险）。
float FrameDelta() {
    return AnimationPinned() ? 0.0f : LastFrameDelta();
}

}  // namespace

void ReserveTweenPool(std::size_t floats, std::size_t vec2, std::size_t vec4, std::size_t ints,
             std::size_t colors) {
    iam_reserve(static_cast<int>(floats), static_cast<int>(vec2), static_cast<int>(vec4),
                static_cast<int>(ints), static_cast<int>(colors));
}

void SettleAll() {
    // 把池子清空：下一帧各 TransitionTo 会用新的 init_value 重建，
    // 而本模块在 pinned 期间传的 init_value 就是 target ⇒ 直接落在终值。
    iam_pool_clear();
}

float TransitionTo(ImGuiID id, ImGuiID channel, bool target, float durSeconds) {
    const float value = target ? 1.0f : 0.0f;
    if (AnimationPinned()) {
        // 钉住时钟时**直接落终值**，不参与补间 —— 见 Anim.h 的「取证时的语义」。
        // 冻在半路会让 52 张静息态截图每轮 md5 都不同。
        // ⚠️ 这里**不要**调 SettleAll()：那会把整池补间清掉，其他控件的过渡也被
        //    一起掀了，而且每帧清一次。池子只在 PinAnimation() 里清一次。
        return value;
    }
    // reduce-motion：CSS 的 prefers-reduced-motion 分支把所有 transition 压到 ~0。
    // 1ms 而不是 0：0 会让 iam_policy_crossfade 每帧重启一条 0 时长补间。
    const float dur = ReduceMotion() ? 0.001f : durSeconds;
    const float out = iam_tween_float(id, channel, value, dur, kEase, iam_policy_crossfade,
                                      FrameDelta(), value);
    return out < 0.0f ? 0.0f : (out > 1.0f ? 1.0f : out);
}

ImU32 TransitionColorTo(ImGuiID id, ImGuiID channel, ImU32 target, float durSeconds) {
    const auto split = [](ImU32 c, float out[4]) {
        out[0] = static_cast<float>((c >> 0) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
        out[3] = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
    };
    const auto join = [](const float v[4]) -> ImU32 {
        const auto ch = [](float x) {
            const float c = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
            return static_cast<ImU32>(c * 255.0f + 0.5f);
        };
        return ch(v[0]) | (ch(v[1]) << 8) | (ch(v[2]) << 16) | (ch(v[3]) << 24);
    };
    if (AnimationPinned()) {
        return target;
    }
    float to[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    split(target, to);
    const ImVec4 result = iam_tween_color(id, channel, ImVec4(to[0], to[1], to[2], to[3]),
                                          ReduceMotion() ? 0.001f : durSeconds, kEaseOut,
                                          iam_policy_crossfade, iam_col_srgb, FrameDelta(),
                                          ImVec4(to[0], to[1], to[2], to[3]));
    const float out[4] = {result.x, result.y, result.z, result.w};
    return join(out);
}

}  // namespace shine::kit
