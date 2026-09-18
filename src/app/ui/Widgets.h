#pragma once
// shine::app::ui —— 可复用 UI 组件层（R-S1）
//
// 规则（照 `Plan/R/R-UI拆分与主题配置化.md` §4）：
//   * 只依赖 **ImGui + theme + app/UiState**，**绝不** include `graph/` `comfy/` `media/` `gallery/`；
//   * 只抽"已经重复出现"的东西（出现第 3 处才抽），不做过度抽象；
//   * 颜色一律由调用方从 `theme::Current()` 取（本层不硬编码色值）。
#include <imgui.h>

#include <string>
#include <string_view>

namespace shine::app::ui {

// 面板标题：小字标题 + 分隔线（侧栏/各面板开头统一样式）
void PanelHeader(std::string_view title);

// 分区标题（`ImGui::SeparatorText` 的统一入口，方便以后统一改样式）
void SectionText(std::string_view text);

// 键值行：左侧灰字 key，右侧正常色 value
void KvRow(std::string_view key, std::string_view value);

// 空状态提示（"暂无内容"这类占位）
void EmptyState(std::string_view text);

// 小徽标：指定颜色的圆点 + 文字（连接状态、计数等）
void Badge(std::string_view text, const float color[4]);

// 路径选择行：标签 + 输入框 + 「浏览…」；返回 true 表示值被改过（调用方据此写回 Settings）
// folder=true 走目录选择（app::SelectFolderDialog），false 走文件选择
bool PathPickerRow(std::string_view label, std::string_view hint, std::string& buffer, std::string_view browseTitle,
                   bool folder);

// 缩略图瓦片：有纹理（id != 0）就按原比例画；否则占位框 + 状态文字
// `id` 用 `ImTextureID`（DX11 下是 SRV 指针），因此本层不认识 gpu/ 类型
void ThumbTile(ImTextureID id, ImVec2 size, std::string_view stateText);

// 多行 tooltip：`ImGui::SetTooltip` 不会换行，长名字（如分类 `ControlNet Preprocessors/Foo/Bar`）
// 会被截断或撑成一条巨长的横条。这里按 wrapEm×字号 折行（默认 30em ≈ 一屏可读宽度）。
// 用法：`if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ui::TooltipWrapped(text);`
void TooltipWrapped(std::string_view text, float wrapEm = 30.0f);

// 可拖分隔条（splitter）：在**当前光标处**放一条竖向拖拽条，左右拖动改 `*width`（自动 clamp）。
// 用法：`BeginChild(左, ImVec2(width, 0)); …; EndChild(); ui::DragSplitter("##sp", &width, 160, 520, height); BeginChild(右, …)`
// 返回 true 表示本帧宽度变了。鼠标悬停/拖动时变 ⬌ 光标并高亮。
bool DragSplitter(const char* id, float* width, float minWidth, float maxWidth, float height);

} // namespace shine::app::ui
