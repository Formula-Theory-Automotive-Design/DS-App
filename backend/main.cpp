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
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

#include "crsf_encoder.h"
#include "crsf_parser.h"
#include "crsf_params.h"
#include "crsf_telemetry.h"
#include "hid/joystick.h"
#include "mapping/channel_map.h"
#include "radio/elrs_link.h"
#include "radio/link_config.h"
#include "serial/echo_filter.h"
#include "serial/serial_port.h"
#include "state/state.h"
#include "websocket/json_util.h"
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
    j += ",\"echoes\":" + std::to_string(s.echoFrames);
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
    int          baud      = 400000;   // ELRS module-bay CRSF default
    std::uint16_t wsPort   = 8080;
    std::string  mapPath   = "mapping.conf";
    std::string  radioPath = "radio.conf";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) { return (i + 1 < argc) ? argv[++i] : def; };
        if (a == "--serial" || a == "-s") serialDev = next(serialDev.c_str());
        else if (a == "--baud" || a == "-b") baud = std::atoi(next("400000"));
        else if (a == "--js" || a == "-j") jsDev = next(jsDev.c_str());
        else if (a == "--port" || a == "-p") wsPort = static_cast<std::uint16_t>(std::atoi(next("8080")));
        else if (a == "--map" || a == "-m") mapPath = next(mapPath.c_str());
        else if (a == "--radio" || a == "-r") radioPath = next(radioPath.c_str());
        else {
            std::fprintf(stderr,
                "usage: %s [--serial DEV] [--baud N] [--js DEV] [--port N] "
                "[--map FILE] [--radio FILE]\n",
                argv[0]);
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
        std::printf("rx:        link + battery telemetry (single-wire echo protection on)\n");
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

    // --- Channel mapping (hot-reloadable) ------------------------------------
    backend::ChannelMap chMap;
    std::string mapErr;
    if (chMap.loadFile(mapPath, mapErr)) {
        std::printf("mapping:   %s (hot-reload on save)\n", mapPath.c_str());
    } else {
        std::printf("mapping:   %s not loaded (%s) - using default axisK->chK\n",
                    mapPath.c_str(), mapErr.c_str());
    }

    // --- Radio / ELRS link config (hot-reloadable) ---------------------------
    backend::LinkConfig linkCfg;
    backend::ElrsLink   elrsLink;
    std::mutex          serialWriteMtx;  // serial.write() is shared by HID + link
    std::string linkErr;
    if (linkCfg.loadFile(radioPath, linkErr)) {
        std::printf("radio:     %s (%zu settings, pushed to module on save)\n",
                    radioPath.c_str(), linkCfg.desired().size());
    } else {
        std::printf("radio:     %s not loaded (%s) - module settings untouched\n",
                    radioPath.c_str(), linkErr.c_str());
    }

    // --- WebSocket -----------------------------------------------------------
    backend::WebSocketServer ws;
    if (!ws.start(wsPort)) {
        std::fprintf(stderr, "error: websocket: %s\n", ws.lastError().c_str());
        return 1;
    }
    std::printf("websocket: ws://0.0.0.0:%u\n", wsPort);

    // --- Config editing over the WebSocket -----------------------------------
    // The frontend can read and rewrite the two hot-reloadable config files.
    // A save is validated by parsing into a throwaway config first, so a bad
    // edit is rejected without ever clobbering the working file; on success the
    // file is replaced atomically, and the existing mtime watchers pick it up
    // (mapping applies on the next TX tick; radio re-pushes to the module).
    auto configPath = [&](const std::string& name) -> const std::string* {
        if (name == "mapping") return &mapPath;
        if (name == "radio")   return &radioPath;
        return nullptr;  // unknown -> ignored (no path traversal)
    };
    ws.setMessageHandler([&](const std::string& msg,
                             const backend::WebSocketServer::ReplyFn& reply) {
        std::string type;
        if (!backend::jsonGetString(msg, "type", type)) return;

        if (type == "getConfig") {
            std::string name;
            if (!backend::jsonGetString(msg, "name", name)) return;
            const std::string* path = configPath(name);
            if (!path) return;
            std::ifstream in(*path, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
            const std::string err = in.is_open() ? "" : ("missing " + *path);
            reply("{\"type\":\"config\",\"name\":\"" + name +
                  "\",\"content\":\"" + backend::jsonEscape(content) +
                  "\",\"error\":\"" + backend::jsonEscape(err) + "\"}");
            return;
        }

        if (type == "saveConfig") {
            std::string name, content;
            if (!backend::jsonGetString(msg, "name", name) ||
                !backend::jsonGetString(msg, "content", content)) return;
            const std::string* path = configPath(name);
            if (!path) return;

            const std::string tmp = *path + ".tmp";
            bool ok = false;
            std::string err;
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out) {
                    err = "cannot write " + tmp;
                } else {
                    out.write(content.data(),
                              static_cast<std::streamsize>(content.size()));
                    out.close();
                    if (name == "mapping") { backend::ChannelMap m; ok = m.loadFile(tmp, err); }
                    else                   { backend::LinkConfig l; ok = l.loadFile(tmp, err); }
                }
            }
            if (ok && std::rename(tmp.c_str(), path->c_str()) != 0) {
                ok = false;
                err = "could not replace " + *path;
            }
            if (!ok) std::remove(tmp.c_str());

            reply("{\"type\":\"saveResult\",\"name\":\"" + name +
                  "\",\"ok\":" + (ok ? "true" : "false") +
                  ",\"error\":\"" + backend::jsonEscape(err) + "\"}");
        }
    });

    std::printf("config:    mapping + radio editable from the frontend\n");
    std::printf("running. Ctrl-C to stop.\n");

    std::thread wsThread([&] { ws.run(); });

    // --- Serial RX thread ----------------------------------------------------
    std::thread rxThread([&] {
        if (!haveSerial) return;
        crsf::CRSFParser parser;
        std::uint8_t buf[256];
        std::uint32_t telemetryPackets = 0;
        std::uint32_t echoFrames       = 0;
        while (g_running) {
            long n = serial.read(buf, sizeof(buf));
            if (n < 0) break;
            for (long i = 0; i < n; ++i) {
                if (!parser.feed(buf[i])) continue;
                crsf::CRSFPacket pkt = parser.getPacket();

                // Single-wire half-duplex: drop the frames we transmitted, which
                // the shared S.Port line echoes back into our own receiver.
                const bool echo = backend::isEchoFrame(pkt.type);
                if (echo) {
                    ++echoFrames;
                } else {
                    ++telemetryPackets;
                    elrsLink.onPacket(pkt);  // ELRS parameter-menu replies
                }

                state.update([&](backend::SystemState& s) {
                    s.packetCount = telemetryPackets;
                    s.echoFrames  = echoFrames;
                    s.crcErrors   = parser.crcErrorCount();
                    if (echo) return;
                    crsf::BatteryData b;
                    crsf::LinkStats   l;
                    if (crsf::decodeBattery(pkt, b))        { s.battery = b; s.hasBattery = true; }
                    else if (crsf::decodeLinkStats(pkt, l)) { s.link = l;    s.hasLink = true; }
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

                    // Pick up edits whether or not a map was loaded at startup
                    // (a freshly-saved mapping should start applying live).
                    std::string e;
                    if (chMap.reloadIfChanged(mapPath, e))
                        std::printf("mapping:   reloaded %s\n", mapPath.c_str());
                    else if (!e.empty())
                        std::printf("mapping:   %s error, kept previous (%s)\n",
                                    mapPath.c_str(), e.c_str());

                    if (chMap.loaded()) {
                        chMap.apply(js, ch);
                    } else {
                        for (int i = 0; i < js.numAxes && i < 4; ++i)
                            ch.channels[i] = axisToChannel(js.axes[i]);
                        for (int i = 0; i < js.numButtons && i < 8; ++i)
                            ch.channels[4 + i] = js.buttons[i]
                                ? crsf::CRSF_CHANNEL_VALUE_MAX : crsf::CRSF_CHANNEL_VALUE_MIN;
                    }
                    std::vector<std::uint8_t> frame = packer.encodeRCChannels(ch);
                    {
                        std::lock_guard<std::mutex> lk(serialWriteMtx);
                        serial.write(frame);
                    }
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

    // --- Link config thread (radio.conf -> ELRS module) ---------------------
    // Pushes the radio settings once at startup, then again whenever the file
    // changes. Shares the serial port with the RC-TX thread via serialWriteMtx.
    std::thread linkThread([&] {
        if (!haveSerial) return;
        auto running = [&] { return g_running.load() && haveSerial; };
        auto send = [&](const std::vector<std::uint8_t>& f) {
            std::lock_guard<std::mutex> lk(serialWriteMtx);
            return serial.write(f);
        };
        auto pushSettings = [&] {
            if (!linkCfg.loaded() || linkCfg.desired().empty()) return;
            backend::ElrsLink::ApplyResult r = elrsLink.apply(linkCfg.desired(), send, running);
            if (!r.connected) {
                std::printf("radio:     module not responding - settings not applied\n");
            } else {
                std::printf("radio:     %s - %d setting(s) changed\n",
                            r.deviceName.c_str(), r.changed);
            }
            for (const std::string& line : r.log)
                std::printf("radio:       %s\n", line.c_str());
        };

        // Give the module a moment to boot before the first ping.
        for (int i = 0; i < 10 && running(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pushSettings();

        while (running()) {
            std::string e;
            if (linkCfg.reloadIfChanged(radioPath, e)) {
                std::printf("radio:     reloaded %s\n", radioPath.c_str());
                pushSettings();
            } else if (!e.empty()) {
                std::printf("radio:     %s error, kept previous (%s)\n",
                            radioPath.c_str(), e.c_str());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    // --- Wait for shutdown 
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\nstopping...\n");
    serial.close();   // unblocks rxThread's read()
    ws.stop();
    rxThread.join();
    hidThread.join();
    pushThread.join();
    linkThread.join();
    wsThread.join();
    std::printf("done.\n");
    return 0;
}
