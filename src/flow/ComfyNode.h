#pragma once
// shine::flow::ShineComfyNode —— 承载任意 ComfyUI `NodeTypeDef` 的**模型节点**（纯数据，无渲染）
//
// P01-S2「去旧图基座」：本文件原先派生自节点画布库的 Node 基类（插口/控件画在画布上），
// 现改为**纯图模型** —— 只保留 节点 / 端口 / 连线 / 值；渲染留给 P07 的 FlowCanvas。
//
// 布局约定（**必须保持稳定**）：端口与「输入行」一一对应 —— 第 i 个输入 = 第 i 行，
// 输出同理。这样"按高度均分端口"的渲染（P07）必然落在同一套行上，控件与标签不错位；
// 同一 className 的端口数量与顺序永远一致（= 定义顺序），旧存档回读不会错位。
// 控件输入也占一个端口：天然支持 ComfyUI 的"把控件改成输入"语义
// （有连线用连线值、没连线用控件值 —— 见 GraphCompiler）。
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "comfy/ComfyNodeDef.h"

namespace shine::flow {

// 二维坐标（图坐标系；取代旧 UI 库的二维向量类型，模型层不认识任何渲染类型）
struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

enum class SocketFlow { Input, Output };

struct Socket {
    SocketFlow flow = SocketFlow::Input;
    std::string name;
    std::vector<std::string> allowedTypes; // 并集拆开后的类型表；含 "ANY" 即通配放行
};

class ShineComfyNode {
public:
    // def 非空 = ComfyUI 动态节点（端口/控件全部由定义决定）
    explicit ShineComfyNode(std::shared_ptr<const comfy::NodeTypeDef> def);
    // 内置占位节点（未连接 ComfyUI 时的示例）：一进一出 "ANY"，无控件
    ShineComfyNode(std::string className, std::string title);

    // —— 身份与几何（画布只吃这些 DTO，不接触内部结构）——
    std::string id;        // 图内稳定 id（存档 / API JSON 引用）
    Vec2 pos;
    Vec2 size;

    [[nodiscard]] const std::string& ClassType() const noexcept { return className_; }
    [[nodiscard]] const std::string& Title() const noexcept { return title_; }
    [[nodiscard]] const std::shared_ptr<const comfy::NodeTypeDef>& Def() const noexcept { return def_; }
    [[nodiscard]] const std::vector<Socket>& Inputs() const noexcept { return inputs_; }
    [[nodiscard]] const std::vector<Socket>& Outputs() const noexcept { return outputs_; }

    // 控件值统一按**字符串**存取（编译时再按 InputDef 的类型转成 JSON 数字/布尔/字符串）
    [[nodiscard]] const std::string& WidgetValue(std::string_view inputName) const noexcept;
    [[nodiscard]] bool HasWidgetValue(std::string_view inputName) const noexcept;
    void SetWidgetValue(std::string_view inputName, std::string value);
    [[nodiscard]] const std::unordered_map<std::string, std::string>& Widgets() const noexcept { return widgets_; }

    // —— P3.7：按**名字**找槽位（下标只是兜底）——
    // `/object_info` 给的输入/输出顺序与前端 JSON 里 `inputs[]/outputs[]` 的数组顺序
    // **不保证一致**；JSON 里还可能出现 `父.子`（autogrow / dynamic combo 展开键）→ 归到父插口。
    [[nodiscard]] std::optional<std::size_t> InputSlotByName(std::string_view name) const;
    [[nodiscard]] std::optional<std::size_t> OutputSlotByName(std::string_view name) const;

    // 存档回读：未注册类型也要**保住数据**（端口按存档重建；能注册的走定义重建）
    void RestoreSockets(std::vector<Socket> inputs, std::vector<Socket> outputs);
    void SetTitle(std::string title) { title_ = std::move(title); }

private:
    void BuildSockets();
    void LoadDefaults();

    std::shared_ptr<const comfy::NodeTypeDef> def_;
    std::string className_;
    std::string title_;
    std::vector<Socket> inputs_;
    std::vector<Socket> outputs_;
    std::unordered_map<std::string, std::string> widgets_;
    static const std::string kEmptyValue;
};

// 两个端口能不能连：结构规则（自连 / 同向 / 越界）+ ComfyUI 类型语义（P3.7）：
//   * 并集：`"FLOAT,INT,BOOLEAN"`（一个端口能接多种类型）
//   * 通配：`*` / `ANY` / `COMFY_MATCHTYPE_*`（接什么就是什么）
//   * 动态父插口：`COMFY_AUTOGROW_V3` / `COMFY_DYNAMICCOMBO_V3`（子键类型由模板决定）
// 这三类若按"集合求交"判**永远接不上** —— 2026-09-17 导入官方 H3 模板实测：33 条只连上 24 条。
[[nodiscard]] bool CanConnect(const ShineComfyNode& from, std::size_t fromOutput, const ShineComfyNode& to,
                              std::size_t toInput);

} // namespace shine::flow
