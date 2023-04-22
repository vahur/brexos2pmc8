#include <stdio.h>
#include "debug.cpp"
#include "fd.cpp"
#include "brexos2.cpp"
#include "pmc8server.cpp"
#include "webserver.cpp"

int main(int argc, char **argv) {
    Brexos2Direct mount;
    int exitCode = 1;

    if (!mount.init("/dev/ttyUSB0")) {
        fputs("Cannot connect to mount\n", stderr);
        return exitCode;
    }

    WebServer webserver;

    if (!webserver.init("ws://localhost:8889")) {
        fputs("Web server init failed\n", stderr);
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

