#pragma once

namespace shine::app {

class MainWindow;

namespace checks {

void RegisterP03WindowChecks(MainWindow& window);
void RegisterP03Probe(MainWindow& window);

} // namespace checks
} // namespace shine::app
