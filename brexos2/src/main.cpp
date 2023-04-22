#include <stdio.h>
#include "fd.cpp"
#include "brexos2.cpp"
#include <readline/readline.h>
#include <readline/history.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

bool validateAxis(unsigned axis) {
    if (axis > 1) {
        printf("Invalid axis: %u\n", axis);
        return false;
    }

    return true;
}

bool validateDirection(unsigned dir) {
    if (dir > 1) {
        printf("Invalid direction: %u\n", dir);
        return false;
    }

    return true;
}

bool validateRate(int rate) {
    if (rate < -5000 || rate > 5000) {
        printf("Invalid rate: %u\n", rate);
        return false;
    }

    return true;
}

bool measureSlewRate(Brexos2Direct& mount, unsigned axis) {
    int initialCount;
    int prevCount;
    int count;
    uint8_t status;
    timespec startTime;

    if (!mount.inquiry(axis, status, initialCount)) return false;
    prevCount = initialCount;
    clock_gettime(CLOCK_REALTIME, &startTime);

    for (int i = 0; i < 10; i++, prevCount = count) {
        sleep(1);
        if (!mount.inquiry(axis, status, count)) return false;
        printf("CPS: %d\n", count - prevCount);
    }

    timespec endTime;
    clock_gettime(CLOCK_REALTIME, &endTime);

    double dblElapsed = (endTime.tv_sec - startTime.tv_sec) + 1e-9 * (endTime.tv_nsec - startTime.tv_nsec);

    printf("Elapsed: %.6f, Avg CPS: %f\n", dblElapsed, (count - initialCount) / dblElapsed);
    return true;
}

int main(int argc, char **argv) {
    Brexos2Direct mount;
    int exitCode = 1;

    if (!mount.init("/dev/ttyUSB0")) {
        puts("Cannot connect to mount");
        return exitCode;
    }

    using_history();
    char *cmdline;
    char *prevcmdline = NULL;
    char cmd[16];
    unsigned axis;
    int param1;
    unsigned param2;

    while ((cmdline = readline("brexos2>")) != NULL) {
        bool result = true;
        axis = 0;
        param1 = 0;
        sscanf(cmdline, "%15s %u %d %u", cmd, &axis, &param1, &param2);

        if (strcmp(cmd, "quit") == 0) {
            break;
        }
        else if (strcmp(cmd, "inq") == 0) {
            if (validateAxis(axis)) {
                int count;
                uint8_t status;
                result = mount.inquiry(axis, status, count);

                if (result) {
                    printf("Axis:%u status=%02x count:%d\n", axis, status, count);
                }
            }
        }
        else if (strcmp(cmd, "enable") == 0) {
            result = mount.enableMotors(true);
        }
        else if (strcmp(cmd, "disable") == 0) {
            result = mount.enableMotors(false);
        }
        else if (strcmp(cmd, "print_axes") == 0) {
            mount.printAxes();
        }
        else if (strcmp(cmd, "slew") == 0) {
            if (validateAxis(axis) && validateRate(param1)) {
                result = mount.slew(axis, param1);
            } 
        }
        else if (strcmp(cmd, "mslew") == 0) {
            if (validateAxis(axis)) {
                result = measureSlewRate(mount, axis);
            }
        }
        else if (strcmp(cmd, "goto") == 0) {
            if (validateAxis(axis) && validateRate(param1)) {
                result = mount.goTo(axis, param1, param2);
            }
        }
        else if (strcmp(cmd, "cmd10") == 0) {
            unsigned cmd10retval;

            if (validateAxis(axis)) {
                result = mount.cmd10(axis, cmd10retval);
            }

            if (result) {
                printf("%04x\n", cmd10retval);
            }
        }
        else if (strcmp(cmd, "cmd0f") == 0) {
            if (validateAxis(axis)) {
                result = mount.cmd0f(axis, param1);
            }
        }

        if (!result) {
            puts("Command failed");
        }

        if (prevcmdline == NULL || strcmp(cmdline, prevcmdline) != 0) {
            add_history(cmdline);
            free(prevcmdline);
            prevcmdline = cmdline;
        } else {
            free(cmdline);
        }
    }

    free(prevcmdline);
    return exitCode;
}

