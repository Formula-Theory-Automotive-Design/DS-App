// backend — desktop driver-station app.
//
// Threads (per the plan's thread model):
//   1. Serial RX     : read FTDI bytes -> crsf-core parser -> telemetry state
//   2. HID poll / TX : read joystick -> joystick state -> encode RC -> serial
//   3. WebSocket      : accept clients, run the RFC 6455 event loop
//   4. Push           : snapshot state at a fixed rate -> broadcast JSON
//
// Hardware is optional: if the serial port or joystick is absent the relevant
// threads simply idle, so the WebSocket API still comes up for the frontend.
//
// Usage: backend [--serial DEV] [--baud N] [--js DEV] [--port N]

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "crsf_encoder.h"
#include "crsf_parser.h"
#include "crsf_telemetry.h"
#include "hid/joystick.h"
#include "serial/serial_port.h"
#include "state/state.h"
#include "websocket/ws_server.h"

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

// ---------------------------------------------------------------------------
// Minimal JSON encoding (numbers + the few shapes we emit)
// ---------------------------------------------------------------------------
std::string jnum(double v, int prec = 2) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

std::string telemetryJson(const backend::SystemState& s) {
    std::string j = "{\"type\":\"telemetry\"";
    j += ",\"packets\":" + std::to_string(s.packetCount);
    j += ",\"crcErrors\":" + std::to_string(s.crcErrors);
    if (s.hasLink) {
        j += ",\"rssi\":" + std::to_string(s.link.uplinkRSSI1);
        j += ",\"lq\":" + std::to_string(s.link.uplinkLQ);
        j += ",\"snr\":" + std::to_string(static_cast<int>(s.link.uplinkSNR));
    }
    if (s.hasBattery) {
        j += ",\"voltage\":" + jnum(s.battery.voltage);
        j += ",\"current\":" + jnum(s.battery.current);
        j += ",\"capacity\":" + std::to_string(s.battery.capacity);
        j += ",\"remaining\":" + std::to_string(s.battery.remaining);
    }
    if (s.hasGps) {
        j += ",\"lat\":" + jnum(s.gps.latitude, 6);
        j += ",\"lon\":" + jnum(s.gps.longitude, 6);
        j += ",\"sats\":" + std::to_string(s.gps.satellites);
    }
    j += "}";
    return j;
}

std::string joystickJson(const backend::JoystickState& js) {
    std::string j = "{\"type\":\"joystick\",\"connected\":";
    j += js.connected ? "true" : "false";
    j += ",\"axes\":[";
    for (int i = 0; i < js.numAxes && i < (int)backend::JoystickState::kMaxAxes; ++i) {
        if (i) j += ",";
        j += jnum(js.axes[i], 3);
    }
    j += "],\"buttons\":[";
    for (int i = 0; i < js.numButtons && i < (int)backend::JoystickState::kMaxButtons; ++i) {
        if (i) j += ",";
        j += js.buttons[i] ? "1" : "0";
    }
    j += "]}";
    return j;
}

// Map a normalized [-1,1] axis to an 11-bit CRSF channel value.
std::uint16_t axisToChannel(float v) {
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;
    const float span = (crsf::CRSF_CHANNEL_VALUE_MAX - crsf::CRSF_CHANNEL_VALUE_MIN) / 2.0f;
    return static_cast<std::uint16_t>(crsf::CRSF_CHANNEL_VALUE_MID + v * span);
}

}  // namespace

