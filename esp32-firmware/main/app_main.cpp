// ESP32 CRSF node — entry point.
//
// Reuses the SAME crsf-core library as the desktop backend (see
// components/crsf-core). This file is the thin ESP-IDF adapter: it brings up
// the UART, then runs two FreeRTOS tasks —
//   crsf_rx_task  : UART bytes -> crsf-core parser -> control logic
//   telemetry_task: periodically encode telemetry back upstream + log status
//
// Wire the receiver/handset CRSF signal to UART2:
//   RX_PIN <- CRSF TX   (data into the ESP32)
//   TX_PIN -> CRSF RX   (telemetry out of the ESP32)

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "crsf_parser.h"
#include "crsf_telemetry.h"

#include "control.h"
#include "uart_driver.h"

namespace {

constexpr char        kTag[]   = "crsf";
constexpr uart_port_t kPort    = UART_NUM_2;
constexpr int         kTxPin   = 17;
constexpr int         kRxPin   = 16;
constexpr int         kBaud    = 420000;

app::UartDriver  g_uart;
crsf::CRSFParser g_parser;
app::Control     g_control;

inline std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void crsf_rx_task(void*) {
    std::uint8_t byte;
    for (;;) {
        const int n = g_uart.readByte(&byte, pdMS_TO_TICKS(10));
        if (n == 1 && g_parser.feed(byte)) {
            g_control.onPacket(g_parser.getPacket(), now_ms());
        }
    }
}

void telemetry_task(void*) {
    for (;;) {
        // Example upstream telemetry. Replace with a real sensor reading.
        crsf::BatteryData battery;
        battery.voltage   = 16.8f;
        battery.current   = 0.0f;
        battery.capacity  = 0;
        battery.remaining = 100;
        g_uart.write(g_control.buildBatteryTelemetry(battery));

        ESP_LOGI(kTag,
                 "rc_frames=%u failsafe=%d ch1=%u lq=%u crc_err=%u",
                 g_control.rcFrameCount(),
                 static_cast<int>(g_control.failsafe(now_ms())),
                 g_control.channels().channels[0],
                 g_control.hasLink() ? g_control.link().uplinkLQ : 0,
                 g_parser.crcErrorCount());

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "CRSF firmware starting (shared crsf-core)");

    const esp_err_t err = g_uart.begin(kPort, kTxPin, kRxPin, kBaud);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "UART init failed: %s", esp_err_to_name(err));
        return;
    }

    xTaskCreate(crsf_rx_task,   "crsf_rx", 4096, nullptr, 10, nullptr);
    xTaskCreate(telemetry_task, "crsf_tx", 4096, nullptr, 5,  nullptr);
}
