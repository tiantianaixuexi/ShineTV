#pragma once
// shine::graph::ShineComfyNode —— 承载任意 ComfyUI `NodeTypeDef` 的通用节点
//
// 布局约定（**必须保持稳定**）：插口与「输入行」一一对应 —— 第 i 个输入 = 第 i 行，
// 输出同理；控件画在对应输入行上（输入名由 VNS 的插口标签渲染，控件占行的右半）。
// 这样 NodeArea::SocketToPosition 的"按 Size.y 均分"必然落在同一套行上，不会出现控件与标签错位。
// 因此**同一 className 的插口数量与顺序永远一致**（= 定义顺序），旧存档 FromJson 不会错位。
//
// 为什么控件输入也建插口：VNS 把插口按节点高度均分，若控件输入不建插口，
// 剩下的插口会被摊到整块高度上、与控件行重叠（P3.2 验收要求"控件不重叠"）。
// 建插口后每行 1:1 对齐；同时天然支持 ComfyUI 的"把控件改成输入"语义
// （有连线用连线值、没连线用控件值 —— 见 GraphCompiler）。
#include <VisualNode.h>
#include <VisualNodeSocket.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "comfy/ComfyNodeDef.h"

namespace shine::graph {

class ShineComfyNode final : public VisNodeSys::Node {
public:
    explicit ShineComfyNode(std::shared_ptr<const comfy::NodeTypeDef> def);
    ShineComfyNode(const ShineComfyNode& other); // 工厂要求拷贝构造

    [[nodiscard]] const std::string& ClassType() const noexcept { return className_; }
    [[nodiscard]] const std::shared_ptr<const comfy::NodeTypeDef>& Def() const noexcept { return def_; }

    // 控件值统一按**字符串**存取（编译时再按 InputDef 的类型转成 JSON 数字/布尔/字符串）
    [[nodiscard]] const std::string& WidgetValue(std::string_view inputName) const noexcept;
    [[nodiscard]] bool HasWidgetValue(std::string_view inputName) const noexcept;
    void SetWidgetValue(std::string_view inputName, std::string value);

protected:
    void Draw() override;
    Json::Value ToJson() override;
    bool FromJson(Json::Value json) override;
    // 插口类型判定（P3.7）：ComfyUI 的类型语义比 VNS 的"集合求交"宽（并集 / 通配 / 动态父插口），
    // 必须覆盖这一条 —— VNS 既用它判新连线（`Node::CanConnect`），也用它**复检并断开**已有连线
    //（`NodeSystem.cpp:1967`），只覆盖一半会被它反过来断掉。
    bool IsValidAsNewConnection(VisNodeSys::NodeSocket* ownSocket, VisNodeSys::NodeSocket* candidateSocket) override;

private:
    void BuildSockets();
    void LoadDefaults();
    void DrawWidget(const comfy::InputDef& in, float widgetX, float rowCenterY, float zoom);

    std::shared_ptr<const comfy::NodeTypeDef> def_;
    std::string className_;
    std::unordered_map<std::string, std::string> widgets_;
    static const std::string kEmptyValue;
};

} // namespace shine::graph
