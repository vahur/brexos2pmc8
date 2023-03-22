#pragma once
#include <termios.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include "fd.cpp"
#include "debug.cpp"

#define BREXOS2_AXIS_INDEX_RA 0
#define BREXOS2_AXIS_INDEX_DEC 1

#define BREXOS2_AXIS_STATUS_SLEWING    0x04
#define BREXOS2_AXIS_STATUS_DISABLED   0x08
#define BREXOS2_AXIS_STATUS_DIRECTION  0x80

#define BREXOS2_MIN_GOTO_RATE 20
#define BREXOS2_MAX_GOTO_RATE 4000

#define BREXOS2_MAX_GUIDING_PULSE_RATE 5

#define BREXOS2_SLEW_RAMP_THRESHOLD_RATE 1600
#define BREXOS2_MAX_SLEW_RATE BREXOS2_MAX_GOTO_RATE
#define BREXOS2_SLEW_RAMP_STEP_RATE 200

class Brexos2Direct {
    struct Axis {
        int m_rate;
        int m_slewRate;
        bool m_slewRampActive;
        int m_trackingRate;
        int m_currentTrackingRate;
        int m_position;
        uint8_t m_status;
        int m_gotoStart;
        int m_gotoTarget;
        int m_gotoRate;

        Axis(): m_rate(0), m_slewRate(0), m_slewRampActive(false), m_trackingRate(0), m_currentTrackingRate(0),
                m_position(0), m_status(BREXOS2_AXIS_STATUS_DISABLED), m_gotoStart(0), m_gotoTarget(0), m_gotoRate(0) {
        }

        void print(uint8_t index) {
            printf(
                "Axis %u\n"
                "-------------------------\n"
                "Rate:          %d\n"
                "Slew rate:     %d\n"
                "Slew ramp act: %s\n"
                "Tracking rate: %d\n"
                "Cur trk rate:  %d\n"
                "Position:      %08X\n"
                "Status:        %02X\n"
                "Goto start:    %08X\n"
                "Goto target:   %08X\n"
                "Goto rate:     %08X\n\n",
                index,
                m_rate,
                m_slewRate,
                m_slewRampActive ? "true" : "false",
                m_trackingRate,
                m_currentTrackingRate,
                m_position,
                m_status,
                m_gotoStart,
                m_gotoTarget,
                m_gotoRate
            );
        }
    };