int main(int argc, char** argv) {
    std::string serialDev = "/dev/ttyUSB0";
    std::string jsDev      = "/dev/input/js0";
    int          baud      = 420000;
    std::uint16_t wsPort   = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) { return (i + 1 < argc) ? argv[++i] : def; };
        if (a == "--serial") serialDev = next(serialDev.c_str());
        else if (a == "--baud") baud = std::atoi(next("420000"));
        else if (a == "--js") jsDev = next(jsDev.c_str());
        else if (a == "--port") wsPort = static_cast<std::uint16_t>(std::atoi(next("8080")));
        else {
            std::fprintf(stderr,
                "usage: %s [--serial DEV] [--baud N] [--js DEV] [--port N]\n", argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    backend::StateManager state;

    // --- Serial (optional) ---------------------------------------------------
    backend::SerialPort serial;
    const bool haveSerial = serial.open(serialDev, baud);
    if (haveSerial) {
        std::printf("serial:    %s @ %d baud\n", serialDev.c_str(), baud);
    } else {
        std::printf("serial:    unavailable (%s) - telemetry RX/TX disabled\n",
                    serial.lastError().c_str());
    }

    // --- Joystick (optional) -------------------------------------------------
    backend::Joystick joystick;
    bool haveJs = joystick.open(jsDev);
    if (haveJs) {
        std::printf("joystick:  %s (%s) - %d axes, %d buttons\n",
                    jsDev.c_str(), joystick.name().c_str(),
                    joystick.state().numAxes, joystick.state().numButtons);
    } else {
        std::printf("joystick:  unavailable (%s)\n", joystick.lastError().c_str());
    }

    // --- WebSocket -----------------------------------------------------------
    backend::WebSocketServer ws;
    if (!ws.start(wsPort)) {
        std::fprintf(stderr, "error: websocket: %s\n", ws.lastError().c_str());
        return 1;
    }
    std::printf("websocket: ws://0.0.0.0:%u\n", wsPort);
    std::printf("running. Ctrl-C to stop.\n");

    std::thread wsThread([&] { ws.run(); });

    // --- Serial RX thread ----------------------------------------------------
    std::thread rxThread([&] {
        if (!haveSerial) return;
        crsf::CRSFParser parser;
        std::uint8_t buf[256];
        while (g_running) {
            long n = serial.read(buf, sizeof(buf));
            if (n < 0) break;
            for (long i = 0; i < n; ++i) {
                if (!parser.feed(buf[i])) continue;
                crsf::CRSFPacket pkt = parser.getPacket();
                state.update([&](backend::SystemState& s) {
                    s.packetCount = parser.packetCount();
                    s.crcErrors   = parser.crcErrorCount();
                    crsf::BatteryData b;
                    crsf::GPSData     g;
                    crsf::LinkStats   l;
                    if (crsf::decodeBattery(pkt, b))   { s.battery = b; s.hasBattery = true; }
                    else if (crsf::decodeGPS(pkt, g))  { s.gps = g;     s.hasGps = true; }
                    else if (crsf::decodeLinkStats(pkt, l)) { s.link = l; s.hasLink = true; }
                });
            }
        }
    });

    // --- HID poll + RC TX thread --------------------------------------------
    std::thread hidThread([&] {
        crsf::CRSFPacker packer(static_cast<std::uint8_t>(crsf::CRSFAddress::Transmitter));
        while (g_running) {
            if (haveJs) {
                if (!joystick.poll()) haveJs = false;  // unplugged
                state.update([&](backend::SystemState& s) { s.joystick = joystick.state(); });

                if (haveSerial) {
                    const backend::JoystickState& js = joystick.state();
                    crsf::RCChannels ch;  // defaults: center, throttle low
                    for (int i = 0; i < js.numAxes && i < 4; ++i) {
                        ch.channels[i] = axisToChannel(js.axes[i]);
                    }
                    for (int i = 0; i < js.numButtons && i < 8; ++i) {
                        ch.channels[4 + i] = js.buttons[i]
                            ? crsf::CRSF_CHANNEL_VALUE_MAX : crsf::CRSF_CHANNEL_VALUE_MIN;
                    }
                    serial.write(packer.encodeRCChannels(ch));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // ~100 Hz
        }
    });

    // --- Push thread (state -> WebSocket) -----------------------------------
    std::thread pushThread([&] {
        while (g_running) {
            backend::SystemState s = state.snapshot();
            ws.broadcast(telemetryJson(s));
            ws.broadcast(joystickJson(s.joystick));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 20 Hz
        }
    });

    // --- Wait for shutdown ---------------------------------------------------
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\nstopping...\n");
    serial.close();   // unblocks rxThread's read()
    ws.stop();
    rxThread.join();
    hidThread.join();
    pushThread.join();
    wsThread.join();
    std::printf("done.\n");
    return 0;
}
