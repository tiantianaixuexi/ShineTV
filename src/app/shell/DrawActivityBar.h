#pragma once
#include "app/AppInternal.h"

// R-S0：从 App.cpp 搬出（一窗口一文件），入口只此一个函数；声明见 AppInternal.h。
namespace shine::app {

void DrawActivityBar(float height);

} // namespace shine::app
