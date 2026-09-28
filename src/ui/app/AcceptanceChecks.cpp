#include "ui/app/AcceptanceChecks.h"

#include "ui/verify/checks/P02Checks.h"
#include "ui/verify/checks/P03Checks.h"
#include "ui/verify/checks/P04ChapterChecks.h"
#include "ui/verify/checks/P04InitChainChecks.h"
#include "ui/verify/checks/P04ReviewChecks.h"
#include "ui/verify/checks/P04WorldChecks.h"
#include "ui/verify/checks/P05Checks.h"
#include "ui/verify/checks/P06Checks.h"
#include "ui/verify/checks/P07Checks.h"
#include "ui/verify/checks/P08Checks.h"
#include "ui/verify/checks/P09Checks.h"
#include "ui/verify/checks/P10Checks.h"
#include "ui/pages/shell/MainWindow.h"

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
