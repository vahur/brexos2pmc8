#pragma once
#include <unistd.h>
#include <errno.h>
#include <stdio.h>


class FileDescriptor {
protected:
    int m_fd;

public:
    FileDescriptor(): m_fd(-1) {
    }

    FileDescriptor(int fd): m_fd(fd) {
    }

    ~FileDescriptor() {
        if (m_fd != -1) {
            ::close(m_fd);
        }
    }

    operator int() const {
        return m_fd;
    }

    int read(void *buf, ssize_t len) {
        return ::read(m_fd, buf, len);
    }

    int readAtLeast(unsigned char *data, int len, int minLen) {
        unsigned char *dst = data;
        int numToRead = len;
        int numReadTotal = 0;

        while(true) {
            int numRead = ::read(m_fd, dst, numToRead);

            if (numRead < 1) {
                fprintf(stderr, "Read failed, fd=%d, errno=%d\n", m_fd, errno);
                return -1;
            }

            numReadTotal += numRead;
            if (numReadTotal >= minLen) break;

            numToRead -= numRead;
            dst += numRead;
        }

        return numReadTotal;
    }

    void set(int fd) {
        m_fd = fd;
    }

    void close() {
        ::close(m_fd);
        m_fd = -1;
    }

    bool writeFully(const void *data, ssize_t len) {
        while (len != 0) {
            ssize_t numWritten = ::write(m_fd, data, len);
            if (numWritten <= 0) return false;

            len -= numWritten;
            data = ((const char *) data) + numWritten;
        }

        return true;
    }
};

