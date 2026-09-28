#include "pipeline/StopPolicy.h"

namespace shine::pipeline {

StopDecision StopPolicy::Evaluate(const Budget& budget, bool has_comfy, bool has_llm,
                                  bool cross_review, bool committed_scenes) const {
    if (budget.Exceeded()) return {true, "S1–S4 预算", budget.Reason()};
    if (!has_llm) return {true, "S5 模型前置", "未配置 LLM，不能自动生成正文或视觉链"};
    if (!has_comfy) return {true, "S6 Comfy 前置", "ComfyUI 未连接，不能提交出图/出片"};
    if (!cross_review) return {true, "S7 交叉复核", "评审模型必须与写作模型不同"};
    if (!committed_scenes) return {true, "S8 场景门禁", "没有已提交 Scene，视觉链无法衔接"};
    return {};
}

} // namespace shine::pipeline
