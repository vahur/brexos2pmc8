#pragma once
#include <termios.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include "fd.cpp"

#define BREXOS2_AXIS_STATUS_SLEWING    0x04
#define BREXOS2_AXIS_STATUS_DISABLED   0x08
#define BREXOS2_AXIS_STATUS_DIRECTION  0x80

#define BREXOS2_MIN_GOTO_RATE 20
#define BREXOS2_MAX_GOTO_RATE 4000

class Brexos2Direct {
    struct Axis {
        unsigned m_rate;
        int m_position;
        uint8_t m_status;
        int m_gotoStart;
        int m_gotoTarget;
        unsigned m_gotoRate;

        Axis(): m_rate(0) {            
        }
    };

    FileDescriptor m_fd;
    pthread_t m_managerThread;
    pthread_mutex_t m_managerMutex;
    int m_managerThreadCreateStatus;
    int m_managerMutexCreateStatus;
    Axis m_axes[2];
    int m_axesIdleCount;
 public:
    Brexos2Direct(): m_managerThreadCreateStatus(-1), m_managerMutexCreateStatus(-1), m_axesIdleCount(0) {
    }

    ~Brexos2Direct() {
        if (m_managerThreadCreateStatus == 0) {
            pthread_cancel(m_managerThread);
            pthread_join(m_managerThread, NULL);
        }

        if (m_managerMutexCreateStatus == 0) {
            pthread_mutex_destroy(&m_managerMutex);
        }
    }

    bool init(const char *devPath) {
        int dev = open(devPath, O_RDWR | O_NOCTTY);
        if (dev == -1) return false; 

        termios params;
        if (tcgetattr(dev, &params) == -1) goto err;

        cfmakeraw(&params);
        cfsetispeed(&params, B9600);
        cfsetospeed(&params, B9600);
        params.c_cflag = CS8 | CREAD | CLOCAL;
        params.c_cc[VTIME] = 5;
        params.c_cc[VMIN] = 0;
        if (tcsetattr(dev, TCSANOW, &params) == -1) goto err;

        if (m_managerMutexCreateStatus != 0) {
            m_managerMutexCreateStatus = pthread_mutex_init(&m_managerMutex, NULL);
            if (m_managerMutexCreateStatus != 0) goto err;
        }

        m_managerThreadCreateStatus = pthread_create(&m_managerThread, NULL, managerThreadProc, this);

        if (m_managerThreadCreateStatus == 0) {
            m_fd.set(dev);
            return true;
        }
    err:
        close(dev);
        return false;
    }

    bool enableMotors(bool enable) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            result = cmdEnableMotors(enable);
            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool slew(uint8_t axisIndex, uint8_t direction, unsigned rate) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            if (rate > 0) {
                uint8_t status;
                int position;

                result = cmdInquiry(axisIndex, status, position);
                if (!result) return result;

                if (status & BREXOS2_AXIS_STATUS_DISABLED) {
                    cmdEnableMotors(true);
                }
            }

            result = cmdSlew(axisIndex, direction, rate);
            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool inquiry(uint8_t axis, uint8_t& status, int& count) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            result = cmdInquiry(axis, status, count);
            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool goTo(uint8_t axisIndex, unsigned rate, int target) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            uint8_t status;
            int position;

            result = cmdInquiry(axisIndex, status, position);

            if (status & BREXOS2_AXIS_STATUS_DISABLED) {
                cmdEnableMotors(true);
            }

            if (result && (status & BREXOS2_AXIS_STATUS_SLEWING)) {
                Axis &axis = m_axes[axisIndex];
                axis.m_gotoStart = position;
                axis.m_gotoTarget = target;
                axis.m_gotoRate = BREXOS2_MIN_GOTO_RATE;
                axis.m_rate = 0;
                result = cmdGoTo(axisIndex, axis.m_gotoRate, (unsigned) target);
            }

            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool getAxisRate(uint8_t axisIndex, unsigned& rate) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            result = updateAxis(axisIndex);

            if (result) {
                const Axis &axis = m_axes[axisIndex];

                if (axis.m_status & BREXOS2_AXIS_STATUS_DISABLED) {
                    rate = 0;
                } else {
                    rate = (axis.m_status & BREXOS2_AXIS_STATUS_SLEWING) ? axis.m_rate : axis.m_gotoRate * 25;
                }
            }

            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool cmd0f(unsigned param) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x03, 0x0f, (uint8_t) (param >> 8), (uint8_t) param };
        uint8_t buf[16];
        return writeCommand(cmd, sizeof(cmd), buf, sizeof(buf));
    }

    bool cmd10(unsigned &param) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x01, 0x10 };
        uint8_t buf[16];

        if (writeCommand(cmd, sizeof(cmd), buf, sizeof(buf))) {
            if (buf[3] == 3) {
                param = buf[5];
                param = (param << 8) | buf[6];
                return true;
            }
        }

        return false;
    }
