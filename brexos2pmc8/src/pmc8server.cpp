#pragma once
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include "debug.cpp"
#include "fd.cpp"
#include "brexos2.cpp"

#define BR2ES_STEP_RATIO (48.0 / 38.0)

struct Axis {
    unsigned m_direction;
    int m_target;
    int m_offset;

    Axis(): m_direction(0), m_target(0), m_offset(0) {
    }
};

class Pmc8Server {
    int m_serverSocket;
    Brexos2Direct& m_mount;
    Axis m_axes[2];
public:
    Pmc8Server(Brexos2Direct& mount): m_serverSocket(-1), m_mount(mount) {
    }

    ~Pmc8Server() {
        if (m_serverSocket != -1) {
            close(m_serverSocket);
        }
    }

    bool init(int port) {
        m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);

        if (m_serverSocket == -1) {
            fputs("Cannot create PMC8 socket", stderr);
            return -1;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(m_serverSocket, (sockaddr*)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "Cannot bind socket %d to port %u\n", m_serverSocket, port);
            goto err;
        }

        if (listen(m_serverSocket, 1) == -1) {
            fprintf(stderr, "Listen failed on socket %d\n", m_serverSocket);
            goto err;
        }

        return true;
    err:
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    void run() {
        while (true) {
            sockaddr_in addr;
            socklen_t addrlen = sizeof(addr);
            int clientSocket = accept(m_serverSocket, (sockaddr*)&addr, &addrlen);

            if (clientSocket == -1) {
                fprintf(stderr, "Connection to client failed: %d\n", errno);
            } else {
                dputs("Connected to client");
                FileDescriptor clientSocketFd(clientSocket);
                runClientLoop(clientSocketFd);
            }
        }
    }

private:
    void runClientLoop(FileDescriptor &fd) {
        char buf[16];
        const char *response;
        int responseLen;

        while (true) {
            int nread = fd.read(buf, sizeof(buf));
            if (nread <= 4) break;

            response = buf;
            responseLen = 0;
            dprintf("%.*s\n", nread, buf);

            if (buf[0] != 'E' || buf[1] != 'S' || buf[nread - 1] != '!') continue;

            switch (buf[2]) {
                case 'G':
                    switch (buf[3]) {
                        case 'd': {
                            if (nread == 6) {
                                int axis = buf[4] - '0';
                                getAxisCurrentDirection(axis, buf, sizeof(buf), &responseLen);
                            }
                            break;
                        }
                        case 'v': {
                            response = "ESGvES6B10A0!";
                            responseLen = 13; 
                            break;
                        }
                        case 'p': {
                            if (nread == 6) {
                                int axis = buf[4] - '0';
                                getAxisCurrentPosition(axis, buf, sizeof(buf), &responseLen);
                            }
                            break;
                        }
                        case 'r': {
                            if (nread == 6) {
                                int axis = buf[4] - '0';
                                getAxisCurrentRate(axis, buf, sizeof(buf), &responseLen);
                            }
                            break;
                        }
                    }
                    break;
                case 'P':
                    switch (buf[3]) {
                        case 't': {
                            if (nread == 12) {
                                int axis = buf[4] - '0';
                                unsigned tmp = parseUInt((uint8_t*)(buf + 5), 6);
                                int target = ((int32_t)(tmp << 8)) >> 8;
                                goTo(axis, target);
                                buf[2] = 'G';
                                responseLen = 12;
                            }
                            break;
                        }
                    }
                    break;
                case 'S':
                    switch (buf[3]) {
                        case 'd': {
                            if (nread == 7) {
                                int axis = buf[4] - '0';
                                int direction = buf[5] - '0';

                                if (axis >= 0 && axis <= 1 && direction >= 0 && direction <= 1) {
                                    m_axes[axis].m_direction = direction;
                                }

                                buf[2] = 'G';
                                responseLen = 7;
                            }
                            break;
                        }
                        case 'p': {
                            // Set Axis Position Value
                            if (nread == 12) {
                                int axis = buf[4] - '0';
                                unsigned tmp = parseUInt((uint8_t*)(buf + 5), 6);
                                int pos = ((int32_t)(tmp << 8)) >> 8;
                                setAxisPosition(axis, pos);
                                buf[2] = 'G';
                                responseLen = 12;
                            }
                            break;
                        }
                        case 'r': {
                            if (nread == 10) {
                                int axis = buf[4] - '0';
                                unsigned rate = parseUInt((uint8_t *)(buf + 5), 4);
                                setAxisSlewRate(axis, rate);
                                buf[2] = 'G';
                                responseLen = 10;
                            }
                            break;
                        }
                    }
                    break;
                case 'T':
                     switch (buf[3]) {
                        case 'r': {
                            if (nread == 9) {
                                unsigned rate = parseUInt((unsigned char *)(buf + 4), 4);
                                setPrecisionTrackingRate(rate);
                                buf[2] = 'G';
                                buf[3] = 'x';
                                responseLen = 9;
                            }
                            break;
                        }
                    }
                    break;
            }

            dprintf("%.*s\n\n", responseLen, response);
            if (!fd.writeFully(response, responseLen)) break;
        }

        dputs("Disconnected");
    }

