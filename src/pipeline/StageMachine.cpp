#include "pipeline/StageMachine.h"

#include <algorithm>
#include <stdexcept>

namespace shine::pipeline {
namespace {

std::vector<StageDefinition> MakeStages() {
    std::vector<StageDefinition> out;
    const std::pair<const char*, const char*> text[] = {
        {"T1", "章节初始化"}, {"T2", "前情导入"}, {"T3", "世界状态"}, {"T4", "角色状态"},
        {"T5", "关系网"}, {"T6", "场景计划"}, {"T7", "冲突编排"}, {"T8", "伏笔计划"},
        {"T9", "场景事件序"}, {"T10", "正文提示"}, {"T11", "正文写作"}, {"T12", "章节评审"},
        {"T13", "修复"}, {"T14", "状态提取"}, {"T15", "状态校验"}, {"T16", "状态提交"},
        {"T17", "下一章备忘"}, {"V0", "资产管线"}, {"V1", "场景切分"}, {"V2", "导演七问"},
        {"V3", "表演设计"}, {"V4", "空间设计"}, {"V5", "镜头设计"}, {"V6", "时间轴"},
        {"V7", "声音设计"}, {"V8", "连续性校验"}, {"V9", "叙事分镜"}, {"V10", "提示词生成"},
        {"V11", "出图/出片"}};
    int index = 0;
    for (const auto& [code, name] : text) {
        out.push_back({static_cast<StageId>(index), code, name, index < 17 ? "text" : "visual",
                       index == 0 ? "项目已打开" : "上一阶段退出条件通过",
                       code == std::string("T16") || code == std::string("V8") ? "门禁与产物校验通过" : "阶段产物已落盘"});
        ++index;
    }
    return out;
}

} // namespace

const std::vector<StageDefinition>& AllStages() {
    static const std::vector<StageDefinition> stages = MakeStages();
    return stages;
}

std::string StageCode(StageId id) {
    const auto& stages = AllStages();
    const auto index = static_cast<std::size_t>(id);
    return index < stages.size() ? stages[index].code : "UNKNOWN";
}

StageId StageFromCode(std::string_view code) {
    const auto& stages = AllStages();
    for (const auto& stage : stages) if (stage.code == code) return stage.id;
    return static_cast<StageId>(-1);
}

bool CanEnter(StageId id, StageId previous) {
    const auto index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(AllStages().size())) return false;
    if (index == 0) return previous == static_cast<StageId>(-1);
    return static_cast<int>(previous) == index - 1;
}

bool IsValidTransition(StageId from, StageId to) { return CanEnter(to, from); }

std::string TransitionError(StageId from, StageId to) {
    if (IsValidTransition(from, to)) return {};
    return "非法阶段跳转：" + StageCode(from) + " → " + StageCode(to) +
           "；必须按阶段表顺序，先完成上一阶段并通过退出条件";
}

} // namespace shine::pipeline
