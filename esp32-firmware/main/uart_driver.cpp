#include "uart_driver.h"

namespace app {

esp_err_t UartDriver::begin(uart_port_t port, int tx_pin, int rx_pin, int baud,
                            int rx_buffer) {
    port_ = port;

    uart_config_t cfg = {};
    cfg.baud_rate  = baud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_param_config(port_, &cfg);
    if (err != ESP_OK) return err;

    err = uart_set_pin(port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    // RX ring buffer, no TX ring buffer (blocking writes), no event queue.
    err = uart_driver_install(port_, rx_buffer, 0, 0, nullptr, 0);
    if (err != ESP_OK) return err;

    inited_ = true;
    return ESP_OK;
}

int UartDriver::readByte(std::uint8_t* out, TickType_t timeout) {
    if (!inited_) return -1;
    return uart_read_bytes(port_, out, 1, timeout);
}

int UartDriver::read(std::uint8_t* buf, std::size_t len, TickType_t timeout) {
    if (!inited_) return -1;
    return uart_read_bytes(port_, buf, len, timeout);
}

bool UartDriver::write(const std::uint8_t* data, std::size_t len) {
    if (!inited_) return false;
    const int written =
        uart_write_bytes(port_, reinterpret_cast<const char*>(data), len);
    return written == static_cast<int>(len);
}

}  // namespace app
