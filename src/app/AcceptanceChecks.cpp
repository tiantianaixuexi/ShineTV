#include "app/AcceptanceChecks.h"

#include "pages/checks/P02Checks.h"
#include "pages/checks/P03Checks.h"
#include "pages/checks/P04ChapterChecks.h"
#include "pages/checks/P04InitChainChecks.h"
#include "pages/checks/P04ReviewChecks.h"
#include "pages/checks/P04WorldChecks.h"
#include "pages/checks/P05Checks.h"
#include "pages/checks/P06Checks.h"
#include "pages/checks/P07Checks.h"
#include "pages/checks/P08Checks.h"
#include "pages/checks/P09Checks.h"
#include "pages/checks/P10Checks.h"
#include "pages/shell/MainWindow.h"

#include <QApplication>

namespace shine::app::acceptance {

std::optional<int> RegisterChecks(QApplication& app, MainWindow& window,
                                   std::wstring_view commandLine) {
    checks::RegisterP03WindowChecks(window);
    checks::RegisterP04WorldChecks(window);
    checks::RegisterP04InitChainChecks(window);
    checks::RegisterP04ChapterChecks(window);
    checks::RegisterP04ReviewChecks(window);
    checks::RegisterP05Checks(window);
    checks::RegisterP06Checks(window);
    checks::RegisterP07Checks(window);
    checks::RegisterP08Checks(window);
    checks::RegisterP09Checks(window);
    checks::RegisterP10Checks(window);
    checks::RegisterP03Probe(window);
    return checks::RegisterP02Checks(app, window, commandLine);
}

} // namespace shine::app::acceptance
