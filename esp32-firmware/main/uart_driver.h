#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

namespace app {

// Thin RAII-ish wrapper over the ESP-IDF UART driver for the CRSF link.
// CRSF is 420000 baud, 8N1, no flow control.
class UartDriver {
public:
    esp_err_t begin(uart_port_t port, int tx_pin, int rx_pin, int baud,
                    int rx_buffer = 512);

    // Read a single byte. Returns 1 if a byte was read, 0 on timeout, <0 error.
    int readByte(std::uint8_t* out, TickType_t timeout);

    // Read up to `len` bytes. Returns bytes read, or <0 on error.
    int read(std::uint8_t* buf, std::size_t len, TickType_t timeout);

    bool write(const std::uint8_t* data, std::size_t len);
    bool write(const std::vector<std::uint8_t>& v) { return write(v.data(), v.size()); }

private:
    uart_port_t port_   = UART_NUM_2;
    bool        inited_ = false;
};

}  // namespace app
