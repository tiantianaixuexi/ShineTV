#pragma once
#include "app/AppInternal.h"

// 工作流模板浏览器（P3.7）：独立浮窗，左边分类、右边该分类下的工作流，**点击才创建**。
// 入口：图工具条「模板」按钮 / 菜单「文件 → 从模板新建工作流...」/ 侧栏「工作流」里的按钮。
namespace shine::app {

void DrawTemplateWindow();

} // namespace shine::app
