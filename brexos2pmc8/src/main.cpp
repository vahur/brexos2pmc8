#include <stdio.h>
#include "debug.cpp"
#include "fd.cpp"
#include "brexos2.cpp"
#include "pmc8server.cpp"

int main(int argc, char **argv) {
    Brexos2Direct mount;
    int exitCode = 1;

    if (!mount.init("/dev/ttyUSB0")) {
        fputs("Cannot connect to mount", stderr);
        return exitCode;
    }

    Pmc8Server server(mount);
    int result = 1;

    if (server.init(8888)) {
        dputs("Running...");
        server.run();
        exitCode = 0;
    }

    return exitCode;
}

