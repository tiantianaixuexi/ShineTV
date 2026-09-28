#include "ui/app/AppEntry.h"

#include <mimalloc.h>

int main(int argc, char** argv) {
    mi_process_init();
    return shine::app::RunApp(argc, argv);
}