    FileDescriptor m_fd;
    pthread_t m_managerThread;
    pthread_mutex_t m_managerMutex;
    int m_managerThreadCreateStatus;
    int m_managerMutexCreateStatus;
    Axis m_axes[2];
    int m_axesIdleCount;
    int m_tickCount;
 public:
    Brexos2Direct(): m_managerThreadCreateStatus(-1), m_managerMutexCreateStatus(-1), m_axesIdleCount(0), m_tickCount(0) {
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
        m_fd.set(dev);

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

        if (!cmdEnableMotors(false)) goto err;
        if (!updateAxis(0)) goto err;
        if (!updateAxis(1)) goto err;

        #ifdef DEBUG
            m_axes[0].print(0);
            m_axes[1].print(1);
        #endif

        m_managerThreadCreateStatus = pthread_create(&m_managerThread, NULL, managerThreadProc, this);

        if (m_managerThreadCreateStatus == 0) {
            return true;
        }
    err:
        m_fd.close();
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

    bool track(uint8_t axisIndex, int rate) {
        if (pthread_mutex_lock(&m_managerMutex) != 0) return false;
        bool result = false;
        Axis &axis = m_axes[axisIndex];

        if (rate < 0) rate = -rate;

        do {
            if (!updateAxis(axisIndex, axis)) break;

            if (axis.m_status & BREXOS2_AXIS_STATUS_DISABLED) {
                if (rate != 0) {
                    if (!cmdEnableMotors(true)) break;
                } else {
                    result = true; // Motors already disabled when turning tracking off
                    break;
                }
            }

            // Only update motor if it's idle
            if (axis.m_status & BREXOS2_AXIS_STATUS_SLEWING && !axis.m_slewRampActive && axis.m_slewRate == 0) {
                result = cmdSlew(axisIndex, rate);
            }
        } while (0);

        axis.m_trackingRate = rate;
        axis.m_currentTrackingRate = rate;
        pthread_mutex_unlock(&m_managerMutex);
        return result;
    }

    bool slew(uint8_t axisIndex, int rate) {
        if (pthread_mutex_lock(&m_managerMutex) != 0) return false;
        bool result = false;
        Axis &axis = m_axes[axisIndex];

        do {
            if (!updateAxis(axisIndex, axis)) break;

            if (axis.m_status & BREXOS2_AXIS_STATUS_DISABLED) {
                if (rate == 0 && axis.m_trackingRate == 0) {
                    result = true; // Motors already disabled
                    break;
                }

                if (!cmdEnableMotors(true)) break;
            }

            // No slewing during goto
            if (!(axis.m_status & BREXOS2_AXIS_STATUS_SLEWING)) break;

            if (axis.m_trackingRate == 0
                    || axis.m_slewRampActive
                    || rate > BREXOS2_MAX_GUIDING_PULSE_RATE
                    || rate < -BREXOS2_MAX_GUIDING_PULSE_RATE) {

                if (rate <= -BREXOS2_SLEW_RAMP_THRESHOLD_RATE) {
                    rate = -BREXOS2_MAX_SLEW_RATE;
                } else if (rate >= BREXOS2_SLEW_RAMP_THRESHOLD_RATE) {
                    rate = BREXOS2_MAX_SLEW_RATE;
                } else if (!axis.m_slewRampActive
                        && axis.m_rate > -BREXOS2_SLEW_RAMP_THRESHOLD_RATE
                        && axis.m_rate < BREXOS2_SLEW_RAMP_THRESHOLD_RATE) {
                    // Normal slew
                    result = cmdSlew(axisIndex, rate);
                    break;
                }

                axis.m_slewRampActive = true;
                result = true;
                break;
            }

            // Tracking is on and got guiding pulse
            int newRate = axis.m_currentTrackingRate + rate;

            if (newRate < 0) {
                newRate = 0;
            }

            result = cmdSlew(axisIndex, newRate);
        } while (0);

        m_axes[axisIndex].m_slewRate = rate;
        pthread_mutex_unlock(&m_managerMutex);
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

    bool goTo(uint8_t axisIndex, int rate, int target) {
        if (pthread_mutex_lock(&m_managerMutex) != 0) return false;
        bool result = false;

        do {
            uint8_t status;
            int position;

            result = cmdInquiry(axisIndex, status, position);

            if (status & BREXOS2_AXIS_STATUS_DISABLED) {
                if (!cmdEnableMotors(true)) break;
            }

            if (result && (status & BREXOS2_AXIS_STATUS_SLEWING)) {
                Axis &axis = m_axes[axisIndex];
                axis.m_gotoStart = position;
                axis.m_gotoTarget = target;
                axis.m_gotoRate = BREXOS2_MIN_GOTO_RATE;
                axis.m_rate = 0;
                result = cmdGoTo(axisIndex, axis.m_gotoRate, (unsigned) target);
            }
        } while (0);

        pthread_mutex_unlock(&m_managerMutex);
        return result;
    }

    bool getAxisRate(uint8_t axisIndex, int& rate) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            result = updateAxis(axisIndex);

            if (result) {
                const Axis &axis = m_axes[axisIndex];

                if (axis.m_status & BREXOS2_AXIS_STATUS_DISABLED) {
                    rate = 0;
                } else {
                    rate = (axis.m_status & BREXOS2_AXIS_STATUS_SLEWING) ? axis.m_slewRate : axis.m_gotoRate * 25;
                }
            }

            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool cmd0f(unsigned param) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x03, 0x0f, (uint8_t) (param >> 8), (uint8_t) param };
            uint8_t buf[16];
            result = writeCommand(cmd, sizeof(cmd), buf, sizeof(buf));
            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    bool cmd10(unsigned &param) {
        bool result = false;

        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x01, 0x10 };
            uint8_t buf[16];

            if (writeCommand(cmd, sizeof(cmd), buf, sizeof(buf))) {
                if (buf[3] == 3) {
                    param = buf[5];
                    param = (param << 8) | buf[6];
                    result = true;
                }
            }

            pthread_mutex_unlock(&m_managerMutex);
        }

