#pragma once

#include <optional>
#include <string_view>

class QApplication;

namespace shine::app {

class MainWindow;

namespace checks {

[[nodiscard]] std::optional<int> RegisterP02Checks(QApplication& app, MainWindow& window,
                                                    std::wstring_view commandLine);

} // namespace checks
} // namespace shine::app
