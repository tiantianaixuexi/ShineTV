#pragma once
// shine::video —— 生成侧**降级账**（`06` K28「降级必须可见」的数据源）
//
// 为什么必须有它：降级（无参考图 → 纯文生图、配了 ControlNet 却没有控制图、宽高被对齐纠正、
// 上游资产未就绪）以前只是 `SceneToImageResult::degraded` 这个 bool + 一行 `log::Warn` ——
// **日志一关就没了**，章级报告（`09` §2.7 的 `cost_report.json`）无从取证。这里把它变成三层：
//   ① **结构化**：`kind` 是可枚举常量、`detail` 是人读中文；
//   ② **可汇总**：`DegradationsToJson()` 直接给章级报告合并；
//   ③ **可追溯**：`AppendDegradationLedger()` 追加写 `<输出目录>/degradations.jsonl`（一行一条）。
//
// 线程：纯计算 + 文件 IO，**只在 worker 调用**（不要在 UI 线程写盘）。
#include "util/File.h"
#include "util/Time.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fmt/format.h>
#include <yyjson.h>

namespace shine::video {

// 降级类别（报告里的唯一口径；新增类别只加常量，不改结构）
inline constexpr std::string_view kDegradeNoReference = "no_reference";      // 缺颜色/参考图 → 纯文生图
inline constexpr std::string_view kDegradeNoControlNet = "no_controlnet";    // 配了 ControlNet 但没有控制图
inline constexpr std::string_view kDegradeSizeAligned = "size_aligned";      // 宽高被对齐纠正（Sanitize 纠正）
inline constexpr std::string_view kDegradeAssetNotReady = "asset_not_ready"; // 上游资产未就绪（`11` §2.7）
// —— H3（视频）侧 ——
inline constexpr std::string_view kDegradeRefTruncated = "ref_truncated";          // 参考图超上限被截断末尾
inline constexpr std::string_view kDegradeFirstFrameIgnored = "first_frame_ignored"; // 链式优先，首帧图被忽略
inline constexpr std::string_view kDegradeChainIgnored = "chain_ignored";          // 首段勾了链式 → 被忽略
inline constexpr std::string_view kDegradeParamUnified = "param_unified";          // 模型级参数与首段不一致被统一
inline constexpr std::string_view kDegradeSeedDerived = "seed_derived";            // 种子 -1（随机）→ 确定性派生值
inline constexpr std::string_view kDegradeNameCollision = "name_collision";        // 同名不同路径 → 上传会互相覆盖

// `shotIndex` 用 npos 表示「工程级」（不对应具体分镜）
inline constexpr std::size_t kDegradeNoShot = static_cast<std::size_t>(-1);

struct GenerationDegradation {
    std::string kind;                       // 上列常量之一
    std::string detail;                     // 中文，可直接显示
    std::size_t shotIndex = kDegradeNoShot; // npos = 工程级
};

[[nodiscard]] inline bool HasDegradation(const std::vector<GenerationDegradation>& list,
                                         std::string_view kind) noexcept {
    for (const GenerationDegradation& item : list) {
        if (item.kind == kind) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::string DegradationLine(const GenerationDegradation& item) {
    const std::string where = item.shotIndex == kDegradeNoShot
                                  ? std::string{"工程"}
                                  : fmt::format("分镜 #{}", item.shotIndex + 1);
    return fmt::format("[{}] {}：{}", item.kind, where, item.detail);
}

// `[{"kind":…,"shot":N,"detail":…}, …]`（工程级写 `"shot":-1`）
[[nodiscard]] inline std::string DegradationsToJson(const std::vector<GenerationDegradation>& list) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return "[]";
    }
    yyjson_mut_val* root = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    for (const GenerationDegradation& item : list) {
        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strncpy(doc, obj, "kind", item.kind.data(), item.kind.size());
        yyjson_mut_obj_add_int(doc, obj, "shot",
                               item.shotIndex == kDegradeNoShot
                                   ? -1
                                   : static_cast<std::int64_t>(item.shotIndex));
        yyjson_mut_obj_add_strncpy(doc, obj, "detail", item.detail.data(), item.detail.size());
        yyjson_mut_arr_add_val(root, obj);
    }
    std::size_t len = 0;
    char* text = yyjson_mut_val_write(root, 0, &len);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        return "[]";
    }
    std::string out{text, len};
    std::free(text);
    return out;
}

// 追加 `{"ts":…,"task":…,"shot":…,"kind":…,"detail":…}` 到 `<dir>/degradations.jsonl`。
// 返回写入条数；`-1` = 写失败（目录建不出/打开失败）——**记账失败不能反过来炸掉出图**，调用方告警即可。
[[nodiscard]] inline int AppendDegradationLedger(const std::filesystem::path& dir,
                                                 std::string_view taskLabel,
                                                 const std::vector<GenerationDegradation>& list) {
    if (list.empty()) {
        return 0;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return -1;
    }
    std::ofstream out = util::OpenOutput(dir / "degradations.jsonl",
                                        std::ios::binary | std::ios::app);
    if (!out) {
        return -1;
    }
    const std::int64_t ts = util::NowMillis() / 1000;
    int written = 0;
    for (const GenerationDegradation& item : list) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (doc == nullptr) {
            break;
        }
        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, obj);
        const std::int64_t shot = item.shotIndex == kDegradeNoShot
                                      ? -1
                                      : static_cast<std::int64_t>(item.shotIndex);
        yyjson_mut_obj_add_int(doc, obj, "ts", ts);
        yyjson_mut_obj_add_strncpy(doc, obj, "task", taskLabel.data(), taskLabel.size());
        yyjson_mut_obj_add_int(doc, obj, "shot", shot);
        yyjson_mut_obj_add_strncpy(doc, obj, "kind", item.kind.data(), item.kind.size());
        yyjson_mut_obj_add_strncpy(doc, obj, "detail", item.detail.data(), item.detail.size());
        std::size_t len = 0;
        char* text = yyjson_mut_val_write(obj, 0, &len);
        yyjson_mut_doc_free(doc);
        if (text == nullptr) {
            continue;
        }
        out.write(text, static_cast<std::streamsize>(len));
        out.put('\n');
        std::free(text);
        ++written;
    }
    out.flush();
    return out.good() ? written : -1;
}

} // namespace shine::video
