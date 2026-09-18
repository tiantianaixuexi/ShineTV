#pragma once
// Tool 抽象与注册表（契约 S2.6 / P4-PLAN）
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"

#include <yyjson.h>

namespace shine::agent {

struct ToolError {
    std::string code;
    std::string message; // 中文
};

class Tool {
public:
    virtual ~Tool() = default;
    [[nodiscard]] virtual std::string_view Name() const = 0;
    [[nodiscard]] virtual std::string_view Description() const = 0;
    [[nodiscard]] virtual yyjson_mut_val* Schema(yyjson_mut_doc* doc) const = 0;
    [[nodiscard]] virtual std::expected<yyjson_doc*, ToolError>
    Execute(yyjson_val* args) = 0;
    [[nodiscard]] virtual bool IsWrite() const noexcept { return false; }
};

class ToolRegistry {
public:
    void Register(std::unique_ptr<Tool> tool);
    void Clear() noexcept;

    [[nodiscard]] yyjson_mut_val* ExportOpenAiTools(yyjson_mut_doc* doc) const;
    [[nodiscard]] std::expected<yyjson_doc*, ToolError>
    Execute(std::string_view name, yyjson_val* args);
    [[nodiscard]] std::size_t size() const noexcept { return tools_.size(); }

    // 已注册工具名（给 Agent→MCP 白名单解析用）
    [[nodiscard]] std::vector<std::string> Names() const;

    static constexpr int kMaxToolCalls = 20;
    static constexpr int kMaxRepeat = 3;

    void ResetLoopState() noexcept;
    [[nodiscard]] std::string CheckLoopGuard(std::string_view name, std::string_view argsCanon);
    void NoteCall(std::string_view name, std::string_view argsCanon);

private:
    std::vector<std::unique_ptr<Tool>> tools_;
    int steps_ = 0;
    int repeatCount_ = 0;
    std::string lastKey_;
};

// 注册内置工具（db 生命周期须覆盖 reg）
void RegisterBuiltinTools(ToolRegistry& reg, db::sqlite::Database& db, bool allowWrite);

// P4 离线自检：schema + 注册工具 + Execute + 循环保护
[[nodiscard]] bool RunToolsSelfCheck();

struct ToolLoopStats {
    int steps = 0;
    std::vector<std::string> callLog;
};

// create 回调：instructions + input_json + tools_json → 完整响应 JSON 或错误文案
using CreateWithToolsFn = std::function<std::expected<std::string, std::string>(
    std::string_view instructions, std::string_view inputJson, std::string_view toolsJson)>;

[[nodiscard]] std::expected<std::string, std::string>
RunToolLoop(ToolRegistry& reg, std::string_view instructions, std::string_view userText,
            const CreateWithToolsFn& create, ToolLoopStats* stats = nullptr);

} // namespace shine::agent
