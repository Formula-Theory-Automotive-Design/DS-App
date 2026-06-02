#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace backend {

// POSIX serial port wrapper for the FTDI link to the RadioMaster Nano.
//
// CRSF runs at 420000 baud, which is not one of the standard termios baud
// constants, so open() configures the line via the Linux termios2 / BOTHER
// ioctl to set an arbitrary bit rate. The port is opened raw (no canonical
// processing, no flow control, 8N1).
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Open `device` (e.g. "/dev/ttyUSB0") at `baud` bits/sec. Returns false and
    // leaves an error string in lastError() on failure.
    bool open(const std::string& device, int baud);
    void close();
    bool isOpen() const { return fd_ >= 0; }

    // Read up to `n` bytes into `buf`. Blocks until at least one byte arrives
    // or the inter-byte timeout (~0.1s) elapses. Returns the number of bytes
    // read (0 on timeout), or -1 on error.
    long read(std::uint8_t* buf, std::size_t n);

    // Write all bytes. Returns false on error.
    bool write(const std::uint8_t* data, std::size_t len);
    bool write(const std::vector<std::uint8_t>& data) {
        return write(data.data(), data.size());
    }

    const std::string& lastError() const { return error_; }

private:
    int         fd_ = -1;
    std::string error_;
};

}  // namespace backend
