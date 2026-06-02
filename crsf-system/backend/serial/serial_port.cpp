#include "serial_port.h"

// We configure the line entirely through the kernel's termios2 interface so we
// can request an arbitrary baud rate (BOTHER). Note: <asm/termbits.h> defines
// `struct termios2` and the flag constants and must NOT be combined with
// glibc's <termios.h> (they both define `struct termios`). So this translation
// unit deliberately avoids <termios.h>.
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace backend {

namespace {
std::string errnoMessage(const std::string& what) {
    return what + ": " + std::strerror(errno);
}
}  // namespace

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const std::string& device, int baud) {
    close();

    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
        error_ = errnoMessage("open(" + device + ")");
        return false;
    }

    struct termios2 tio;
    std::memset(&tio, 0, sizeof(tio));
    if (ioctl(fd_, TCGETS2, &tio) != 0) {
        error_ = errnoMessage("ioctl(TCGETS2)");
        close();
        return false;
    }

    // Raw 8N1, no flow control, receiver enabled, ignore modem control lines.
    tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS | CBAUD);
    tio.c_cflag |= CS8 | CLOCAL | CREAD | BOTHER;  // BOTHER => use c_*speed
    tio.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK |
                     ISTRIP | IXON | IXOFF | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    // Non-canonical read: return as soon as data is available, with a 0.1s
    // inter-read timeout so the read loop can poll a shutdown flag.
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1;  // tenths of a second

    // Arbitrary baud via BOTHER.
    tio.c_ispeed = static_cast<speed_t>(baud);
    tio.c_ospeed = static_cast<speed_t>(baud);

    if (ioctl(fd_, TCSETS2, &tio) != 0) {
        error_ = errnoMessage("ioctl(TCSETS2)");
        close();
        return false;
    }

    error_.clear();
    return true;
}

void SerialPort::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

long SerialPort::read(std::uint8_t* buf, std::size_t n) {
    if (fd_ < 0) {
        error_ = "read: port not open";
        return -1;
    }
    ssize_t r = ::read(fd_, buf, n);
    if (r < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return 0;
        }
        error_ = errnoMessage("read");
        return -1;
    }
    return static_cast<long>(r);
}

bool SerialPort::write(const std::uint8_t* data, std::size_t len) {
    if (fd_ < 0) {
        error_ = "write: port not open";
        return false;
    }
    std::size_t written = 0;
    while (written < len) {
        ssize_t w = ::write(fd_, data + written, len - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            error_ = errnoMessage("write");
            return false;
        }
        written += static_cast<std::size_t>(w);
    }
    return true;
}

}  // namespace backend