    void getAxisCurrentDirection(int axisIndex, char *response, int responseMaxLen, int *responseLen) {
        if (!validateAxisIndex(axisIndex)) return;
    
        int dir = m_axes[axisIndex].m_direction;
        *responseLen = snprintf(response, responseMaxLen, "ESGd%d%01X!", axisIndex, dir);
    }

    void getAxisCurrentPosition(int axis, char *response, int responseMaxLen, int *responseLen) {
        if (!validateAxisIndex(axis)) return;

        int count;
        uint8_t status;

        if (m_mount.inquiry(axis, status, count)) {
            count = round(count * BR2ES_STEP_RATIO) + m_axes[axis].m_offset;
            *responseLen = snprintf(response, responseMaxLen, "ESGp%d%06X!", axis, count & 0xffffff);
        }
    }

    void getAxisCurrentRate(int axis, char *response, int responseMaxLen, int *responseLen) {
        if (!validateAxisIndex(axis)) return;
        int rate;

        if (m_mount.getAxisRate(axis, rate)) {
            rate *= BR2ES_STEP_RATIO;
            *responseLen = snprintf(response, responseMaxLen, "ESGr%d%04X!", axis, rate < 0 ? -rate : rate);
        }
    }

    bool validateAxisIndex(int axisIndex) {
        return axisIndex >= 0 && axisIndex <= 1;
    }

    void setAxisPosition(int axis, int pos) {
        if (!validateAxisIndex(axis)) return;

        dprintf("Axis: %d, new position: %06X\n", axis, pos & 0xffffff);

        int count;
        uint8_t status;

        if (m_mount.inquiry(axis, status, count)) {
            int pmc8count = count * BR2ES_STEP_RATIO;
            m_axes[axis].m_offset = pos - pmc8count;
        }
    }

    void setPrecisionTrackingRate(unsigned rate) {
        dprintf("setPrecisionTrackingRate: %04X\n", rate);
        unsigned char buf[16];

        int trackingRate = round(rate / 25.0 / BR2ES_STEP_RATIO * (5.0 / 38.0));
        dprintf("Tracking rate: %d\n", trackingRate);

        if (trackingRate >= 0 && trackingRate < 10) {
            m_mount.slew(1, 0);  // Stop DEC
            m_mount.track(0, trackingRate);  // Track RA
        }
    }

    void setAxisSlewRate(int axis, unsigned rate) {
        if (axis < 0 || axis > 1) {
            return;
        }

        int slewRate = round(rate / BR2ES_STEP_RATIO * (5.0 / 38.0));

        if (slewRate > 4000) {
            slewRate = 4000;
        }

        if (!m_axes[axis].m_direction) slewRate = -slewRate;
        dprintf("Axis: %d, slew rate: %d\n", axis, slewRate);
        m_mount.slew(axis, slewRate);
    }

    void goTo(int axis, int target) {
        if (axis < 0 || axis > 1) {
            return;
        }

        m_axes[axis].m_target = target;
        target = round((target - m_axes[axis].m_offset) / BR2ES_STEP_RATIO);
        dprintf("Goto axis: %d, target=%06X\n", axis, target & 0xffffff);
        m_mount.goTo(axis, 128 * 5, target); 
    }

    static unsigned parseUInt(const uint8_t *buf, int numDigits) {
        unsigned result = 0;

        for (int i = 0; i < numDigits; i++) {
            unsigned c = buf[i] - '0';

            if (c > 9) {
                c -= 'A' - '0' - 10;
                if (c > 15) c = 0;
            }

            result = (result << 4) | c; 
        }

        return result;
    }
};

