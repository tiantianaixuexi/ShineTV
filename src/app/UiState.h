#pragma once
// shine::app::UiState —— 应用级 UI 状态（R-S0）
//
// 背景：原先这些状态是 `App.cpp` 匿名命名空间里的一堆 `g_*` 全局变量。窗口一旦拆到不同文件，
// 若继续各自 `extern` 就会形成网状依赖；统一收进 `UiState` 后：
//   * 只有一个所有者（`State()`），谁改了什么一目了然；
//   * 以后要"窗口布局/开关存盘"只需序列化这一个结构；
//   * 新窗口（如 G 线的图库）只依赖本头文件，不再碰 App.cpp 内部。
//
// 迁移约定：R-S0 阶段 `App.cpp` 保留一层**过渡引用别名**（`State().sideView` → `ui.sideView`）以做到
// "零改动搬移、行为不变"；R-S2 拆 shell 时逐步改为 `State().xxx` 并删除别名。
#include <array>
#include <string>

namespace shine::app {

// 六区布局：①顶栏 ②活动栏 ③侧栏 ④中央图 ⑤右栏 ⑥底栏
// ② 切换 ③ 的内容
enum class SideView : int {
    Assets = 0, // 资源
    Nodes,      // 节点
    Workflows,  // 工作流
    Comfy,      // Comfy
    Shots,      // 分镜（P5.3）
    Gallery,    // 图库（G-S5）
    Novel,      // 小说
    Paint,      // 画布（P6.2）
};

struct ActivityItem {
    SideView id;
    const char* icon;  // 活动栏短字
    const char* title; // tooltip / 侧栏标题
};

// 活动栏固定 8 项（顺序即显示顺序）
[[nodiscard]] const std::array<ActivityItem, 8>& Activities() noexcept;

// 验收截图用的"中央区抢焦点"目标（`SHINE_WINDOW=shots|gallery|paint`）
// 同区多窗口的选中 tab 由 imgui.ini 决定，代码改不动 → 只能开局抢几帧焦点
enum class FocusWindow : int { None = 0, Shots, Gallery, Novel, Paint };

struct UiState {
    // —— 浮窗可见性 ——
    bool showSettings = false;
    bool showAbout = false;
    bool showStyleEditor = false;
    bool showTemplateWindow = false; // P3.7：工作流模板浏览器（工具栏「模板」/ 文件菜单打开）
    bool showNodeWindow = false;     // P3.7b：节点浏览器（工具栏「节点」/ 视图菜单打开）
    bool showViewer = false;         // G-S5：「查看器」浮窗（完整实现在 G-S10）

    // —— 侧栏 ——
    SideView sideView = SideView::Assets;
    bool sideOpen = true;

    // —— 验收用（SHINE_WINDOW=shots|gallery）——
    // dock 的"当前页"由 imgui.ini 的 `Selected=` 决定，代码改不动；只能开局抢几帧焦点
    // 把中央区切到目标页（抢完归零，之后照常交回用户）。
    int focusFrames = 0;
    FocusWindow focusWindow = FocusWindow::None;

    // —— 编辑缓冲 ——
    // 语义：空串表示"还没从 Settings 填充过"，首帧由各窗口自行填充（保持拆前行为）
    std::string editComfyUrl;
    std::string editThemeId;
    std::string editGalleryLocalDir;
    std::string editGalleryOutputDir;
    std::string editGalleryInputDir;
    // P1 OpenAI（密钥用密码框；空 = 尚未从 Settings 填充）
    std::string editOpenaiBaseUrl;
    std::string editOpenaiApiKey;
    std::string editOpenaiModelDefault;
    std::string editOpenaiModelPlanner;
    std::string editOpenaiModelWriter;
    std::string editOpenaiModelCritic;
    // 多模型
    int editLlmProvider = 0; // 0 openai 1 mimo 2 minimax 3 custom
    std::string editMimoBaseUrl;
    std::string editMimoApiKey;
    std::string editMimoModel;
    std::string editMinimaxBaseUrl;
    std::string editMinimaxApiKey;
    std::string editMinimaxModel;
    // P9.1 出图后端
    std::string editImageBackend;
    std::string editImageBaseUrl;
    std::string editImageApiKey;
    std::string editImageModel;
    int editImageWidth = 1024;
    int editImageHeight = 1024;
    int editImageSteps = 20;
};

[[nodiscard]] UiState& State() noexcept;

} // namespace shine::app