        return result;
    }

    void printAxes() {
        if (pthread_mutex_lock(&m_managerMutex) == 0) {
            m_axes[0].print(0);
            m_axes[1].print(1);
            pthread_mutex_unlock(&m_managerMutex);
        }
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
            m_tickCount++;
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
            if (axis.m_gotoTarget != axis.m_gotoStart) {
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
                dprintf("Goto ramp: status=%02X start=%08X, end=%08X, rate=%u\n", axis.m_status, axis.m_gotoStart,
                        axis.m_gotoTarget, axis.m_gotoRate);

                cmdGoTo(axisIndex, rate, axis.m_gotoTarget);
            }
        } else {
            if (axis.m_slewRampActive) {
                int rate = axis.m_rate;

                if (rate < axis.m_slewRate) {
                    rate += BREXOS2_SLEW_RAMP_STEP_RATE;
                    if (rate > axis.m_slewRate) rate = axis.m_slewRate;
                } else if (rate > axis.m_slewRate) {
                    rate -= BREXOS2_SLEW_RAMP_STEP_RATE;
                    if (rate < axis.m_slewRate) rate = axis.m_slewRate;
                }

                axis.m_slewRampActive = rate != axis.m_slewRate;

                if (axis.m_rate != rate) {
                    dprintf("Slew ramp: status=%02X, rate=%d\n", axis.m_status, rate);
                    cmdSlew(axisIndex, rate);
                }
            } else if (axisIndex == BREXOS2_AXIS_INDEX_RA) {
                if (axis.m_trackingRate != 0) {
                    // Modulate tracking rate to slow it down a bit
                    int newTrackingRate = (m_tickCount % 6) == 0 ? axis.m_trackingRate - 1 : axis.m_trackingRate;

                    if (newTrackingRate < 0) {
                        newTrackingRate = 0;
                    }

                    axis.m_currentTrackingRate = newTrackingRate;

                    if (axis.m_slewRate > -BREXOS2_MAX_GUIDING_PULSE_RATE && axis.m_slewRate < BREXOS2_MAX_GUIDING_PULSE_RATE) {
                        int newRate = newTrackingRate + axis.m_slewRate;
                        if (newRate < 0) newRate = 0;

                        if (axis.m_rate != newRate) {
                            cmdSlew(BREXOS2_AXIS_INDEX_RA, newRate);
                        }
                    }
                }
            }
        }
    }

    bool updateAxis(int axisIndex) {
        Axis& axis = m_axes[axisIndex];
        return cmdInquiry(axisIndex, axis.m_status, axis.m_position);
    }

    bool updateAxis(int axisIndex, Axis &axis) {
        return cmdInquiry(axisIndex, axis.m_status, axis.m_position);
    }

    bool cmdEnableMotors(bool enable) {
        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x01, (uint8_t) (enable ? 0xff : 0x00) };
        return m_fd.writeFully(cmd, sizeof(cmd));
    }

    bool cmdGoTo(uint8_t axis, int rate, unsigned target) {
        if (rate < 0) rate = -rate;

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

    bool cmdSlew(uint8_t axis, int rate) {
        uint8_t direction;
        unsigned rateToUse;

        if (rate > 0) {
            direction = 1;
            rateToUse = rate;
        } else {
            direction = 0;
            rateToUse = -rate;
        }

        if (rateToUse > BREXOS2_MAX_SLEW_RATE) rateToUse = BREXOS2_MAX_SLEW_RATE;

        const uint8_t cmd[] = { 0x55, 0xaa, 0x01, 0x04, (uint8_t) (axis << 5 | 1), direction,
                (uint8_t) (rateToUse >> 8), (uint8_t) rateToUse };
        uint8_t buf[16];
        m_axes[axis].m_rate = rate;
        return writeCommand(cmd, sizeof(cmd), buf, sizeof(buf));
    }

    bool readResponse(uint8_t *buf, int len) {
        int numRead = m_fd.readAtLeast(buf, len, 4);

        if (numRead == -1) {
            fputs("readAtLeast failed\n", stderr);
            return false;
        }

        if (buf[0] == 0x55 && buf[1] == 0xaa && buf[2] == 0x01) {
            int packetLen = buf[3];
            int numToRead = packetLen + 4 - numRead;

            if (numToRead > 0) {
                if (m_fd.readAtLeast(buf + numRead, len - numRead, numToRead) == -1) {
                    fputs("Second readAtLeast failed\n", stderr);
                    return false;
                }
            }

#if 0
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