private:
    static void *managerThreadProc(void *arg) {
        ((Brexos2Direct *) arg)->manageMount();
        return NULL;
    }

    void manageMount() {
        timespec sleepInterval = { 0, 100 /* MS */ * 1000000L };

        while (nanosleep(&sleepInterval, NULL) == 0) {
            if (pthread_mutex_lock(&m_managerMutex) != 0) break;

            manageAxis(0);
            manageAxis(1);
            managePowerSave();
            pthread_mutex_unlock(&m_managerMutex);
        }
    }

    bool isAxisEnabledAndSlewing(int axis) {
        return (m_axes[axis].m_status & (BREXOS2_AXIS_STATUS_DISABLED | BREXOS2_AXIS_STATUS_SLEWING)) == BREXOS2_AXIS_STATUS_SLEWING;
    }

    void managePowerSave() {
        if (isAxisEnabledAndSlewing(0) && isAxisEnabledAndSlewing(1)) {
            if (m_axes[0].m_rate == 0 && m_axes[1].m_rate == 0) {
                if (m_axesIdleCount++ >= 100) { // ~10 sec
                    cmdEnableMotors(false);
                }

                return;
            }
        }

        m_axesIdleCount = 0;
    }

    void manageAxis(uint8_t axisIndex) {
        Axis &axis = m_axes[axisIndex];
        if (!cmdInquiry(axisIndex, axis.m_status, axis.m_position)) return;

        if (axis.m_status & BREXOS2_AXIS_STATUS_DISABLED) {
            axis.m_rate = 0;
            return;
        }

        if (!(axis.m_status & BREXOS2_AXIS_STATUS_SLEWING)) {
            // Goto ramp
            int distance1 = abs(axis.m_gotoTarget - axis.m_position);
            int distance2 = abs(axis.m_position - axis.m_gotoStart);
            int distance = distance1 < distance2 ? distance1 : distance2;
            int rate = round(sqrt(distance) * 10.0);

            if (rate < BREXOS2_MIN_GOTO_RATE) {
                rate = BREXOS2_MIN_GOTO_RATE;
            } else if (rate > BREXOS2_MAX_GOTO_RATE) {
                rate = BREXOS2_MAX_GOTO_RATE;
            }

            axis.m_gotoRate = rate;
            cmdGoTo(axisIndex, rate, axis.m_gotoTarget);
        }
    }

    bool updateAxis(int axisIndex) {
        Axis& axis = m_axes[axisIndex];
        return cmdInquiry(axisIndex, axis.m_status, axis.m_position);
    }

    bool cmdEnableMotors(bool enable) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x01, (uint8_t) (enable ? 0xff : 0x00) };
        return m_fd.writeFully(cmd, sizeof(cmd));
    }

    bool cmdGoTo(uint8_t axis, unsigned rate, unsigned target) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x06, (uint8_t) (axis << 5 | 2), 
                (uint8_t) (rate >> 8), (uint8_t) rate,
                (uint8_t) (target >> 16), (uint8_t) (target >> 8), (uint8_t) target };
        uint8_t buf[16];
        return writeCommand(cmd, sizeof(cmd), buf, sizeof(buf));
    }

    bool cmdInquiry(uint8_t axis, uint8_t& status, int& count) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x01, (uint8_t) (axis << 5 | 4) };
        uint8_t buf[16];

        if (writeCommand(cmd, sizeof(cmd), buf, sizeof(buf))) {
            if (buf[3] == 5) {
                status = buf[5];
                count = (int8_t) buf[6];
                count = (count << 8) | buf[7];
                count = (count << 8) | buf[8];
                return true;
            }
        }

        return false;
    }

    bool cmdSlew(uint8_t axis, uint8_t direction, unsigned rate) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x04, (uint8_t) (axis << 5 | 1), direction,
                (uint8_t) (rate >> 8), (uint8_t) rate };
        uint8_t buf[16];
        m_axes[axis].m_rate = rate;
        return writeCommand(cmd, sizeof(cmd), buf, sizeof(buf));
    }

    bool readResponse(uint8_t *buf, int len) {
        int numRead = m_fd.readAtLeast(buf, len, 4);

        if (numRead == -1) {
            fputs("readAtLeast failed", stderr);
            return false;
        }

        if (buf[0] == 0x55 && buf[1] == 0xaa && buf[2] == 0x01) {
            int packetLen = buf[3];
            int numToRead = packetLen + 4 - numRead;

            if (numToRead > 0) {
                if (m_fd.readAtLeast(buf + numRead, len - numRead, numToRead) == -1) {
                    fputs("Second readAtLeast failed", stderr);
                    return false;
                }

            }

#ifdef DEBUG
            for (int i = 4; i < packetLen + 4; i++) {
                fprintf(stderr, "%02x ", buf[i]);
            }

            fputc('\n', stderr);
#endif
            return true;
        }

        return false;
    }

    bool writeCommand(const uint8_t *cmd, int cmdLen, uint8_t *response, int responseLen) {
        if (!m_fd.writeFully(cmd, cmdLen)) return false;
        return readResponse(response, responseLen);
    }
};
