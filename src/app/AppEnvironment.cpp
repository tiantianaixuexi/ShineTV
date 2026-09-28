#include "app/AppEnvironment.h"

#include <windows.h>

#include <string>

namespace shine::app {

std::filesystem::path EnvironmentPath(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return {};
    }

    std::wstring buffer(size, L'\0');
    GetEnvironmentVariableW(name, buffer.data(), size);
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return std::filesystem::path{buffer};
}

} // namespace shine::app
