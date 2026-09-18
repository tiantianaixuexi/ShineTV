// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/SideViewTitle.h"
#include "app/AppIncludes.h"

namespace shine::app {

const char* SideViewTitle(SideView v) {
    for (const auto& a : Activities()) {
        if (a.id == v) return a.title;
    }
    return "侧栏";
}

} // namespace shine::app
