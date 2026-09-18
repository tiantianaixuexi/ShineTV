#pragma once
// shine::app::shots —— 分镜表（P5.3；自 video/ui 迁入 app）
//
// 三块：
//   * `DrawShotTable()`      → 停靠窗口「分镜」：左侧表格（`ImGuiListClipper`）+ 右侧编辑区
//   * `DrawVideoSidePanel()` → 侧栏「分镜」：工程字段 / 目录 / 打开保存 / 统计
//   * `Tick()`               → 每帧（**UI 线程**）执行"延迟命令"（增删改工程必须在表格绘制之外做，
//                              否则会在遍历 `shots` 时改 `vector` → 迭代器失效）
//
// 状态全在 `Editor()` 里（UI 线程独占）。P5.5 的生成按钮、P5.7 的出分镜图按钮都挂在本文件。
#include "video/CharacterAsset.h"
#include "video/VideoProject.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace shine::app::shots {

// 图库拖拽 payload 的**约定名**（`Plan/PLAN.md` §4 已定：G 线产出、主线消费）。
// 内容 = NUL 结尾的 UTF-8 绝对路径字符串（与 `SHINE_NODE_TYPE` 同款约定）。
inline constexpr const char* kImagePathPayload = "SHINE_IMAGE_PATH";

struct EditorState {
    ::shine::video::VideoProject project;
    std::filesystem::path projectFile; // 当前工程文件（空 = 还没存过盘）
    bool dirty = false;                // 有未保存修改（行尾标 `*`）
    std::size_t selected = 0;          // 选中的分镜下标（越界会被夹回）
    std::string message;               // 底部提示（中文，成功/失败都写这里）
};

[[nodiscard]] EditorState& Editor();

// 当前工程所在目录（有工程文件就用它所在目录，否则用 `VideoProjectDir()`）
[[nodiscard]] std::filesystem::path ProjectDir();

// 当前工程的角色资产目录（`CharacterAssetDir(ProjectDir())`）
[[nodiscard]] std::filesystem::path CharacterDir();

void DrawShotTable();
void DrawVideoSidePanel();

// 每帧调用一次（`App::DrawFrame`），必须在表格绘制之后
void Tick();

} // namespace shine::app::shots
