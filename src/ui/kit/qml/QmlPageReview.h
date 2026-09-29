#pragma once
// ui/kit/qml/QmlPageReview —— QML 侧的评审取证入口（2026-09-30 架构层）
//
// 与 Widgets 侧 verify/review/* 的关系：**同一套取证约定**。
// 关键在于它必须走 QuickHost::GrabBlocking()（场景图 grab），而不是
// review::Grab() 的 QWidget::grab() —— 后者对 QQuickWidget 只会得到空图。
//
// 触发：SHINE_QML_REVIEW=<目录>（沿用其余 P0x 的「环境变量即路径」约定）
// 过滤：SHINE_QML_PAGES=<页名,页名>（可选；缺省 = 拍全部已注册页）
// 产出：<目录>/qml-<页名>-<主题>.png + shots-manifest.txt
//
// 每页 × 5 套主题各出一张，用来验证「QML 与 QSS 共享 ColorToken」——
// 若某一主题下 QML 的 Tag 颜色与 Widgets 的不同，本页会立刻暴露。
//
// 页面注册表（RegisterPage）在本文件，是**共享层**：页面由并行子 Agent 各自
// 迁移，一个页面一条记录、记录里的尺寸与资源路径由该页作者自己填。
// 共享层单写者维护，页面作者只提供数据 —— 不要让页面作者改这个文件。
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::qml {

// 一条页面记录。
//   name  —— 同时是产出文件名的一段、以及 SHINE_QML_PAGES 的过滤键
//   res   —— qrc 里的路径（qt_add_resources 的 PREFIX 是 /qt/qml，所以形如 :/qt/qml/X.qml）
//   w/h   —— 取证画布尺寸。**必须是设计稿的视口尺寸**，截图才能与 webui 逐像素比
struct PageEntry {
    std::string_view name;
    std::string_view res;
    int w;
    int h;
    // 页面是否需要宿主注入 `Page`（AssetPageModel）上下文属性。
    // 已接入产品的页面（Assets）走真数据入口，取证时必须注入，否则运行期 ReferenceError。
    bool needs_page_model = false;
};

// 返回已注册页面（顺序即取证顺序）。注册表在 .cpp 的匿名命名空间里维护。
[[nodiscard]] const std::vector<PageEntry>& Pages();

// 拍全部（或 filter 指定的）页面 × 5 套主题，出图 + manifest 后 _Exit(0)。
void SaveQmlPageReview(const std::filesystem::path& dir, std::string_view filter);

} // namespace shine::qml
