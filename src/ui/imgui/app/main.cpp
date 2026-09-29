#include "ui/imgui/host/AppEntry.h"

#include <mimalloc.h>

// ImGui 前端的进程入口。Qt 版是 QApplication + AppEntry::RunApp；
// 这里只有分配器初始化 + RunApp，宿主（Win32/WGL+GL3）全在 ui/imgui/host 里。
int main(int argc, char** argv) {
    mi_process_init();
    return shine::imguiapp::RunApp(argc, argv);
}
