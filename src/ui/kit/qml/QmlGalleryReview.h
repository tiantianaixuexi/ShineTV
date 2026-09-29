#pragma once
// ui/kit/qml/QmlGalleryReview —— QML 侧的评审取证入口（2026-09-29 架构层）
//
// 与 Widgets 侧 verify/review/* 的关系：**同一套取证约定**。
// 关键在于它必须走 QuickHost::GrabBlocking()（场景图 grab），而不是
// review::Grab() 的 QWidget::grab() —— 后者对 QQuickWidget 只会得到空图。
//
// 触发：SHINE_QML_REVIEW=<目录>（沿用其余 P0x 的「环境变量即路径」约定）
// 产出：<目录>/qml-gallery-<主题>.png + shots-manifest.txt
//
// 5 套主题各出一张，用来验证「QML 与 QSS 共享 ColorToken」——
// 若某一主题下 QML 的 Tag 颜色与 Widgets 的不同，本页会立刻暴露。
#include <filesystem>

namespace shine::qml {

void SaveQmlGalleryReview(const std::filesystem::path& dir);

} // namespace shine::qml
